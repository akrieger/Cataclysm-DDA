#include "zzip.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include <zstd/zstd.h>
#include <zstd/common/mem.h>
#include <zstd/common/xxhash.h>

#include "cata_scope_helpers.h"
#include "filesystem.h"
#include "flexbuffer_cache.h"
#include "flexbuffer_json.h"

namespace
{

// Implementations of the mandatory Json helper types
// to use flexbuffers/JsonObject with zzips.

struct zzip_flexbuffer_storage : flexbuffer_storage {
    std::shared_ptr<mmap_file> mmap_handle_;

    explicit zzip_flexbuffer_storage( std::shared_ptr<mmap_file> mmap_handle )
        : mmap_handle_{ std::move( mmap_handle ) } {
    }

    const uint8_t *data() const override {
        return static_cast<const uint8_t *>( mmap_handle_->base() );
    }
    size_t size() const override {
        return mmap_handle_->len();
    }
};

struct zzip_flexbuffer : parsed_flexbuffer {
    explicit zzip_flexbuffer( std::shared_ptr<flexbuffer_storage> storage )
        : parsed_flexbuffer( std::move( storage ) ) {
    }

    bool is_stale() const override {
        return false;
    }

    std::unique_ptr<std::istream> get_source_stream() const noexcept( false ) override {
        return nullptr;
    }

    std::filesystem::path get_source_path() const noexcept override {
        return {};
    }
};

struct zzip_vector_storage : flexbuffer_storage {
    std::vector<std::byte> buffer_;

    explicit zzip_vector_storage( std::vector<std::byte> &&buffer )
        : buffer_{ std::move( buffer ) } {
    }

    const uint8_t *data() const override {
        return reinterpret_cast<const uint8_t *>( buffer_.data() );
    }

    size_t size() const override {
        return buffer_.size();
    }
};

// To save time, and because we aren't multithreading, we can cache zstd
// compress and decompress contexts, indexed by dictionary path.
struct cached_zstd_context {
    ZSTD_CCtx *cctx = nullptr;
    ZSTD_DCtx *dctx = nullptr;

    cached_zstd_context(
        ZSTD_CCtx *cctx,
        ZSTD_DCtx *dctx )
        : cctx{ cctx },
          dctx{ dctx } {
    }

    cached_zstd_context( cached_zstd_context const & ) = delete;
    cached_zstd_context( cached_zstd_context &&rhs ) noexcept : cctx( rhs.cctx ), dctx( rhs.dctx ) {
        rhs.cctx = nullptr;
        rhs.dctx = nullptr;
    }

    cached_zstd_context &operator=( cached_zstd_context const & ) = delete;
    cached_zstd_context &operator=( cached_zstd_context &&rhs ) noexcept {
        if( &rhs == this ) {
            return *this;
        }
        if( cctx ) {
            ZSTD_freeCCtx( cctx );
        }
        cctx = rhs.cctx;
        rhs.cctx = nullptr;

        if( dctx ) {
            ZSTD_freeDCtx( dctx );
        }
        dctx = rhs.dctx;
        rhs.dctx = nullptr;
        return *this;
    }

    ~cached_zstd_context() {
        ZSTD_freeCCtx( cctx );
        ZSTD_freeDCtx( dctx );
    }
};

std::unordered_map<std::string, cached_zstd_context> cached_contexts;

} // namespace

struct zzip::compressed_entry {
    std::string path;
    size_t offset;
    size_t len;
};

namespace
{

constexpr unsigned int kFileNameMagic = 0;
constexpr unsigned int kChecksumMagic = 1;

constexpr size_t kChecksumFrameSize = ZSTD_SKIPPABLEHEADERSIZE + sizeof( uint64_t );
constexpr uint64_t kCheckumSeed = 0x1337C0DE;

constexpr const std::string_view kEntriesKey = "entries";
constexpr const std::string_view kEntryOffsetKey = "offset";
constexpr const std::string_view kEntryLenKey = "len";

constexpr const std::string_view kMetaKey = "meta";
constexpr const std::string_view kMetaContentEndKey = "content_end";
constexpr const std::string_view kMetaTotalContentSizeKey = "total_content_size";

struct zzip_file_entry {
    size_t offset = 0;
    size_t len = 0;
};

struct zzip_meta {
    size_t content_end = 0;
    size_t total_content_size = 0;
};


// A helper class for safely and consistently accessing metadata from the footer index.
struct zzip_footer {
    const JsonObject &footer_;

    explicit zzip_footer( const JsonObject &jsobj )
        : footer_( jsobj ) {
    }

    bool is_valid() const {
        return footer_.has_object( kMetaKey );
    }

    zzip_meta get_meta() const {
        JsonObject meta_obj = footer_.get_object( kMetaKey );
        meta_obj.allow_omitted_members();
        size_t content_end = meta_obj.get_int( kMetaContentEndKey );
        size_t total_content_size = meta_obj.get_int( kMetaTotalContentSizeKey );
        return zzip_meta{ content_end, total_content_size };
    }

    std::optional<zzip_file_entry> get_entry( const std::filesystem::path &path ) const {
        std::optional<zzip_file_entry> entry;

        std::optional<JsonValue> entries_opt = footer_.get_member_opt( kEntriesKey );

        if( !footer_.has_object( kEntriesKey ) ) {
            return entry;
        }

        JsonObject entries = footer_.get_object( kEntriesKey );
        entries.allow_omitted_members();

        std::string path_str = path.generic_u8string();
        std::optional<JsonValue> entry_opt = entries.get_member_opt( path_str );
        if( entry_opt.has_value() ) {
            JsonObject entry_obj = *entry_opt;
            entry_obj.allow_omitted_members();
            size_t offset = entry_obj.get_int( kEntryOffsetKey );
            size_t len = entry_obj.get_int( kEntryLenKey );
            entry.emplace( zzip_file_entry{ offset, len } );
        }

        return entry;
    }

    std::vector<zzip::compressed_entry> get_entries() const {
        std::vector<zzip::compressed_entry> compressed_entries;

        if( footer_.has_object( kEntriesKey ) ) {
            JsonObject json_entries = footer_.get_object( kEntriesKey );
            compressed_entries.reserve( json_entries.size() );

            for( const JsonMember &entry : json_entries ) {
                JsonObject entry_object = entry.get_object();
                compressed_entries.emplace_back( zzip::compressed_entry{
                    entry.name(),
                    static_cast<size_t>( entry_object.get_int( kEntryOffsetKey ) ),
                    static_cast<size_t>( entry_object.get_int( kEntryLenKey ) )
                } );
            }
        }
        return compressed_entries;
    }
};

} // namespace

struct zzip::context {
    context( ZSTD_CCtx *cctx, ZSTD_DCtx *dctx )
        : cctx{ cctx }, dctx{ dctx } {
    }
    ZSTD_CCtx *cctx;
    ZSTD_DCtx *dctx;
};

zzip::zzip( std::filesystem::path path, std::shared_ptr<mmap_file> file, JsonObject footer )
    : path_{ std::move( path ) },
      file_{ std::move( file ) },
      footer_{ std::move( footer ) }
{
}

zzip::~zzip() noexcept
{
    footer_.allow_omitted_members();
}

std::shared_ptr<zzip> zzip::load( const std::filesystem::path &path,
                                  const std::filesystem::path &dictionary_path )
{
    std::shared_ptr<zzip> zip;
    std::shared_ptr<mmap_file> file = mmap_file::map_writeable_file( path );

    flexbuffers::Reference root;
    std::shared_ptr<parsed_flexbuffer> flexbuffer;
    JsonObject footer;
    bool needs_recovery = false;

    if( file->len() > 0 ) {
        try {
            std::shared_ptr<zzip_flexbuffer_storage> storage{ std::make_shared<zzip_flexbuffer_storage>( file ) };

            root = flexbuffer_root_from_storage( storage );
            flexbuffer = std::make_shared<zzip_flexbuffer>( std::move( storage ) );
            footer = JsonValue( std::move( flexbuffer ), root, nullptr, 0 );
            zzip_footer f{ footer };
            if( !f.is_valid() ) {
                needs_recovery = true;
            }
        } catch( std::exception &e ) {
            // Something went wrong. Try to recover the file.
            needs_recovery = true;
        }
    }

    zip = std::shared_ptr<zzip>( new zzip( path, std::move( file ), std::move( footer ) ) );

    ZSTD_CCtx *cctx;
    ZSTD_DCtx *dctx;
    if( dictionary_path.empty() ) {
        cctx = ZSTD_createCCtx();
        dctx = ZSTD_createDCtx();
    } else if( auto it = cached_contexts.find( dictionary_path.string() );
               it != cached_contexts.end() ) {
        cctx = it->second.cctx;
        dctx = it->second.dctx;
    } else {
        std::shared_ptr<const mmap_file> dictionary = mmap_file::map_file( dictionary_path );
        cctx = ZSTD_createCCtx();
        ZSTD_CCtx_loadDictionary( cctx, dictionary->base(), dictionary->len() );
        ZSTD_CCtx_setParameter( cctx, ZSTD_c_compressionLevel, 7 );
        dctx = ZSTD_createDCtx();
        ZSTD_DCtx_loadDictionary( dctx, dictionary->base(), dictionary->len() );
        cached_contexts.emplace( dictionary_path.string(), cached_zstd_context{ cctx, dctx } );
    }

    zip->ctx_ = std::make_unique<zzip::context>( cctx, dctx );

    if( needs_recovery ) {
        zip->recover_lost_footer();
    }

    return zip;
}

namespace
{

// Given a pointer and length of an encoded entry, skips the metadata frames
// and returns a pointer to the compressed data frame + its length.
std::pair<void *, size_t> skip_header_frames( void *entry_base, size_t entry_len )
{
    char *base = static_cast<char *>( entry_base );
    while( ZSTD_isSkippableFrame( base, entry_len ) ) {
        size_t skip_offset = ZSTD_findFrameCompressedSize( base, entry_len );
        base += skip_offset;
        entry_len -= skip_offset;
    }
    return { base, entry_len };
}

} // namespace

bool zzip::add_file( const std::filesystem::path &zzip_relative_path, std::string_view content )
{
    size_t estimated_size = ZSTD_compressBound( content.length() );

    JsonObject footer_copy = copy_footer();
    footer_copy.allow_omitted_members();
    zzip_footer footer{ footer_copy };

    std::optional<zzip_meta> meta_opt = footer.get_meta();
    size_t old_content_end = 0;
    if( meta_opt.has_value() ) {
        old_content_end = meta_opt->content_end;
    }

    std::string relative_path_string = zzip_relative_path.generic_u8string();
    // Add 1024 bytes padding for the footer. Should be very plenty.
    // Skippable frames are 8 bytes of header plus the content
    if( !ensure_capacity_for(
            old_content_end + ZSTD_SKIPPABLEHEADERSIZE + relative_path_string.length() + kChecksumFrameSize +
            estimated_size + 1024 ) ) {
        return false;
    }

    size_t final_size = write_file_at( relative_path_string, content, old_content_end );

    if( final_size == 0 ) {
        return false;
    }

    std::vector<compressed_entry> new_entry;
    new_entry.emplace_back( zzip::compressed_entry{
        std::move( relative_path_string ),
        old_content_end,
        final_size
    } );

    if( !update_footer( footer_copy, old_content_end + final_size, new_entry ) ) {
        return false;
    }

    return true;
}

bool zzip::has_file( const std::filesystem::path &zzip_relative_path ) const
{
    zzip_footer footer{ footer_ };
    std::optional<zzip_file_entry> fparams = footer.get_entry( zzip_relative_path );
    return fparams.has_value();
}

size_t zzip::get_file_size( const std::filesystem::path &zzip_relative_path ) const
{
    zzip_footer footer{ footer_ };
    std::optional<zzip_file_entry> fparams = footer.get_entry( zzip_relative_path );
    if( !fparams.has_value() ) {
        return 0;
    }
    void *base = file_base_plus( fparams->offset );
    size_t len = fparams->len;
    std::tie( base, len ) = skip_header_frames( base, len );
    return ZSTD_decompressBound( base, len );
}

std::vector<std::byte> zzip::get_file( const std::filesystem::path &zzip_relative_path ) const
{
    std::vector<std::byte> buf{};
    size_t size = get_file_size( zzip_relative_path );
    buf.resize( size );
    size_t final_size = get_file_to(zzip_relative_path, buf.data(), size);
    // get_file_to returns 0 on error so we return an empty array.
    buf.resize( final_size );
    return buf;
}

size_t zzip::get_file_to( const std::filesystem::path &zzip_relative_path, std::byte *dest,
                          size_t dest_len ) const
{
    zzip_footer footer{ footer_ };
    std::optional<zzip_file_entry> fparams = footer.get_entry( zzip_relative_path );
    if( !fparams.has_value() ) {
        return 0;
    }
    size_t file_len = fparams->len;
    void *file_base = file_base_plus( fparams->offset );
    // TODO should we checksum the file before attempting decompressing?
    std::tie( file_base, file_len ) = skip_header_frames( file_base, file_len );
    unsigned long long file_size = ZSTD_decompressBound( file_base, file_len );
    if( dest_len < file_size ) {
        return 0;
    }
    size_t actual = ZSTD_decompressDCtx( ctx_->dctx, dest, dest_len, file_base, file_len );
    return actual;
}

// Returns a copy of the footer json as a fresh independent object, not backed by the zzip.
JsonObject zzip::copy_footer() const
{
    if( !file_ ) {
        return footer_;
    } else {
        zzip_footer footer{ footer_ };
        std::optional<zzip_meta> meta_opt = footer.get_meta();
        size_t old_content_end = 0;
        if( meta_opt.has_value() ) {
            old_content_end = meta_opt->content_end;
        }
        if( file_->len() <= old_content_end ) {
            // This should really be impossible.
            return JsonObject{};
        }
        size_t footer_size = file_->len() - old_content_end;
        std::vector<std::byte> footer_buf;
        footer_buf.resize( footer_size );
        memcpy( footer_buf.data(), file_base_plus( old_content_end ), footer_size );
        std::shared_ptr<flexbuffer_storage> footer_storage = std::make_shared<zzip_vector_storage>(
                    std::move( footer_buf )
                );
        flexbuffers::Reference root = flexbuffer_root_from_storage( footer_storage );
        std::shared_ptr<parsed_flexbuffer> footer_flexbuffer = std::make_shared<zzip_flexbuffer>(
                    std::move( footer_storage )
                );
        return JsonValue( std::move( footer_flexbuffer ), root, nullptr, 0 );
    }
}

// Ensures the zzip file has room to write at least this many bytes.
// Returns the guaranteed size of the file, which is at least bytes large.
size_t zzip::ensure_capacity_for( size_t bytes )
{
    // Round up to a whole page. Drives nowadays are all 4k pages.
    size_t needed_pages = std::max( size_t{1}, ( bytes + ( 4 * 1024 ) - 1 ) / ( 4 * 1024 ) );

    size_t new_size = needed_pages * 4 * 1024;

    if( file_->len() < new_size && !file_->resize_file( new_size ) ) {
        return 0;
    }
    return new_size;
}

// Actually performs the compression and encoding of a file into the zzip.
size_t zzip::write_file_at( std::string_view filename, std::string_view content, size_t offset )
{
    // The format of a compressed entry is a series of zstd frames.
    // There are an unbounded number of leading skippable frames of unspecified content.
    // At present, we write two skippable frames per entry:
    //   - A frame for the filename of the entry.
    //   - A frame for a 64 bit XXH checksum of the entire compressed frame.
    //   - The actual compressed frame.
    // Returns the size of the entire file entry
    // (i.e. from the start of the first skippable frame to the end of the compressed data).

    if( file_->len() <= offset ) {
        return 0;
    }

    size_t header_size = ZSTD_writeSkippableFrame(
                             file_base_plus( offset ),
                             file_capacity_at( offset ),
                             filename.data(),
                             filename.length(),
                             kFileNameMagic
                         );
    if( ZSTD_isError( header_size ) ) {
        return 0;
    }
    offset += header_size;
    // Make room for the checksum frame before the file.
    offset += kChecksumFrameSize;
    size_t file_size = ZSTD_compress2(
                           ctx_->cctx,
                           file_base_plus( offset ),
                           file_capacity_at( offset ),
                           content.data(),
                           content.size()
                       );
    if( ZSTD_isError( file_size ) ) {
        return 0;
    }
    uint64_t checksum = XXH64( file_base_plus( offset ), file_size, kCheckumSeed );
    uint64_t checksum_le = 0;
    MEM_writeLE64( &checksum_le, checksum );
    size_t checksum_size = ZSTD_writeSkippableFrame(
                               file_base_plus( offset - kChecksumFrameSize ),
                               kChecksumFrameSize,
                               reinterpret_cast<const char *>( &checksum_le ),
                               sizeof( checksum_le ),
                               kChecksumMagic
                           );
    if( ZSTD_isError( checksum_size ) || checksum_size != kChecksumFrameSize ) {
        return 0;
    }
    return header_size + checksum_size + file_size;
}

// Writes a new footer at the end of the zzip, copying old entries from the given
// original JsonObject and inserting the given new entries.
// If shrink_to_fit is true, will shrink the file as needed to eliminate padding bytes
// between the content and the footer.
bool zzip::update_footer( JsonObject const &original_footer, size_t content_end,
                          const std::vector<compressed_entry> &entries, bool shrink_to_fit )
{
    std::unordered_set<std::string> processed_files;
    flexbuffers::Builder builder;
    size_t root_start = builder.StartMap();
    size_t total_content_size = 0;
    {
        size_t entries_start = builder.StartMap( kEntriesKey.data() );
        // NOLINTNEXTLINE(cata-almost-never-auto)
        for( const auto& [zzip_relative_path, offset, len] : entries ) {
            std::string new_filename = zzip_relative_path;
            {
                size_t new_entry_map = builder.StartMap( new_filename.c_str() );
                builder.UInt( kEntryOffsetKey.data(), offset );
                builder.UInt( kEntryLenKey.data(), len );
                builder.EndMap( new_entry_map );
                content_end = std::max( content_end, offset + len );
                total_content_size += len;
                processed_files.insert( new_filename );
            }
        }
        for( JsonMember entry : original_footer.get_object( kEntriesKey.data() ) ) {
            std::string old_filename = entry.name();
            if( !processed_files.count( old_filename ) ) {
                size_t entry_map = builder.StartMap( old_filename.c_str() );
                JsonObject old_data = entry.get_object();
                size_t offset = old_data.get_int( kEntryOffsetKey );
                size_t len = old_data.get_int( kEntryLenKey );
                builder.UInt( kEntryOffsetKey.data(), offset );
                builder.UInt( kEntryLenKey.data(), len );
                content_end = std::max( content_end, offset + len );
                total_content_size += len;
                builder.EndMap( entry_map );
            }
        }
        builder.EndMap( entries_start );
    }
    {
        size_t meta_start = builder.StartMap( kMetaKey.data() );
        builder.UInt( kMetaContentEndKey.data(), content_end );
        builder.UInt( kMetaTotalContentSizeKey.data(), total_content_size );
        builder.EndMap( meta_start );
    }
    builder.EndMap( root_start );
    builder.Finish();
    auto buf = builder.GetBuffer();

    if( shrink_to_fit ) {
        if( !file_->resize_file( content_end + buf.size() ) ) {
            return false;
        }
    } else {
        size_t guaranteed_capacity = ensure_capacity_for( content_end + buf.size() );
        if( guaranteed_capacity == 0 ) {
            return false;
        }
    }

    memcpy( file_base_plus( file_->len() - buf.size() ), buf.data(), buf.size() );

    std::shared_ptr<flexbuffer_storage> new_storage = std::make_shared<zzip_flexbuffer_storage>
            ( file_ );
    flexbuffers::Reference new_root = flexbuffer_root_from_storage( new_storage );
    std::shared_ptr<parsed_flexbuffer> new_flexbuffer = std::make_shared<zzip_flexbuffer>(
                std::move( new_storage )
            );
    footer_ = JsonValue( std::move( new_flexbuffer ), new_root, nullptr, 0 );

    return true;
}

// Scans the zzip to recover what data it can and write a fresh footer.
void zzip::recover_lost_footer()
{
    // For whatever reason, we don't have a valid footer.
    // Recover footer metadata from linear scans of the headers before content.
    std::unordered_map<std::string, compressed_entry> recovered_entries_map;
    const size_t file_len = file_->len();
    size_t recovery_offset = 0;

    // Try to recover as many files as we can
    while( recovery_offset < file_len ) {
        // Scan the headers for the filename and checksum fields.
        // The checksum is for the _compressed_ content, to make sure it is
        // okay to decompress. Not sure if zstd does sensible things when asked
        // to decompress garbage.
        size_t entry_begin = recovery_offset;
        size_t entry_offset = entry_begin;

        std::optional<std::string> filename_opt;
        std::optional<uint64_t> checksum_opt;
        bool recovered_headers = false;
        while( entry_offset < file_len ) {
            size_t file_remaining = file_len - entry_begin;
            // For each entry we expect at least a filename and checksum header.
            void *entry_ptr = file_base_plus( entry_offset );
            if( ZSTD_isSkippableFrame( entry_ptr, file_remaining ) ) {
                size_t frame_size = ZSTD_findFrameCompressedSize( entry_ptr, file_remaining );
                if( ZSTD_isError( frame_size ) ) {
                    break;
                }
                ZSTD_FrameHeader header;
                size_t ec = ZSTD_getFrameHeader( &header, entry_ptr, file_remaining );
                if( ec ) {
                    // Some kind of error, not relevant if file_remaining is too small or it was some other error.
                    break;
                }
                unsigned int magic = header.dictID;
                size_t content_size = header.frameContentSize;
                switch( magic ) {
                    case kFileNameMagic: {
                        // Pre-create a new string with the correct size, and zero-bytes as content.
                        filename_opt.emplace( content_size, '\0' );
                        std::string &filename = *filename_opt;
                        size_t bytes_read = ZSTD_readSkippableFrame(
                                                filename.data(),
                                                filename.size(),
                                                nullptr,
                                                entry_ptr,
                                                file_remaining );
                        if( ZSTD_isError( bytes_read ) || bytes_read != content_size ) {
                            entry_offset = file_len;
                            break;
                        }
                        break;
                    }
                    case kChecksumMagic: {
                        uint64_t checksum_le = 0;
                        size_t bytes_read = ZSTD_readSkippableFrame(
                                                &checksum_le,
                                                sizeof( checksum_le ),
                                                nullptr,
                                                entry_ptr,
                                                file_remaining );
                        if( ZSTD_isError( bytes_read ) || bytes_read != content_size ) {
                            entry_offset = file_len;
                            break;
                        }
                        checksum_opt = MEM_readLE64( &checksum_le );
                        break;
                    }
                    default:
                        break;
                }

                entry_offset += frame_size;
            } else {
                recovered_headers = filename_opt.has_value() && checksum_opt.has_value();
                break;
            }
        }

        if( entry_offset >= file_len ) {
            // We've run off the end of the file and read past the original footer.
            break;
        }
        if( !recovered_headers ) {
            // Missing required metadata.
            break;
        }

        void *file_frame_begin = file_base_plus( entry_offset );
        size_t file_frame_size = ZSTD_findFrameCompressedSize( file_frame_begin, file_len - entry_offset );
        if( ZSTD_isError( file_frame_size ) ) {
            break;
        }
        uint64_t checksum = XXH64( file_frame_begin, file_frame_size, kCheckumSeed );
        if( checksum != checksum_opt ) {
            // Corruption in the compressed frame. Don't try to recover it, assume
            // everything after is lost.
            break;
        }
        recovery_offset = entry_offset + file_frame_size;
        compressed_entry &entry = recovered_entries_map.try_emplace( *filename_opt ).first->second;
        entry = compressed_entry{ *filename_opt, entry_begin, recovery_offset - entry_begin };
    }

    std::vector<compressed_entry> recovered_entries;
    recovered_entries.reserve( recovered_entries_map.size() );
    for( auto &[path, recovered_entry] : recovered_entries_map ) {
        recovered_entries.emplace_back( std::move( recovered_entry ) );
    }

    update_footer( JsonObject{}, recovery_offset, recovered_entries, /* shrink_to_fit = */ true );
}

std::shared_ptr<zzip> zzip::create_from_folder( const std::filesystem::path &path,
        const std::filesystem::path &folder,
        const std::filesystem::path &dictionary )
{
    std::shared_ptr<zzip> zip = zzip::load( path, dictionary );
    JsonObject original_footer = zip->copy_footer();
    zzip_footer footer{ original_footer };
    std::optional<zzip_meta> meta_opt = footer.get_meta();
    size_t content_end = 0;
    if( meta_opt.has_value() ) {
        content_end = meta_opt->content_end;
    }
    size_t raw_input_remaining = 0;
    std::unordered_set<std::string> files_to_insert;
    std::vector<compressed_entry> new_entries;
    for( const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(
             folder ) ) {
        if( !entry.is_regular_file() ) {
            continue;
        }
        raw_input_remaining += entry.file_size();
        files_to_insert.emplace( entry.path().generic_u8string() );
    }

    // Conservatively estimate 10x compression ratio
    size_t input_consumed = 0;
    size_t bytes_written = 0;
    zip->ensure_capacity_for( ( raw_input_remaining / 10 ) + 1024 );

    for( const std::string &file_path_string : files_to_insert ) {
        std::filesystem::path file_path = std::filesystem::u8path( file_path_string );
        std::shared_ptr<const mmap_file> file = mmap_file::map_file( file_path );
        std::string file_relative_path = file_path.lexically_relative( folder ).generic_u8string();
        size_t compressed_size = zip->write_file_at(
                                     file_relative_path,
                                     std::string_view{ static_cast<const char *>( file->base() ), file->len() },
                                     content_end
                                 );
        if( compressed_size == 0 ) {
            // Possibly ran out of file space. Figure out how much is left to insert.
            // Estimate based on comression ratio of data so far.
            size_t needed = raw_input_remaining * ( static_cast<double>( bytes_written ) / input_consumed );
            zip->ensure_capacity_for( content_end + needed + 1024 );

            compressed_size = zip->write_file_at(
                                  file_relative_path,
                                  std::string_view{ static_cast<const char *>( file->base() ), file->len() },
                                  content_end
                              );
            if( compressed_size == 0 ) {
                return nullptr;
            }
        }
        new_entries.emplace_back( compressed_entry{ std::move( file_relative_path ), content_end, compressed_size } );
        bytes_written += compressed_size;
        input_consumed += file->len();
        content_end += compressed_size;
        raw_input_remaining -= file->len();
    }
    zip->update_footer( original_footer, content_end, new_entries, /* shrink_to_fit = */ true );
    return zip;
}

bool zzip::extract_to_folder( const std::filesystem::path &path,
                              const std::filesystem::path &folder,
                              const std::filesystem::path &dictionary )
{
    std::shared_ptr<zzip> zip = zzip::load( path, dictionary );

    for( const JsonMember &entry : zip->footer_.get_object( kEntriesKey ) ) {
        std::filesystem::path filename = std::filesystem::u8path( entry.name() );
        size_t len = zip->get_file_size( filename );
        std::filesystem::path destination_file_path = folder / filename;
        if( !assure_dir_exist( destination_file_path.parent_path() ) ) {
            return false;
        }
        std::shared_ptr<mmap_file> file = mmap_file::map_writeable_file( destination_file_path );
        if( !file || !file->resize_file( len ) ) {
            return false;
        }
        if( zip->get_file_to( filename, static_cast<std::byte *>( file->base() ), file->len() ) != len ) {
            return false;
        }
    }
    return true;
}

bool zzip::compact( double bloat_factor )
{
    zzip_footer footer{ footer_ };
    std::optional<zzip_meta> meta_opt = footer.get_meta();
    if( !meta_opt.has_value() ) {
        return false;
    }

    zzip_meta &meta = *meta_opt;
    size_t max_allowed_size = meta.total_content_size * bloat_factor + 1024;
    // Are we even going to try to compact?
    if( file_->len() < max_allowed_size ) {
        return false;
    }

    // Can we save at least one physical page?
    size_t delta = file_->len() - ( meta.total_content_size + 1024 );
    if( delta < 4 * 1024 ) {
        return false;
    }

    std::vector<compressed_entry> compressed_entries = footer.get_entries();

    // If we were compacting in place, sorting left to right would ensure that we
    // only move entries left that we need to move. Unchanged data would stay on the left.
    // However that exposes us to data loss on power loss because zzip recovery
    // relies on an intact sequence of zstd frames.
    std::sort( compressed_entries.begin(), compressed_entries.end(), []( const compressed_entry & lhs,
    const compressed_entry & rhs ) {
        return lhs.offset < rhs.offset;
    } );

    std::filesystem::path tmp_path = path_;
    tmp_path.replace_extension( ".zzip.tmp" ); // NOLINT(cata-u8-path)

    std::shared_ptr<mmap_file> compacted_file = mmap_file::map_writeable_file( tmp_path );
    if( !compacted_file || !compacted_file->resize_file( meta.total_content_size + 1024 ) ) {
        return false;
    }

    size_t current_end = 0;
    for( compressed_entry &entry : compressed_entries ) {
        memcpy( static_cast<char *>( compacted_file->base() ) + current_end,
                file_base_plus( entry.offset ),
                entry.len
              );
        entry.offset = current_end;
        current_end += entry.len;
    }

    // We can swap the old mmap'd file with the new one
    // and write a fresh footer directly into it.
    JsonObject old_footer{ copy_footer() };
    old_footer.allow_omitted_members();

    file_ = compacted_file;
    compacted_file = nullptr;

    std::error_code ec;
    on_out_of_scope reset_on_failure{ [&] {
            file_ = mmap_file::map_writeable_file( path_ );
            std::filesystem::remove( tmp_path, ec );
        } };

    if( !update_footer( old_footer, current_end, compressed_entries, /* shrink_to_fit = */ true ) ) {
        return false;
    }

    // Swap the tmp file over the real path.
    // Give it a couple tries in case of filesystem races on Windows.
    std::filesystem::rename( tmp_path, path_, ec );
    size_t attempts = 1;
    while( ec && attempts < 3 ) {
        ++attempts;
        std::filesystem::rename( tmp_path, path_, ec );
    }
    if( ec ) {
        return false;
    }
    reset_on_failure.cancel();
    return true;
}

// Can't directly increment void*, have to cast to char* first.
void *zzip::file_base_plus( size_t offset ) const
{
    return static_cast<char *>( file_->base() ) + offset;
}

// A helper to handle underflow. I wouldn't write it if I didn't hit it.
size_t zzip::file_capacity_at( size_t offset ) const
{
    if( file_->len() < offset ) {
        return 0;
    }
    return file_->len() - offset;
}