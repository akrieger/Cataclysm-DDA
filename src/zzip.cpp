#include <memory>

#include "json_loader.h"
#include "zzip.h"

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
    zzip_flexbuffer( std::shared_ptr<zzip_flexbuffer_storage> storage ) : parsed_flexbuffer( std::move(
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
}

std::shared_ptr<zzip> zzip::load( const fs::path &path )
{
    std::shared_ptr<zzip> zip = nullptr;
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
