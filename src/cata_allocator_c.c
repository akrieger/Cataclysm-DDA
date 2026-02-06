#include "cata_allocator_c.h"

#ifdef _MSC_VER
// Statically overriding malloc() family seems to work on release,
// but not debug. It's brittle anyway.
#define ENABLE_OVERRIDE 0
#else
// Needs _GNU_SOURCE to get some of the defines it wants
#define _GNU_SOURCE
#endif

#include <rpmalloc/rpmalloc.c> // NOLINT(bugprone-suspicious-include)

alloc_funcs cata_get_alloc_funcs( void )
{
    alloc_funcs funcs = {
        rpmalloc,
        rpcalloc,
        rprealloc,
        rpfree,
    };
    return funcs;
}

