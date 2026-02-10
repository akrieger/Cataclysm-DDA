#include "cata_allocator.h"
#include "options.h"

struct Alloc {
  Alloc() { }
};

Alloc a;

int main()
{
    get_options().init();
    return 0;
}
