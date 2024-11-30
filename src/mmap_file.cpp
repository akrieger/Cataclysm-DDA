#include "mmap_file.h"

#ifdef _WIN32

#include <vector>

#include "debug.h"
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
    if( !FlushViewOfFile( map_base, 0 ) ) {
        debugmsg( std::to_string( GetLastError() ) );
    }
    if( !UnmapViewOfFile( map_base ) ) {
        debugmsg( std::to_string( GetLastError() ) );
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
            if( !CloseHandle( file_mapping ) ) {
                debugmsg( std::to_string( GetLastError() ) );
            }
        }
        if( file != INVALID_HANDLE_VALUE ) {
            if( !FlushFileBuffers( file ) ) {
                // Fails if file was open readonly
                // But honestly doesn't matter.
                // debugmsg( std::to_string( GetLastError() ) );
            }
            if( !CloseHandle( file ) ) {
                debugmsg( std::to_string( GetLastError() ) );
            }
        }
    }

    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE file_mapping = INVALID_HANDLE_VALUE;
#else
#endif
};

namespace
{

#ifdef _WIN32
struct win32_memory_mapping {
    HANDLE file_mapping = INVALID_HANDLE_VALUE;
    void *base = nullptr;
    size_t len = 0;
};

win32_memory_mapping win32_map_view_of_file( HANDLE file_handle, DWORD create_flags )
{
    win32_memory_mapping m{};

    LARGE_INTEGER file_size{};
    if( !GetFileSizeEx( file_handle, &file_size ) ) {
        // Failed to get file size.
        return m;
    }

    HANDLE file_mapping_handle = CreateFileMappingW(
                                     file_handle,
                                     nullptr,
                                     create_flags & GENERIC_WRITE ? PAGE_READWRITE : PAGE_READONLY,
                                     // Explicitly pass file size to increase backing file, if size is larger.
                                     file_size.HighPart,
                                     file_size.LowPart,
                                     nullptr
                                 );
    if( file_mapping_handle == INVALID_HANDLE_VALUE ) {
        return m;
    }
    on_out_of_scope close_file_mapping_guard( [&] { CloseHandle( file_mapping_handle ); } );

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
    close_file_mapping_guard.cancel();
    m.file_mapping = file_mapping_handle;
    m.base = map_base;
    m.len = file_size.QuadPart;
    return m;
}

std::pair<HANDLE, win32_memory_mapping> win32_map_file( const fs::path &file_path,
        DWORD create_flags )
{
    std::pair<HANDLE, win32_memory_mapping> m{ INVALID_HANDLE_VALUE, {} };

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
    on_out_of_scope close_file_guard( [&] { CloseHandle( file_handle ); } );

    m.second = win32_map_view_of_file( file_handle, create_flags );
    if( m.second.base == nullptr ) {
        return m;
    }

    m.first = file_handle;
    close_file_guard.cancel();
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
    int fd = open( file_path_string.c_str(), ( flags == O_RDWR ? O_CREAT : 0 ) | flags );
    if( fd == -1 ) {
        return m;
    }

    on_out_of_scope close_file_guard( [&] { close( fd ); } );

    std::error_code ec;
    size_t file_size = fs::file_size( file_path, ec );
    if( ec ) {
        return m;
    }

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

std::shared_ptr<mmap_file> mmap_file::map_file_generic( const fs::path &file_path, int flags )
{
    std::shared_ptr<mmap_file> mapped_file;

#ifdef _WIN32
    auto [file, memory_mapping] = win32_map_file( file_path, flags );
    if( memory_mapping.base == nullptr ) {
        return mapped_file;
    }
    mapped_file = std::shared_ptr<mmap_file> { new mmap_file() };
    mapped_file->mmap_handle = std::make_shared<handle>();
    mapped_file->mmap_handle->file = file;
    mapped_file->mmap_handle->file_mapping = memory_mapping.file_mapping;
    mapped_file->map_base = static_cast<uint8_t *>( memory_mapping.base );
    mapped_file->map_len = memory_mapping.len;
#else
    mapping m = posix_map_file( file_path, flags );
    if( m.base == nullptr ) {
        return mapped_file;
    }

    mapped_file = std::shared_ptr<const mmap_file> { new mmap_file() };
    // No need for an underlying handle.
    mapped_file->map_base = static_cast<uint8_t *>( m.base );
    mapped_file->map_len = m.len;
#endif
    return mapped_file;
}

std::shared_ptr<const mmap_file> mmap_file::map_file( const fs::path &file_path )
{
    std::shared_ptr<const mmap_file> mapped_file;

#ifdef _WIN32
    mapped_file = map_file_generic( file_path, GENERIC_READ );
#else
    mapped_file = map_file_generic( file_path, O_RDONLY );
#endif
    return mapped_file;
}

std::shared_ptr<mmap_file> mmap_file::map_writeable_file( const fs::path &file_path )
{
    std::shared_ptr<mmap_file> mapped_file;

#ifdef _WIN32
    mapped_file = map_file_generic( file_path, GENERIC_READ | GENERIC_WRITE );
#else
    mapped_file = map_file_generic( file_path, O_RDRW );
#endif
    return mapped_file;
}

bool mmap_file::resize_file( size_t desired_size )
{
    if( desired_size == map_len ) {
        return true;
    }
#ifdef _WIN32
    if( !FlushViewOfFile( map_base, 0 ) ) {
        debugmsg( std::to_string( GetLastError() ) );
        return false;
    }
    if( !UnmapViewOfFile( map_base ) ) {
        debugmsg( std::to_string( GetLastError() ) );
        return false;
    }
    if( !CloseHandle( mmap_handle->file_mapping ) ) {
        mmap_handle->file_mapping = INVALID_HANDLE_VALUE;
        debugmsg( std::to_string( GetLastError() ) );
        return false;
    }
    if( !FlushFileBuffers( mmap_handle->file ) ) {
        debugmsg( std::to_string( GetLastError() ) );
        return false;
    }
    LARGE_INTEGER file_size;
    file_size.QuadPart = desired_size;
    if( !SetFilePointerEx( mmap_handle->file, file_size, NULL, FILE_BEGIN ) ) {
        debugmsg( std::to_string( GetLastError() ) );
        return false;
    }
    if( !SetEndOfFile( mmap_handle->file ) ) {
        debugmsg( std::to_string( GetLastError() ) );
        return false;
    }

    win32_memory_mapping m = win32_map_view_of_file( mmap_handle->file, GENERIC_READ | GENERIC_WRITE );
    if( m.base == nullptr ) {
        return false;
    }

    mmap_handle->file_mapping = m.file_mapping;
    map_base = reinterpret_cast<uint8_t *>( m.base );
    map_len = m.len;
#else

#endif
    return true;
}

uint8_t *mmap_file::base()
{
    return map_base;
}

uint8_t const *mmap_file::base() const
{
    return map_base;
}

size_t mmap_file::len() const
{
    return map_len;
}
