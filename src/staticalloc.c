/* staticalloc.c implements a simple allocator that provides access to a single,
 * chunk of memory, without attempting to portion it at all.
 *
 * written nml 2005-03-10
 *
 */

#include "firstinclude.h"

#include "staticalloc.h"

#include "def.h"
#include "mem.h"
#include "_mem.h"
#include "zvalgrind.h"
#include "zstdint.h"

#include <assert.h>
#include <stdlib.h>
#include <limits.h>

struct staticalloc {
    union {
        int size;                   /* size of available memory.  The sign 
                                     * indicates whether it is currently 
                                     * allocated or not - negative indicates 
                                     * allocated, positive indicates not */
        void *dummy;                /* here to ensure that staticalloc is one 
                                     * word in size, in a world of LP64 and
                                     * LLP64 stupidity */
    } u;
};

struct staticalloc *staticalloc_new(void *area, unsigned int size) {
    struct staticalloc *alloc = NULL;

    /* can't handle size of > INT_MAX because we use the sign bit to track
     * allocation.  Seems like a good tradeoff. */
    if ((size <= INT_MAX) && (size > sizeof(*alloc))) {
        alloc = area;
        alloc->u.size = ((int) size) - sizeof(*alloc);
        VALGRIND_MAKE_NOACCESS(MEM_PTRADD(alloc, sizeof(*alloc)), 
          alloc->u.size);
        assert(alloc->u.size);
    }

    return alloc;
}

void staticalloc_delete(struct staticalloc *alloc) {
    assert(alloc->u.size);
    VALGRIND_MAKE_WRITABLE(MEM_PTRADD(alloc, sizeof(*alloc)), alloc->u.size);
    alloc->u.size = 0;  /* zero out memory, so it can't be reused */
    return;
}

void *staticalloc_malloc(struct staticalloc *alloc, unsigned int size) {
    void *ptr;

    assert(alloc->u.size);

    if ((alloc->u.size > 0) && (((unsigned int) alloc->u.size) >= size)) {
        VALGRIND_MAKE_WRITABLE(MEM_PTRADD(alloc, sizeof(*alloc)), 
          alloc->u.size);
        alloc->u.size *= -1;  /* indicate that the chunk is allocated */
        ptr = MEM_PTRADD(alloc, sizeof(*alloc));
        VALGRIND_MALLOCLIKE_BLOCK(ptr, size, 0, 0);
        return ptr;
    } else {
        /* our memory chunk is currently out, please come again... */
        return NULL;
    }
}

void staticalloc_free(struct staticalloc *alloc, void *ptr) {
    assert(alloc->u.size);
    if (alloc->u.size < 0 && (ptr == MEM_PTRADD(alloc, sizeof(*alloc)))) {
        alloc->u.size *= -1;   /* indicate that the chunk is free */
        VALGRIND_FREELIKE_BLOCK(ptr, 0);
        VALGRIND_MAKE_NOACCESS(MEM_PTRADD(alloc, sizeof(*alloc)), 
          alloc->u.size);
    } else {
        assert(!CRASH);      /* user error, it's already free */
    }
}

unsigned int staticalloc_allocated(struct staticalloc *alloc) {
    assert(alloc->u.size);
    return (alloc->u.size < 0);
}

int staticalloc_is_managed(struct staticalloc *alloc, void *ptr) {
    assert(alloc->u.size);
    return (ptr == MEM_PTRADD(alloc, sizeof(*alloc)));
}

unsigned int staticalloc_overhead() {
    return sizeof(struct staticalloc);
}

#ifdef STATICALLOC_TEST

#define BYTES sizeof(int)

int main() {
    STATICALLOC_DECL(alloc, BYTES);
    void *ptr;
    int *intptr;

    ptr = staticalloc_malloc(alloc, BYTES);
    assert(ptr);                            /* allocated should have worked */
    assert(ptr == &alloc_stackspace[1]);    /* should give us address one word 
                                             * from the start of the space */
    assert(!staticalloc_malloc(alloc, 1));  /* shouldn't be able to allocated 
                                             * again */
    intptr = ptr;
    *intptr = 0;                            /* access it */

    staticalloc_free(alloc, ptr);

    /* invalid stuff */
#if 0
    *intptr = 0;                            /* access it when we don't own it */
    do {
        STATICALLOC_DECL(leak, 1);
        assert(staticalloc_malloc(leak, 1));
    } while (0);
#endif

    assert(staticalloc_malloc(alloc, 1) == ptr);
    assert(!staticalloc_malloc(alloc, 1));
    staticalloc_free(alloc, ptr);

    staticalloc_delete(alloc);

    return EXIT_SUCCESS;
}

#endif

