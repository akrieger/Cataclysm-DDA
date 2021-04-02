#include "mmap_file.h"

#ifdef _WIN32

#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#else

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#endif

mmap_file::mmap_file() {}

mmap_file::~mmap_file()
{
#ifdef _WIN32
    if( base != nullptr ) {
        UnmapViewOfFile( base );
    }
#else
#endif
}

struct mmap_file::handle {
    // No copying
    handle() = default;
    handle( handle const & ) = delete;
    handle &operator=( handle const & ) = delete;
#ifdef _WIN32
    handle( handle &&h ) {
        swap( h );
    }
    handle &operator=( handle &&h ) {
        swap( h );
        return *this;
    }
    void swap( handle &h ) {
        using std::swap;
        swap( file_mapping, h.file_mapping );
        swap( file, h.file );
    }

    ~handle() {
        if( file_mapping != INVALID_HANDLE_VALUE ) {
            CloseHandle( file_mapping );
        }
        if( file != INVALID_HANDLE_VALUE ) {
            CloseHandle( file );
        }
    }

    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE file_mapping = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif
};

std::shared_ptr<mmap_file> mmap_file::map_file( const std::string &file_path )
{
    std::shared_ptr<mmap_file> mapped_file;

#ifdef _WIN32
    size_t wide_path_len = MultiByteToWideChar(
                               CP_UTF8,
                               0,
                               file_path.c_str(),
                               -1,
                               NULL,
                               0
                           );
    if( wide_path_len == 0 ) {
        // Error expanding path
        return mapped_file;
    }
    std::vector<wchar_t> wide_path = std::vector<wchar_t>( wide_path_len, L'\0' );
    wide_path_len = MultiByteToWideChar(
                        CP_UTF8,
                        0,
                        file_path.c_str(),
                        -1,
                        wide_path.data(),
                        wide_path_len
                    );
    HANDLE file_handle = CreateFileW(
                             wide_path.data(),
                             GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_DELETE,
                             NULL,
                             OPEN_EXISTING,
                             0,
                             NULL
                         );
    if( file_handle == INVALID_HANDLE_VALUE ) {
        // Failed to open file.
        return mapped_file;
    }
    LARGE_INTEGER file_size{};
    if( !GetFileSizeEx( file_handle, &file_size ) ) {
        // Failed to get file size.
        return mapped_file;
    }
    HANDLE file_mapping_handle = CreateFileMappingW(
                                     file_handle,
                                     NULL,
                                     PAGE_READONLY,
                                     0,
                                     0,
                                     NULL
                                 );
    if( file_mapping_handle == NULL ) {
        return mapped_file;
    }
    void *map_base = MapViewOfFile(
                         file_mapping_handle,
                         FILE_MAP_READ,
                         0,
                         0,
                         file_size.QuadPart
                     );
    if( map_base == nullptr ) {
        // Failed to mmap file.
        return mapped_file;
    }
    mapped_file = std::shared_ptr<mmap_file> { new mmap_file() };
    mapped_file->mmap_handle = std::make_shared<handle>();
    mapped_file->mmap_handle->file = file_handle;
    mapped_file->mmap_handle->file_mapping = file_mapping_handle;
    mapped_file->base = static_cast<uint8_t *>( map_base );
    mapped_file->len = file_size.QuadPart;
#else

#endif
    return mapped_file;
}
