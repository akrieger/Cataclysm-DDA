#pragma once
#ifndef CATA_SRC_ZZIP_H
#define CATA_SRC_ZZIP_H

#include <memory>
#include <string_view>
#include <vector>

#include <ghc/fs_std_fwd.hpp>

#include "flexbuffer_json.h"
#include "mmap_file.h"

// A 'zstandard zip'. A basic implementation of an archive consistenting of
// independently decompressable entries.
// The archive is implemented as concatenated zstd frames with a flexbuffer
// footer describing the contents.
class zzip
{
        zzip( const fs::path &path, std::shared_ptr<mmap_file> file, JsonObject footer );

    public:
        ~zzip() noexcept;

        static std::shared_ptr<zzip> load( const fs::path &path, const fs::path &dictionary = {} );

        size_t get_file_size( const fs::path &zzip_relative_path ) const;
        std::vector<std::byte> get_file( const fs::path &zzip_relative_path ) const;
        size_t get_file_to( const fs::path &zzip_relative_path, std::byte *dest, size_t dest_len ) const;

        bool add_file( const fs::path &zzip_relative_path, std::string_view content );

        static std::shared_ptr<zzip> create_from_folder( const fs::path &path, const fs::path &folder,
                const fs::path &dictionary = {} );
        static bool extract_to_folder( const fs::path &path, const fs::path &folder,
                                       const fs::path &dictionary = {} );

        struct entry {
            entry( fs::path relative_path, size_t length ) : relative_path{ std::move( relative_path ) }, length{ length } {}
            fs::path relative_path;
            size_t length;
        };
        std::vector<entry> entries() const;

    private:
        fs::path path;
        std::shared_ptr<mmap_file> file;
        JsonObject footer;

        struct context;
        std::unique_ptr<context> ctx;
};

#endif // CATA_SRC_ZZIP_H
