// This is a dda-specifc define in our build scripts, not a general sanitizer test.
#ifndef SANITIZE
#define SNMALLOC_USE_WAIT_ON_ADDRESS 1
#ifdef __ANDROID__
#define MALLOC_USABLE_SIZE_QUALIFIER const
#endif
#ifdef _WIN32
#include <snmalloc/windows/override/detours.cc> // NOLINT(bugprone-suspicious-include)
#else
#include <snmalloc/override/malloc.cc> // NOLINT(bugprone-suspicious-include)
#endif
#include <snmalloc/override/new.cc> // NOLINT(bugprone-suspicious-include)
#endif
