/* alloc.c implements wrappers around system malloc and free.
 *
 * written nml 2005-03-10
 *
 */

#include "firstinclude.h"

#include "alloc.h"

#include <stdlib.h>

void *alloc_malloc(void *opaque, unsigned int size) {
    return malloc(size);
}

void alloc_free(void *opaque, void *ptr) {
    free(ptr);
    return;
}

const struct alloc alloc_system = {NULL, alloc_malloc, alloc_free};

