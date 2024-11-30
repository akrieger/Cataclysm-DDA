#pragma once
#ifndef CATA_SRC_MMAP_FILE_H
#define CATA_SRC_MMAP_FILE_H

#include <memory>
#include <string>

#include <ghc/fs_std_fwd.hpp>

class mmap_file
{
    public:
        static std::shared_ptr<const mmap_file> map_file( const fs::path &file_path );

        ~mmap_file();

        uint8_t *base();
        uint8_t const *base() const;

        size_t len() const;

    private:
        mmap_file();

        // Opaque type to platform specific mmap implementation which can clean up the view when destructed.
        struct handle;
        std::shared_ptr<handle> mmap_handle;
        uint8_t *map_base = nullptr;
        size_t map_len = 0;
};

#endif // CATA_SRC_MMAP_FILE_H
