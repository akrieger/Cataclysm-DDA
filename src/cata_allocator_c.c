#include "cata_allocator_c.h"

#ifdef _MSC_VER
#define ENABLE_OVERRIDE 0
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

