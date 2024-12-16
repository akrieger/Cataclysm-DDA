#include "zzip.h"

#include <memory>
#include <optional>
#include <vector>

#include <zstd/zdict.h>
#include <zstd/zstd.h>

#include "filesystem.h"
#include "flexbuffer_cache.h"
#include "json_loader.h"
#include "platform_win.h"
#include "perf.h"

namespace
{
struct zzip_flexbuffer_storage : flexbuffer_storage {
    std::shared_ptr<mmap_file> mmap_handle_;

    explicit zzip_flexbuffer_storage( std::shared_ptr<mmap_file> mmap_handle ) : mmap_handle_{ std::move( mmap_handle ) } {}

    const uint8_t *data() const override {
        return static_cast<const uint8_t *>( mmap_handle_->base() );
    }
    size_t size() const override {
        return mmap_handle_->len();
    }
};

struct zzip_flexbuffer : parsed_flexbuffer {
    zzip_flexbuffer( std::shared_ptr<flexbuffer_storage> storage ) : parsed_flexbuffer( std::move(
                    storage ) ) {}

    virtual bool is_stale() const override {
        return false;
    }

    virtual std::unique_ptr<std::istream> get_source_stream() const noexcept( false ) override {
        return nullptr;
    }

    virtual fs::path get_source_path() const noexcept override {
        return {};
    }
};

struct zzip_vector_storage : flexbuffer_storage {
    std::vector<std::byte> buffer_;

    explicit zzip_vector_storage( std::vector<std::byte> &&buffer ) : buffer_{ std::move( buffer ) } {}

    const uint8_t *data() const override {
        return reinterpret_cast<const uint8_t *>( buffer_.data() );
    }

    size_t size() const override {
        return buffer_.size();
    }
};

struct cached_zstd_context {
    ZSTD_CCtx *cctx;
    ZSTD_DCtx *dctx;
};

std::unordered_map<fs::path, cached_zstd_context> cached_contexts;

}

struct zzip::context {
    context(
        ZSTD_CCtx *cctx,
        ZSTD_DCtx *dctx
    ) : cctx{ cctx }, dctx{ dctx } {
    }
    ZSTD_CCtx *cctx;
    ZSTD_DCtx *dctx;
};

zzip::zzip( const fs::path &path, std::shared_ptr<mmap_file> file, JsonObject footer ) : path{path},
    file{std::move( file )},
    footer{ std::move( footer ) } {}
zzip::~zzip() noexcept
{
    footer.allow_omitted_members();
}

std::shared_ptr<zzip> zzip::load( const fs::path &path, const fs::path &dictionary_path )
{
    std::shared_ptr<zzip> zip;
    std::shared_ptr<mmap_file> file = mmap_file::map_writeable_file( path );
    flexbuffers::Reference root;
    std::shared_ptr<parsed_flexbuffer> flexbuffer;
    if( file ) {
        std::shared_ptr<zzip_flexbuffer_storage> storage = std::make_shared<zzip_flexbuffer_storage>
                ( file );

        root = flexbuffer_root_from_storage( storage );
        flexbuffer = std::make_shared<zzip_flexbuffer>( std::move( storage ) );
    } else {
        flexbuffer = flexbuffer_cache::parse_buffer( "{}" );
        root = flexbuffer_root_from_storage( flexbuffer->get_storage() );
    }

    zip = std::shared_ptr<zzip>( new zzip( path, std::move( file ), JsonValue( std::move( flexbuffer ),
                                           root, nullptr, 0 ) ) );

    ZSTD_CCtx *cctx;
    ZSTD_DCtx *dctx;
    if( dictionary_path.empty() ) {
        cctx = ZSTD_createCCtx();
        dctx = ZSTD_createDCtx();
    } else if( auto it = cached_contexts.find( dictionary_path ); it != cached_contexts.end() ) {
        cctx = it->second.cctx;
        dctx = it->second.dctx;
    } else {
        std::shared_ptr<const mmap_file> dictionary = mmap_file::map_file( dictionary_path );
        cctx = ZSTD_createCCtx();
        ZSTD_CCtx_loadDictionary( cctx, dictionary->base(), dictionary->len() );
        ZSTD_CCtx_setParameter( cctx, ZSTD_c_compressionLevel, 7 );
        dctx = ZSTD_createDCtx();
        ZSTD_DCtx_loadDictionary( dctx, dictionary->base(), dictionary->len() );
        cached_contexts.emplace( dictionary_path, cached_zstd_context{ cctx, dctx } );
    }
    zip->ctx = std::make_unique<zzip::context>( cctx, dctx );

    return zip;
}

namespace
{

struct file_params {
    size_t offset = 0;
    size_t len = 0;
};

std::optional<file_params> get_file_params( const fs::path &path, const JsonObject &footer )
{
    std::string path_str = path.generic_u8string();
    if( !footer.has_object( "entries" ) ) {
        return std::nullopt;
    }

    JsonObject entries = footer.get_object( "entries" );
    entries.allow_omitted_members();
    if( !entries.has_object( path_str ) ) {
        return std::nullopt;
    }

    file_params fparams;
    JsonObject entry = entries.get_object( path_str );
    entry.allow_omitted_members();
    fparams.offset = entry.get_int( "offset" );
    fparams.len = entry.get_int( "len" );
    return fparams;
}

struct zzip_meta {
    size_t content_end = 0;
};

std::optional<zzip_meta> get_meta( const JsonObject &footer )
{
    if( !footer.has_object( "meta" ) ) {
        return std::nullopt;
    }

    JsonObject meta = footer.get_object( "meta" );
    if( !meta.has_int( "content_end" ) ) {
        return std::nullopt;
    }

    zzip_meta zmeta;
    zmeta.content_end = meta.get_int( "content_end" );
    return zmeta;
}

}

size_t zzip::get_file_size( const fs::path &zzip_relative_path ) const
{
    std::optional<file_params> fparams = get_file_params( zzip_relative_path, footer );
    if( !fparams.has_value() ) {
        throw std::runtime_error( "whups" );
    }
    void *base = static_cast<char *>( file->base() ) + fparams->offset;
    return ZSTD_decompressBound( base, fparams->len );
}

bool zzip::has_file( const fs::path &zzip_relative_path ) const
{
    std::optional<file_params> fparams = get_file_params( zzip_relative_path, footer );
    return fparams.has_value();
}

std::vector<std::byte> zzip::get_file( const fs::path &zzip_relative_path ) const
{
    std::optional<file_params> fparams = get_file_params( zzip_relative_path, footer );
    if( !fparams.has_value() ) {
        throw std::runtime_error( "whups" );
    }
    void *base = static_cast<char *>( file->base() ) + fparams->offset;
    unsigned long long file_size = ZSTD_decompressBound( base, fparams->len );
    std::vector<std::byte> buf{ file_size };
    size_t actual = ZSTD_decompressDCtx( ctx->dctx, buf.data(), file_size, base, fparams->len );
    buf.resize( actual );
    return buf;
}

size_t zzip::get_file_to( const fs::path &zzip_relative_path, std::byte *dest,
                          size_t dest_len ) const
{
    std::optional<file_params> fparams = get_file_params( zzip_relative_path, footer );
    if( !fparams.has_value() ) {
        throw std::runtime_error( "whups" );
    }
    void *base = static_cast<char *>( file->base() ) + fparams->offset;
    unsigned long long file_size = ZSTD_decompressBound( base, fparams->len );
    if( dest_len < file_size ) {
        return 0;
    }
    size_t actual = ZSTD_decompressDCtx( ctx->dctx, dest, dest_len, base, fparams->len );
    return actual;
}

bool zzip::add_file( const fs::path &zzip_relative_path, std::string_view content )
{
    size_t estimated_size = ZSTD_compressBound( content.length() );

    std::error_code ec;

    std::optional<zzip_meta> meta_opt = get_meta( footer );
    size_t old_content_end = 0;
    if( meta_opt.has_value() ) {
        old_content_end = meta_opt->content_end;
    }

    JsonObject footer_copy;
    if( !file ) {
        // Can't mmap an empty file
        fs::resize_file( path, estimated_size, ec );
        if( ec ) {
            return false;
        }
        file = mmap_file::map_writeable_file( path );
        footer_copy = footer;
        footer_copy.allow_omitted_members();
    } else {
        size_t footer_size = file->len() - old_content_end;
        std::vector<std::byte> footer_buf;
        footer_buf.resize( footer_size );
        memcpy( footer_buf.data(), static_cast<char *>( file->base() ) + old_content_end, footer_size );
        std::shared_ptr<flexbuffer_storage> footer_storage = std::make_shared<zzip_vector_storage>
                ( std::move( footer_buf ) );
        flexbuffers::Reference root = flexbuffer_root_from_storage( footer_storage );
        std::shared_ptr<parsed_flexbuffer> footer_flexbuffer = std::make_shared<zzip_flexbuffer>( std::move(
                    footer_storage ) );
        footer_copy = JsonValue( std::move( footer_flexbuffer ), root, nullptr, 0 );
        footer_copy.allow_omitted_members();

        if( !file->resize_file( old_content_end + estimated_size ) ) {
            return false;
        }
    }
    size_t actual_size = ZSTD_compress2( ctx->cctx,
                                         static_cast<char *>( file->base() ) + old_content_end, estimated_size,
                                         content.data(), content.size() );
    if( ZSTD_isError( actual_size ) ) {
        return false;
    }

    flexbuffers::Builder builder;
    size_t root_start = builder.StartMap();
    {
        size_t meta_start = builder.StartMap( "meta" );
        builder.UInt( "content_end", old_content_end + actual_size );
        // TODO more meta
        builder.EndMap( meta_start );
    }
    {
        std::string new_filename = zzip_relative_path.generic_u8string();
        JsonObject entries = footer_copy.get_object( "entries" );
        size_t entries_start = builder.StartMap( "entries" );
        {
            size_t new_entry_map = builder.StartMap( new_filename.c_str() );
            builder.UInt( "offset", old_content_end );
            builder.UInt( "len", actual_size );
            builder.EndMap( new_entry_map );
        }
        for( JsonMember entry : entries ) {
            std::string old_filename = entry.name();
            if( old_filename != new_filename ) {
                size_t entry_map = builder.StartMap( old_filename.c_str() );
                JsonObject old_data = entry.get_object();
                builder.UInt( "offset", old_data.get_int( "offset" ) );
                builder.UInt( "len", old_data.get_int( "len" ) );
                builder.EndMap( entry_map );
            }
        }
        builder.EndMap( entries_start );
    }
    builder.EndMap( root_start );
    builder.Finish();
    auto buf = builder.GetBuffer();

    if( !file->resize_file( old_content_end + actual_size + buf.size() ) ) {
        return false;
    }
    memcpy( static_cast<char *>( file->base() ) + old_content_end + actual_size, buf.data(),
            buf.size() );

    std::shared_ptr<flexbuffer_storage> new_storage = std::make_shared<zzip_flexbuffer_storage>( file );
    flexbuffers::Reference new_root = flexbuffer_root_from_storage( new_storage );
    std::shared_ptr<parsed_flexbuffer> new_flexbuffer = std::make_shared<zzip_flexbuffer>( std::move(
                new_storage ) );
    footer = JsonValue( std::move( new_flexbuffer ), new_root, nullptr, 0 );
    return true;
}

std::shared_ptr<zzip> zzip::create_from_folder( const fs::path &path, const fs::path &folder,
        const fs::path &dictionary )
{
    std::shared_ptr<zzip> zip = zzip::load( path, dictionary );
    for( const fs::directory_entry &entry : fs::recursive_directory_iterator( folder ) ) {
        if( !entry.is_regular_file() ) {
            continue;
        }
        std::shared_ptr<const mmap_file> file = mmap_file::map_file( entry.path() );
        zip->add_file( fs::relative( entry.path(), folder ).generic_u8string(), std::string_view{static_cast<const char *>( file->base() ), file->len()} );
    }
    return zip;
}

bool zzip::extract_to_folder( const fs::path &path, const fs::path &folder,
                              const fs::path &dictionary )
{
    std::shared_ptr<zzip> zip = zzip::load( path, dictionary );

    for( const JsonMember &entry : zip->footer.get_object( "entries" ) ) {
        std::string filename = entry.name();
        size_t len = zip->get_file_size( filename );
        fs::path destination_file_path = folder / filename;
        if( !assure_dir_exist( destination_file_path.parent_path() ) ) {
            return false;
        }
        std::shared_ptr<mmap_file> file = mmap_file::map_writeable_file( destination_file_path );
        if( !file ) {
            fs::resize_file( destination_file_path, len );
            file = mmap_file::map_writeable_file( destination_file_path );
        }
        if( !file ) {
            return false;
        }
        if( zip->get_file_to( filename, static_cast<std::byte *>( file->base() ), file->len() ) != len ) {
            return false;
        }
    }
    return true;
}

std::vector<zzip::entry> zzip::entries() const
{
    std::vector<entry> entries;
    JsonObject json_entries = footer.get_object( "entries" );
    entries.reserve( json_entries.size() );

    for( const JsonMember &entry : footer.get_object( "entries" ) ) {
        entries.emplace_back( fs::path( entry.name() ), entry.get_object().get_int( "len" ) );
    }
    return entries;
}
