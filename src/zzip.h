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

        static std::shared_ptr<zzip> load( const fs::path &path );

        std::vector<std::byte> get_file( const fs::path &zzip_relative_path );
        bool add_file( const fs::path &zzip_relative_path, std::string_view content );

        bool save();

    private:
        fs::path path;
        std::shared_ptr<mmap_file> file;
        JsonObject footer;

        struct context;
        void init_context();
        void init_cctx();
        void init_dctx();
        std::unique_ptr<context> ctx;
};

#endif // CATA_SRC_ZZIP_H
