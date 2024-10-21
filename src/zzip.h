#pragma once
#ifndef CATA_SRC_ZZIP_H
#define CATA_SRC_ZZIP_H

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
        static std::shared_ptr<zzip> load( const fs::path &path );

        bool save();

        fs::path path;
        std::shared_ptr<mmap_file> file;
        JsonObject footer;
};

#endif // CATA_SRC_ZZIP_H
