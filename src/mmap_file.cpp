#include "mmap_file.h"

#ifdef _WIN32

#include <vector>

#include "platform_win.h"

#else

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#endif

#include <ghc/fs_std_fwd.hpp>

#include "cata_scope_helpers.h"
#include "cata_utility.h"

mmap_file::mmap_file() = default;

mmap_file::~mmap_file()
{
#ifdef _WIN32
    if( base != nullptr ) {
        UnmapViewOfFile( base );
    }
#else
    if( base != nullptr ) {
        msync( base, len, MS_SYNC );
        munmap( base, len );
    }
#endif
}

struct mmap_file::handle {
    // No copying
    handle() = default;
    handle( const handle & ) = delete;
    handle &operator=( const handle & ) = delete;

    // We can implement these if we need them.
    handle( handle &&h ) = delete;
    handle &operator=( handle &&h ) = delete;

    // Only Windows requires after-the-fact cleanup.
#ifdef _WIN32
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
#endif
};

std::shared_ptr<mmap_file> mmap_file::map_file( const std::string &file_path )
{
    return map_file( fs::u8path( file_path ) );
}

namespace
{

#ifdef _WIN32
struct mapping {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE file_mapping = INVALID_HANDLE_VALUE;
    void *base = nullptr;
    size_t len = 0;
};

mapping win32_map_file( const fs::path &file_path, DWORD create_flags )
{
    mapping m;

    HANDLE file_handle = CreateFileW(
                             file_path.native().c_str(),
                             create_flags,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr,
                             create_flags & GENERIC_WRITE ? OPEN_ALWAYS : OPEN_EXISTING,
                             0,
                             nullptr
                         );
    if( file_handle == INVALID_HANDLE_VALUE ) {
        // Failed to open file.
        return m;
    }
    LARGE_INTEGER file_size{};
    if( !GetFileSizeEx( file_handle, &file_size ) ) {
        // Failed to get file size.
        return m;
    }
    HANDLE file_mapping_handle = CreateFileMappingW(
                                     file_handle,
                                     nullptr,
                                     create_flags & GENERIC_WRITE ? PAGE_READWRITE : PAGE_READONLY,
                                     0,
                                     0,
                                     nullptr
                                 );
    if( file_mapping_handle == nullptr ) {
        return m;
    }
    void *map_base = MapViewOfFile(
                         file_mapping_handle,
                         ( create_flags & GENERIC_WRITE ? FILE_MAP_WRITE : 0 ) | FILE_MAP_READ,
                         0,
                         0,
                         file_size.QuadPart
                     );
    if( map_base == nullptr ) {
        // Failed to mmap file.
        return m;
    }
    m.file = file_handle;
    m.file_mapping = file_mapping_handle;
    m.base = map_base;
    m.len = file_size.QuadPart;
    return m;
}
#else
struct mapping {
    void *base = nullptr;
    size_t len = 0;
};

void *posix_map_file( const fs::path &file_path, int flags )
{
    mapping m;

    const std::string &file_path_string = file_path.native();
    std::error_code ec;
    size_t file_size = fs::file_size( file_path, ec );
    if( ec ) {
        return m;
    }

    int fd = open( file_path_string.c_str(), ( flags == O_RDWR ? O_CREAT : 0 ) | flags );
    if( fd == -1 ) {
        return m;
    }
    on_out_of_scope close_file_guard( [&] { close( fd ); } );
    void *map_base = mmap( nullptr, file_size, ( flags == O_RDWR ? PROT_WRITE : 0 ) | PROT_READ,
                           MAP_PRIVATE, fd, 0 );
    if( !map_base ) {
        return m;
    }

    m.base = map_base;
    m.len = file_size;

    return m;
}
#endif

}

std::shared_ptr<mmap_file> mmap_file::map_file( const fs::path &file_path )
{
    std::shared_ptr<mmap_file> mapped_file;

#ifdef _WIN32
    mapping m = win32_map_file( file_path, GENERIC_READ );
    if( m.file == INVALID_HANDLE_VALUE || m.file_mapping == INVALID_HANDLE_VALUE ||
        m.base == nullptr ) {
        return mapped_file;
    }
    mapped_file = std::shared_ptr<mmap_file> { new mmap_file() };
    mapped_file->mmap_handle = std::make_shared<handle>();
    mapped_file->mmap_handle->file = m.file;
    mapped_file->mmap_handle->file_mapping = m.file_mapping;
    mapped_file->base = static_cast<uint8_t *>( m.base );
    mapped_file->len = m.len;
#else
    const std::string &file_path_string = file_path.native();
    std::error_code ec;
    size_t file_size = fs::file_size( file_path, ec );
    if( ec ) {
        return mapped_file;
    }

    int fd = open( file_path_string.c_str(), O_RDWR | O_CREAT );
    if( fd == -1 ) {
        return mapped_file;
    }
    on_out_of_scope close_file_guard( [&] { close( fd ); } );
    void *map_base = mmap( nullptr, file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0 );
    if( !map_base ) {
        return mapped_file;
    }

    mapped_file = std::shared_ptr<mmap_file> { new mmap_file() };
    // No need for an underlying handle.
    mapped_file->base = static_cast<uint8_t *>( map_base );
    mapped_file->len = file_size;
#endif
    return mapped_file;
}
