/*  
 *  unit test for the poolalloc library.
 *
 */

#include "firstinclude.h"
#include "test.h"
#include "poolalloc.h"
#include "error.h"

#include <stdio.h>

int test_file(FILE * fp, int argc, char ** argv) {
    struct poolalloc * pa;
    unsigned a;
    unsigned bulk_alloc = 1024;
    unsigned margin = 10;

    pa = poolalloc_new(0, bulk_alloc, NULL);
    for (a = 0; a < bulk_alloc + margin; a++)
        poolalloc_malloc(pa, 1);
    if (poolalloc_allocated(pa) != bulk_alloc + margin) {
        ERROR2("reports %u allocations though %u made", 
          poolalloc_allocated(pa), bulk_alloc + margin);
    }
    poolalloc_delete(pa);
    return 1;
}

