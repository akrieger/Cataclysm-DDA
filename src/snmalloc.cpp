#define SNMALLOC_USE_WAIT_ON_ADDRESS 1
#ifdef __ANDROID__
#define MALLOC_USABLE_SIZE_QUALIFIER const
#endif

#include "snmalloc/override/malloc.cc"
#include "snmalloc/override/new.cc"

#ifdef _WIN32
// To statically link snmalloc without errors or overrides we have to also provide _msize_base.
// See https://developercommunity.visualstudio.com/t/-msize-implementation/626773 for the 2019
// feature request which would address/ameliorate this.
// See https://github.com/microsoft/snmalloc/pull/775 for the snmalloc PR to avoid this.
size_t _msize_base( void *ptr ) noexcept
{
    return snmalloc::libc::malloc_usable_size( ptr );
}
#endif
