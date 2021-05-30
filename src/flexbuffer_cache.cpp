#include "flexbuffer_cache.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>

#include <sys/stat.h>

#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/flexbuffers.h"
#include "flatbuffers/idl.h"
#include "flatbuffers/util.h"

#include "filesystem.h"
#include "json.h"
#include "make_static.h"
#include "mmap_file.h"
#include "path_info.h"

struct FlexBufferStorage {
    virtual ~FlexBufferStorage() {}
    virtual const uint8_t *data() const = 0;
    virtual size_t len() const = 0;
};

struct FlexBufferBuilderStorage : FlexBufferStorage {
    flexbuffers::Builder fbb{ 256, flexbuffers::BUILDER_FLAG_SHARE_ALL };

    const uint8_t *data() const override {
        return fbb.GetBuffer().data();
    }

    size_t len() const override {
        return fbb.GetSize();
    }
};

struct FlexBufferMmapStorage : FlexBufferStorage {
    std::shared_ptr<mmap_file> mmap_handle;

    const uint8_t *data() const override {
        return mmap_handle->base;
    }
    size_t len() const override {
        return mmap_handle->len;
    }
};

struct FlexBufferCache::ParsedBuffer {
    FlexBufferCache::FlexBuffer buffer;
    time_t source_mtime;
    std::shared_ptr<const FlexBufferStorage> storage;
};

struct FlexBufferCache::DiskCacheEntry {
    std::string flexbufferPath;
    uint64_t mtime;
};

namespace
{
std::string get_base_relative_path( const std::string &file )
{
    static std::string abs_base_path;
    static std::once_flag once;
    std::call_once( once, [] {
        std::string base_path = PATH_INFO::base_path();
        if( base_path.empty() )
        {
            base_path = ".";
        }
        abs_base_path = abs_path( base_path );

#ifdef _WIN32
        std::transform( abs_base_path.begin(), abs_base_path.end(), abs_base_path.begin(), []( char c )
        {
            if( c == '\\' ) {
                return '/';
            }
            return c;
        } );
        if( abs_base_path.back() != '/' )
        {
            abs_base_path.push_back( '/' );
        }
#endif
    } );

    std::string source_abs_path = abs_path( file );
#ifdef _WIN32
    std::transform( source_abs_path.begin(), source_abs_path.end(),
    source_abs_path.begin(), []( char c ) {
        if( c == '\\' ) {
            return '/';
        }
        return c;
    } );
#endif

    std::string source_rel_path;
    if( abs_base_path.compare( 0, abs_base_path.length(), source_abs_path.c_str(),
                               abs_base_path.length() ) == 0 ) {
        source_rel_path = source_abs_path.substr( abs_base_path.length() );
    }
    return source_rel_path;
}
}

FlexBufferCache &FlexBufferCache::global_cache()
{
    static FlexBufferCache cache = [] {
        FlexBufferCache cache;
        std::string flexbuffers_root_path = abs_path( PATH_INFO::cache_dir() + "/flexbuffers" );
        std::vector<std::string> flexbuffer_cache = get_files_from_path(
            ".fb",
            PATH_INFO::cache_dir() + "/flexbuffers",
            true,
            true );
        for( const std::string &flexbuffer : flexbuffer_cache )
        {
            // The file path format is <input file>.<mtime>.fb
            // So find the second-to-last-dot
            size_t end_of_base_file_path = flexbuffer.find_last_of( '.', flexbuffer.length() - 4 );
            if( end_of_base_file_path == std::string::npos ) {
                // Not properly formatted, ditch it.
                remove_file( flexbuffer );
                continue;
            }
            std::string base_file_path = flexbuffer.substr( 0, end_of_base_file_path );
            size_t cached_mtime = std::strtoull( flexbuffer.c_str() + end_of_base_file_path + 1, nullptr, 0 );

            // Don't just blindly insert, we may end up in a situation with multiple flexbuffers for the same input json
            auto old_entry = cache.filesystem_cache_.find( base_file_path );
            if( old_entry != cache.filesystem_cache_.end() && old_entry->second.mtime < cached_mtime ) {
                // Lets only keep the newest.
                remove_file( old_entry->second.flexbufferPath );
                old_entry->second = DiskCacheEntry{ flexbuffer, cached_mtime };
            } else {
                cache.filesystem_cache_.emplace( base_file_path, DiskCacheEntry{ flexbuffer, cached_mtime } );
            }
        }
        return cache;
    }( );
    return cache;
}

std::shared_ptr<FlexBufferCache::FlexBuffer> FlexBufferCache::parse( std::string const
        &json_source_path )
{
    auto json_source = mmap_file::map_file( json_source_path );
    if( !json_source ) {
        throw std::runtime_error( "Failed to mmap " + json_source_path );
    }

    auto entry = parse_buffer_( reinterpret_cast<const char *>( json_source->base ) );
    if( !entry ) {
        throw JsonError( "Failed to parse " + json_source_path + " into a flexbuffer" );
    }

    // Aliasing constructor to hide the private bits of the cache entry.
    return std::shared_ptr<FlexBufferCache::FlexBuffer>( entry, &entry->buffer );
}

std::shared_ptr<FlexBufferCache::FlexBuffer> FlexBufferCache::parse_through(
    std::string json_source_path ) noexcept( false )
{
    // Is our cache potentially stale?
    std::string rel_path = get_base_relative_path( json_source_path );
    if( rel_path.empty() ) {
        throw std::runtime_error( "Could not relativize path for " + json_source_path );
    }
    \
    std::shared_ptr<FlexBufferCache::ParsedBuffer> parsed_buffer = get_from_cache_if_appropriate_
            ( json_source_path, rel_path );
    if( parsed_buffer ) {
        return std::shared_ptr<FlexBufferCache::FlexBuffer>( parsed_buffer, &parsed_buffer->buffer );
    }

    auto json_source = mmap_file::map_file( json_source_path );
    if( !json_source ) {
        throw std::runtime_error( "Failed to mmap " + json_source_path );
    }

    parsed_buffer = parse_buffer_( reinterpret_cast<const char *>( json_source->base ) );
    if( !parsed_buffer ) {
        throw JsonError( "Failed to parse " + json_source_path + " into a flexbuffer" );
    }
    input_sizes_ += json_source->len;
    cache_file_( std::move( json_source_path ), parsed_buffer );

    // Aliasing constructor to hide the private bits of the cache entry.
    return std::shared_ptr<FlexBufferCache::FlexBuffer>( parsed_buffer, &parsed_buffer->buffer );
}

size_t FlexBufferCache::drop_cache() noexcept
{
    size_t cache_count = cached_buffers_.size();
    cached_buffers_.clear();
    return cache_count;
}

std::shared_ptr<FlexBufferCache::FlexBuffer> FlexBufferCache::parse_and_cache(
    std::string json_source_path )
{
    return parse_through( json_source_path );
}

std::shared_ptr<FlexBufferCache::FlexBuffer> FlexBufferCache::parse_buffer(
    std::string const& json)
{
    auto fb = parse_buffer_(json.c_str());
    std::shared_ptr<FlexBufferCache::FlexBuffer>(fb, &fb->buffer)
}

std::shared_ptr<FlexBufferCache::ParsedBuffer> FlexBufferCache::parse_buffer_( const char *buffer )
{
    flatbuffers::IDLOptions opts;
    opts.use_flexbuffers = true;
    flatbuffers::Parser parser{ opts };
    auto cache_entry = std::make_shared<FlexBufferCache::ParsedBuffer>();
    auto storage = std::make_shared<FlexBufferBuilderStorage>();
    cache_entry->storage = storage;
    if( !parser.ParseFlexBuffer( buffer, nullptr, &storage->fbb ) ) {
        return nullptr;
    }
    cache_entry->buffer = flexbuffers::GetRoot( storage->fbb.GetBuffer() );
    return cache_entry;
}

void FlexBufferCache::cache_file_( std::string &&file,
                                   const std::shared_ptr<FlexBufferCache::ParsedBuffer> &entry )
{
    struct stat st {};
    if( stat( file.c_str(), &st ) || !( ( st.st_mode & S_IFREG ) &&
                                        ( st.st_mode & S_IREAD ) ) ) {
        return;
    }

    std::string rel_path = get_base_relative_path( file );
    if( rel_path.empty() ) {
        throw std::exception( ( "Could not relativize " + file ).c_str() );
    }
    std::string fb_file = PATH_INFO::cache_dir() + "/flexbuffers/" + rel_path + "." +
                          std::to_string(
                              st.st_mtime ) + ".fb";

    flatbuffers::EnsureDirExists( flatbuffers::StripFileName( fb_file ) );
    std::ofstream fb( fb_file.c_str(), std::ofstream::binary );
    if( fb.good() ) {
        fb.write( reinterpret_cast<const char *>( entry->storage->data() ), entry->storage->len() );
        fb.close();
    }
    cached_buffers_.emplace( std::move( rel_path ), entry );
}

std::shared_ptr<FlexBufferCache::ParsedBuffer> FlexBufferCache::load_from_disk_cache_
( const std::string &json_source_path, const std::string &relative_path, time_t source_mtime )
{
    std::shared_ptr<FlexBufferCache::ParsedBuffer> buffer{};

    std::string rel_path = get_base_relative_path( json_source_path );

    if( rel_path == "" ) {
        return buffer;
    }

    // Is there even a potential cached flexbuffer for this file.
    auto disk_entry = filesystem_cache_.find( PATH_INFO::cache_dir() + "/flexbuffers/" + rel_path );
    if( disk_entry == filesystem_cache_.end() ) {
        return buffer;
    }

    // Does the source file's mtime match what we cached previously
    if( source_mtime != disk_entry->second.mtime ) {
        // Cached flexbuffer on disk is out of date, remove it.
        remove_file( disk_entry->second.flexbufferPath );
        filesystem_cache_.erase( disk_entry );
        return buffer;
    }

    // Try to mmap the cached flexbuffer
    auto mmap_handle = mmap_file::map_file( disk_entry->second.flexbufferPath );
    if( !mmap_handle ) {
        return buffer;
    }

    buffer = std::make_shared<ParsedBuffer>();
    buffer->buffer = flexbuffers::GetRoot( mmap_handle->base, mmap_handle->len );
    auto storage = std::make_shared<FlexBufferMmapStorage>();
    storage->mmap_handle = mmap_handle;
    buffer->storage = storage;

    return buffer;
}

std::shared_ptr<FlexBufferCache::ParsedBuffer> FlexBufferCache::get_from_cache_if_appropriate_
( const std::string &json_source_path, const std::string &relative_path )
{
    std::shared_ptr<FlexBufferCache::ParsedBuffer> buffer;

    // Find out when the source file last changed.
    struct stat st {};
    if( stat( json_source_path.c_str(), &st ) || !( ( st.st_mode & S_IFREG ) &&
            ( st.st_mode & S_IREAD ) ) ) {
        // Can't stat it? Definitely shouldn't trust cache.
        cached_buffers_.erase( relative_path );
        return buffer;
    }
    time_t source_mtime = st.st_mtime;

    // Have we loaded this flexbuffer already?
    auto cached_entry = cached_buffers_.find( relative_path );
    if( cached_entry != cached_buffers_.end() ) {
        // Does the source file's mtime match what we cached previously?
        if( source_mtime == cached_entry->second->source_mtime ) {
            buffer = cached_entry->second;
            return buffer;
        } else {
            cached_buffers_.erase( cached_entry );
            cached_entry = cached_buffers_.end();
        }
    }

    // Do we have an appropriate filesystem cached flexbuffer?
    auto filesystem_cache_entry = filesystem_cache_.find( relative_path );
    if( filesystem_cache_entry != filesystem_cache_.end() &&
        filesystem_cache_entry->second.mtime != source_mtime ) {
        // We do but it's out of date.
        remove_file( filesystem_cache_entry->second.flexbufferPath );
        filesystem_cache_.erase( filesystem_cache_entry );
        return buffer;
    }

    buffer = load_from_disk_cache_( json_source_path, relative_path, source_mtime );
    if( buffer ) {
        // Sweet, cache hit.
        cached_buffers_.emplace( std::string( relative_path ), buffer );
    }
    return buffer;
}
