#pragma once
#ifndef CATA_SRC_FLEXBUFFER_CACHE_H
#define CATA_SRC_FLEXBUFFER_CACHE_H

#include <unordered_map>
#include <memory>

#include "flatbuffers/flexbuffers.h"

namespace flexbuffers
{
class Reference;
} // namespace flexbuffers

class FlexBufferCache
{
    public:
        static FlexBufferCache &global_cache();
        ~FlexBufferCache() = default;

        using FlexBuffer = flexbuffers::Reference;

        // Throw exceptions on IO and parse errors.
        std::shared_ptr<FlexBuffer> parse_through( std::string json_file_path ) noexcept( false );
        size_t drop_cache() noexcept;
        std::shared_ptr<FlexBuffer> parse( const std::string &json_file_path ) noexcept( false );
        std::shared_ptr<FlexBuffer> parse_and_cache( std::string json_file_path ) noexcept( false ) ;

    private:
        FlexBufferCache() = default;

        struct ParsedBuffer;

        std::shared_ptr<ParsedBuffer> parse_buffer_( const char *buffer );
        void cache_file_( std::string &&key,
                          const std::shared_ptr<ParsedBuffer> &entry );
        std::shared_ptr<ParsedBuffer> find_buffer_( const std::string &key );
        std::shared_ptr<ParsedBuffer> load_from_disk_cache_( const std::string &json_file_path,
                const std::string &relative_path, time_t source_mtime );
        std::shared_ptr<ParsedBuffer> get_from_cache_if_appropriate_( const std::string &json_file_path,
                const std::string &relative_path );

        size_t cached_buffer_sizes_ = 0;
        size_t input_sizes_ = 0;
        // Map of original json file path to cached parsed FlexBuffer
        std::unordered_map<std::string, std::shared_ptr<ParsedBuffer>> cached_buffers_;
        // Map of original json file path to disk serialized FlexBuffer path and mtime of input.
        struct DiskCacheEntry;
        std::unordered_map<std::string, DiskCacheEntry> filesystem_cache_;
};

#endif // CATA_SRC_FLEXBUFFER_JSON_H
