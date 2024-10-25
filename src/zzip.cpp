#include "zzip.h"

#include <memory>
#include <optional>
#include <vector>

#include <zstd/zdict.h>
#include <zstd/zstd.h>

#include "json_loader.h"

namespace
{
struct zzip_flexbuffer_storage : flexbuffer_storage {
    std::shared_ptr<mmap_file> mmap_handle_;

    explicit zzip_flexbuffer_storage( std::shared_ptr<mmap_file> mmap_handle ) : mmap_handle_{ std::move( mmap_handle ) } {}

    const uint8_t *data() const override {
        return mmap_handle_->base;
    }
    size_t size() const override {
        return mmap_handle_->len;
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

}
zzip::zzip( const fs::path &path, std::shared_ptr<mmap_file> file, JsonObject footer ) : path{path},
    file{std::move( file )},
    footer{ std::move( footer ) } {}
zzip::~zzip() noexcept {}

std::shared_ptr<zzip> zzip::load( const fs::path &path )
{
    std::shared_ptr<zzip> zip;
    std::shared_ptr<mmap_file> file = mmap_file::map_file( path );
    if( !file ) {
        return zip;
    }

    std::shared_ptr<zzip_flexbuffer_storage> storage = std::make_shared<zzip_flexbuffer_storage>
            ( file );
    flexbuffers::Reference root = flexbuffer_root_from_storage( storage );
    std::shared_ptr<parsed_flexbuffer> flexbuffer = std::make_shared<zzip_flexbuffer>( std::move(
                storage ) );

    zip = std::shared_ptr<zzip>( new zzip( path, std::move( file ), JsonValue( std::move( flexbuffer ),
                                           root, nullptr, 0 ) ) );
    return zip;
}

namespace
{

struct dictionary_params {
    size_t offset = 0;
    size_t len = 0;
};

std::optional<dictionary_params> get_dictionary_params( JsonObject footer )
{
    if( !footer.has_object( "dict" ) ) {
        return std::nullopt;
    }

    dictionary_params params;
    JsonObject dict = footer.get_object( "dict" );
    params.offset = dict.get_int( "offset" );
    params.len = dict.get_int( "len" );
    return params;
}

struct file_params {
    size_t offset = 0;
    size_t len = 0;
};

std::optional<file_params> get_file_params( const fs::path &path, JsonObject footer )
{
    std::string path_str = path.generic_u8string();
    if( !footer.has_object( "entries" ) ) {
        return std::nullopt;
    }

    JsonObject entries = footer.get_object( "entries" );
    if( !entries.has_object( path_str ) ) {
        return std::nullopt;
    }

    file_params fparams;
    JsonObject entry = entries.get_object( path_str );
    fparams.offset = entry.get_int( "offset" );
    fparams.len = entry.get_int( "len" );
    return fparams;
}

struct zzip_meta {
    size_t content_end = 0;
};

std::optional<zzip_meta> get_meta( JsonObject footer )
{
    if( !footer.has_object( "meta" ) ) {
        return std::nullopt;
    }

    JsonObject meta = meta.get_object( "meta" );
    if( !meta.has_int( "content_end" ) ) {
        return std::nullopt;
    }

    zzip_meta zmeta;
    zmeta.content_end = meta.get_int( "content_end" );
    return zmeta;
}

}

struct zzip::context {
    ~context() {
        ZSTD_freeCCtx( cctx );
        ZSTD_freeDCtx( dctx );
    }

    ZSTD_CCtx *cctx;
    ZSTD_DCtx *dctx;
};

void zzip::init_context()
{
    if( !ctx ) {
        ctx = std::make_unique<context>();
    }
}

void zzip::init_cctx()
{
    init_context();
    if( !ctx->cctx ) {
        ctx->cctx = ZSTD_createCCtx();
        std::optional<dictionary_params> dparams = get_dictionary_params( footer );
        if( dparams.has_value() ) {
            ZSTD_CCtx_loadDictionary_byReference( ctx->cctx, file->base + dparams->offset, dparams->len );
        }
        ZSTD_CCtx_setParameter( ctx->cctx, ZSTD_c_compressionLevel, 3 );
    }
}

void zzip::init_dctx()
{
    init_context();
    if( !ctx->dctx ) {
        ctx->dctx = ZSTD_createDCtx();
        std::optional<dictionary_params> dparams = get_dictionary_params( footer );
        if( dparams.has_value() ) {
            ZSTD_DCtx_loadDictionary_byReference( ctx->dctx, file->base + dparams->offset, dparams->len );
        }
    }
}

std::vector<std::byte> zzip::get_file( const fs::path &zzip_relative_path )
{
    init_dctx();
    std::optional<file_params> fparams = get_file_params( zzip_relative_path, footer );
    if( !fparams.has_value() ) {
        throw std::runtime_error( "whups" );
    }
    void *base = file->base + fparams->offset;
    unsigned long long file_size = ZSTD_decompressBound( base, fparams->len );
    std::vector<std::byte> buf{ file_size };
    size_t actual = ZSTD_decompressDCtx( ctx->dctx, buf.data(), file_size, base, fparams->len );
    buf.resize( actual );
    return buf;
}

bool zzip::add_file( const fs::path &zzip_relative_path, std::string_view content )
{
    std::optional<zzip_meta> meta_opt = get_meta( footer );
    size_t old_content_end = 0;
    if( meta_opt.has_value() ) {
        old_content_end = meta_opt->content_end;
    }

    size_t footer_size = file->len - old_content_end;
    std::vector<std::byte> footer_buf;
    footer_buf.resize( footer_size );
    memcpy( footer_buf.data(), file->base + old_content_end, footer_size );
    auto storage = std::make_shared<zzip_vector_storage>( std::move( footer_buf ) );
    flexbuffers::Reference root = flexbuffer_root_from_storage( storage );
    std::shared_ptr<parsed_flexbuffer> flexbuffer = std::make_shared<zzip_flexbuffer>( std::move(
                storage ) );
    JsonObject footer_copy = JsonValue( std::move( flexbuffer ), root, nullptr, 0 );

    init_cctx();
    size_t estimated_size = ZSTD_compressBound( content.length() );

    file.reset();
    fs::resize_file( path, old_content_end + estimated_size );
    file = mmap_file::map_file( path );
    if( !file ) {
        return false;
    }
    size_t actual_size = ZSTD_compress2( ctx->cctx, file->base + old_content_end, estimated_size,
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
        std::optional<dictionary_params> dparams = get_dictionary_params( footer_copy );
        if( dparams ) {
            size_t dict_start = builder.StartMap( "dict" );
            builder.UInt( "offset", dparams->offset );
            builder.UInt( "len", dparams->len );
            builder.EndMap( dict_start );
        }
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

    file.reset();
    fs::resize_file( path, old_content_end + actual_size + buf.size() );
    file = mmap_file::map_file( path );
    if( !file ) {
        return false;
    }
    memcpy( file->base + old_content_end + actual_size, buf.data(), buf.size() );

    auto new_storage = std::make_shared<zzip_flexbuffer_storage>( file );
    flexbuffers::Reference new_root = flexbuffer_root_from_storage( new_storage );
    std::shared_ptr<parsed_flexbuffer> new_flexbuffer = std::make_shared<zzip_flexbuffer>( std::move(
                storage ) );
    footer = JsonValue( std::move( new_flexbuffer ), new_root, nullptr, 0 );
    return true;
}

bool zzip::save()
{
    return false;
}
