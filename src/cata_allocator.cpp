#include "cata_allocator.h"

#ifndef __has_feature
#define __has_feature(x) 0
#endif

//#define CATA_USE_MIMALLOC

// snmalloc isn't compatible with any sanitizers.
#if !defined CATA_USE_MIMALLOC && !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)
#define CATA_USE_SNMALLOC
#endif

#ifdef CATA_USE_SNMALLOC
// Disable asserts and slower debug code in the allocator.
#ifndef NDEBUG
#define NDEBUG
#endif
#define SNMALLOC_USE_WAIT_ON_ADDRESS 1
#ifdef __ANDROID__
#define MALLOC_USABLE_SIZE_QUALIFIER const
#endif
// Static redirection of malloc doesn't work on Windows.
#ifndef _WIN32
#include <snmalloc/override/malloc.cc> // NOLINT(bugprone-suspicious-include)
#endif
#include <snmalloc/override/new.cc> // NOLINT(bugprone-suspicious-include)
#endif

#ifdef __SANITIZE_ADDRESS__
#define MI_TRACK_ASAN ON
#endif

// mimalloc internal asserts by default check NDEBUG which we don't set.
#define MI_DEBUG 0

#ifdef CATA_USE_MIMALLOC
#define SDL_SET_MEMORY_FUNCTIONS

#include <mimalloc/static.c> // NOLINT(bugprone-suspicious-include)
#include <mimalloc/mimalloc-new-delete.h> // IWYU pragma: keep

#endif

#if defined(TILES) || defined(SDL_SOUND)
#define SDL_SET_MEMORY_FUNCTIONS
#if defined(_MSC_VER) && defined(USE_VCPKG)
#include <SDL2/SDL_stdinc.h>
#else
#include <SDL_stdinc.h>
#endif
#endif

void cata::init_allocator()
{
#ifdef SDL_SET_MEMORY_FUNCTIONS
#ifdef CATA_USE_SNMALLOC
    SDL_SetMemoryFunctions(
        snmalloc::libc::malloc,
        snmalloc::libc::calloc,
        snmalloc::libc::realloc,
        snmalloc::libc::free );
#else
    SDL_SetMemoryFunctions(
        mi_new,
        mi_calloc,
        mi_realloc,
        mi_free );
#endif
#endif
}
