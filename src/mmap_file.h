#pragma once
#ifndef CATA_SRC_MMAP_FILE_H
#define CATA_SRC_MMAP_FILE_H

#include <memory>
#include <string>

#include <ghc/fs_std_fwd.hpp>

// Consider https://github.com/decodeless/mappedfile instead of this.

class mmap_file
{
    public:
        static std::shared_ptr<const mmap_file> map_file( const fs::path &file_path );

        static std::shared_ptr<mmap_file> map_writeable_file( const fs::path &file_path );

        bool resize_file( size_t desired_size );

        ~mmap_file();

        uint8_t *base();
        uint8_t const *base() const;

        size_t len() const;

    private:
        mmap_file();

        static std::shared_ptr<mmap_file> map_file_generic( const fs::path &file_path, int flags );

        // Opaque type to platform specific mmap implementation which can clean up the view when destructed.
        struct handle;
        std::shared_ptr<handle> mmap_handle;
        uint8_t *map_base = nullptr;
        size_t map_len = 0;
};

#endif // CATA_SRC_MMAP_FILE_H
