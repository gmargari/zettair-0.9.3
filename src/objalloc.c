/* objalloc.c implements an object that efficiently returns allocations of a 
 * set size.  Our implementation is fairly simple, in that we request memory
 * from the underlying allocation mechanism in chunks, which we arrange into a
 * singly-linked list.  We also maintain a singly-linked list of free objects,
 * which we manipulate as objects are allocated/freed.
 *
 * Naturally, the next step for this bit of code would be to *accept* an
 * allocator and free functions to be able to chain allocators.
 *
 * written nml 2004-08-30
 *
 */

#include "firstinclude.h"

#include "objalloc.h"

#include "alloc.h"
#include "def.h"
#include "mem.h"
#include "_mem.h"
#include "zvalgrind.h"

#include <assert.h>
#include <stdlib.h>

/* integer value we use to fill up space */
#define SPACE_FILL 0xdeadbeefU

#define FILL_BYTE(i) ((unsigned char) (SPACE_FILL >> (8 * (3 - ((i) % 4)))))

struct objalloc_chunk { 
    struct objalloc_chunk *next;     /* linked list of chunks */
    unsigned int size;               /* size of the rest of the chunk */
    /* more memory than this is allocated, and is managed below */
};

struct objalloc_alloc {
    struct objalloc_alloc *next;     /* linked list of alloc objects */
};

struct objalloc {
    unsigned int allocsize;          /* size of objects we're allocating */
    unsigned int redzone;            /* size of redzone we're putting after 
                                      * objects */
    unsigned int chunksize;          /* size of chunks we're requesting from 
                                      * the underlying allocation mechanism */
    unsigned int allocated;          /* how many objects are currently 
                                      * allocated */
    unsigned int reserved;           /* how many objects are currently on the 
                                      * next linked list, ready for 
                                      * allocation */
    unsigned int align;              /* object alignment */
    struct alloc alloc;              /* underlying allocator */
    struct objalloc_alloc *next;     /* linked list of objects ready for 
                                      * allocation */
    struct objalloc_chunk chunk;     /* first chunk */
};

/* internal function to check that the object allocator is in a sane state */
static int objalloc_invariant(struct objalloc *obj) {
    struct objalloc_alloc *alloc = obj->next,
                          *nextalloc;
    struct objalloc_chunk *chunk,
                          *next;
    unsigned int reserved = 0;

    if (!DEAR_DEBUG) {
        return 1;
    }

    /* check all allocations are from our set of memory */
    while (alloc) {
        /* check alignment */
        assert((((unsigned long int) alloc) / obj->align) * obj->align 
          == (unsigned long int) alloc);
        VALGRIND_MAKE_READABLE(alloc, sizeof(*alloc));
        nextalloc = alloc->next;
        /* this check is a bit too expensive even for DEAR_DEBUG
        if (!objalloc_is_managed(obj, alloc)) {
            assert(!CRASH);
            return 0;
        } */
        VALGRIND_MAKE_NOACCESS(alloc, sizeof(*alloc));
        alloc = nextalloc;
        reserved++;
    }

    /* check we have the number reserved that we said we did */
    if (reserved != obj->reserved) {
        assert(!CRASH);
        return 0;
    }

    /* check each chunk */
    chunk = &obj->chunk;
    while (chunk) {
        void *curr,
             *end;

        /* mark header as accessable */
        VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));

        curr = mem_align(mem_ptradd(chunk, sizeof(*chunk)), obj->align);
        end = mem_ptradd(chunk, sizeof(*chunk) + chunk->size);

        while (MEM_PTRADD(curr, obj->allocsize + obj->redzone) <= end) {
            unsigned int i;

            /* make redzone valid */
            VALGRIND_MAKE_READABLE(MEM_PTRADD(curr, obj->allocsize), 
              obj->redzone);

            /* check redzone value */
            for (i = 0; i < obj->redzone; i++) {
                if (((unsigned char *) mem_ptradd(curr, obj->allocsize))[i] 
                  != FILL_BYTE(i)) {
                    /* redzone has been violated */
                    assert(!CRASH);
                }
            }

            /* make redzone invalid again */
            VALGRIND_MAKE_NOACCESS(MEM_PTRADD(curr, obj->allocsize), 
              obj->redzone);

            curr = MEM_PTRADD(curr, obj->allocsize + obj->redzone);
        }

        next = chunk->next;

        /* make header invalid again */
        VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));

        chunk = next;
    }

    return 1;
}

/* internal function to break up the memory in a chunk and allocate it to the
 * list in the alloc object.  free indicates whether allocations should be
 * free'd as we go (so that valgrind doesn't report leaks). */
static void objalloc_chunkify(struct objalloc *obj,
  struct objalloc_chunk *chunk, int free) {
    void *curr = mem_align(mem_ptradd(chunk, sizeof(*chunk)), obj->align),
         *end = mem_ptradd(chunk, sizeof(*chunk) + chunk->size);
    struct objalloc_alloc *alloc;

    while (MEM_PTRADD(curr, obj->allocsize + obj->redzone) <= end) {
        /* append allocation to the start of the object's linked list */
        unsigned int i;

        alloc = curr;
        alloc->next = obj->next;
        obj->next = alloc;
        obj->reserved++;

        /* free the memory from valgrind's point of view */
        if (RUNNING_ON_VALGRIND && free) {
            VALGRIND_FREELIKE_BLOCK(alloc, 0);
        }

        /* mark the redzone with fill value */
        for (i = 0; i < obj->redzone; i++) {
            ((unsigned char *) mem_ptradd(alloc, obj->allocsize))[i] 
              = FILL_BYTE(i);
        }

        /* mark the redzone out of bounds using valgrind */
        VALGRIND_MAKE_NOACCESS(MEM_PTRADD(curr, obj->allocsize), obj->redzone);

        curr = MEM_PTRADD(curr, obj->allocsize + obj->redzone);
    }

    /* mark chunk header out of bounds using valgrind (acts as red-zone above
     * the objects) */
    VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));

    assert(objalloc_invariant(obj));
}

struct objalloc *objalloc_new(unsigned int size, unsigned int align,
  unsigned int redzone, unsigned int bulkalloc, const struct alloc *alloc) {
    struct objalloc *obj;
    unsigned int min;

    /* don't allow 0 sized objects, it doesn't make any sense */
    if (!size) {
        return NULL;
    }

    if (!alloc) {
        alloc = &alloc_system;
    }

    if (!align) {
        align = mem_align_max();
    }

    /* ensure that size is a multiple of the alignment */
    if (size < sizeof(struct objalloc_alloc)) {
        size = sizeof(struct objalloc_alloc);
    }
    if (align * (size / align) != size) {
        /* round up to nearest alignment boundary */
        size = align * (size / align + 1);
    }

    /* figure out redzone allowing for alignment */
    redzone = ((redzone + (align - 1)) / align) * align;

    /* figure out minimum size that we need bulkalloc to be to allocate the
     * object, a chunk header, one redzone and one object */
    min = sizeof(struct objalloc) + align + size + redzone;
    if (bulkalloc < min) {
        bulkalloc = min;
    }

    if ((obj = alloc->malloc(alloc->opaque, bulkalloc))) {
        obj->align = align;
        obj->allocsize = size;
        obj->redzone = redzone;
        obj->chunksize = bulkalloc;
        obj->next = NULL;
        obj->chunk.next = NULL;
        obj->chunk.size = bulkalloc - sizeof(*obj);
        obj->allocated = 0;
        obj->reserved = 0;
        obj->alloc = *alloc;
        objalloc_chunkify(obj, &obj->chunk, 0);

        if (!objalloc_invariant(obj)) {
            objalloc_delete(obj);
            obj = NULL;
        }
    }

    return obj;
}

void objalloc_delete(struct objalloc *obj) {
    struct objalloc_chunk *chunk,
                          *next;

    /* bring chunk headers back into addressable space */
    VALGRIND_MAKE_READABLE(&obj->chunk, sizeof(obj->chunk));

    chunk = obj->chunk.next;
    while (chunk) {
        void *curr,
             *end;

        /* bring chunk headers back into addressable space */
        VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));
        next = chunk->next;

        curr = mem_align(mem_ptradd(chunk, sizeof(*chunk)), obj->align);
        end = mem_ptradd(chunk, sizeof(*chunk) + chunk->size);

        while (obj->redzone 
          && MEM_PTRADD(curr, obj->allocsize + obj->redzone) <= end) {
            unsigned int i;

            /* make redzone valid */
            VALGRIND_MAKE_READABLE(MEM_PTRADD(curr, obj->allocsize), 
              obj->redzone);

            /* check redzone value */
            for (i = 0; i < obj->redzone; i++) {
                if (((unsigned char *) mem_ptradd(curr, obj->allocsize))[i] 
                  != FILL_BYTE(i)) {
                    /* redzone has been violated */
                    assert(!CRASH);
                }
            }

            curr = MEM_PTRADD(curr, obj->allocsize + obj->redzone);
        }

        obj->alloc.free(obj->alloc.opaque, chunk);
        chunk = next;
    }

    obj->alloc.free(obj->alloc.opaque, obj);
}

unsigned int objalloc_reserve(struct objalloc *obj, unsigned int reserve) {
    /* have to allocate a new chunk */
    struct objalloc_chunk *chunk;

    while ((obj->reserved < reserve) 
      && (chunk = obj->alloc.malloc(obj->alloc.opaque, obj->chunksize))) {
        VALGRIND_MAKE_READABLE(&obj->chunk, sizeof(obj->chunk));

        chunk->next = obj->chunk.next;
        chunk->size = obj->chunksize - sizeof(*chunk);
        obj->chunk.next = chunk;

        VALGRIND_MAKE_NOACCESS(&obj->chunk, sizeof(obj->chunk));

        objalloc_chunkify(obj, chunk, 0);
        assert(obj->next);
    }

    return obj->reserved;
}

void *objalloc_malloc(struct objalloc *obj, unsigned int size) {
    void *ptr;

    if (size <= obj->allocsize) {
        if (!obj->next) {
            if (!objalloc_reserve(obj, 1)) {
                return NULL;
            }
        }

        assert(obj->reserved);
        assert(obj->next);
        ptr = obj->next;
        VALGRIND_MAKE_READABLE(ptr, obj->allocsize);
        obj->next = obj->next->next;
        VALGRIND_MAKE_WRITABLE(ptr, obj->allocsize);
        obj->allocated++;
        obj->reserved--;
        VALGRIND_MALLOCLIKE_BLOCK(ptr, obj->allocsize, 0, 0);
        return ptr;
    } else {
        return NULL;
    }
}

void objalloc_free(struct objalloc *obj, void *ptr) {
    struct objalloc_alloc *alloc = ptr;
    unsigned char *cptr = ptr;
    unsigned int i;

    if (!ptr) {
        return;
    }

    assert(objalloc_is_managed(obj, ptr));

    alloc->next = obj->next;
    obj->next = alloc;
    obj->allocated--;
    obj->reserved++;

    /* check that redzone is intact */
    cptr += obj->allocsize;
    VALGRIND_MAKE_READABLE(cptr, obj->redzone);
    for (i = 0; i < obj->redzone; i++) {
        if (cptr[i] != FILL_BYTE(i)) {
            /* redzone has been violated */
            assert(!CRASH);
        }
    }
    VALGRIND_MAKE_NOACCESS(cptr, obj->redzone);

    VALGRIND_FREELIKE_BLOCK(ptr, 0);
    VALGRIND_MAKE_NOACCESS(ptr, obj->allocsize);
}

void *objalloc_realloc(struct objalloc *obj, void *ptr, unsigned int size) {
    if (size) {
        if (ptr) {
            assert(objalloc_is_managed(obj, ptr));

            if (size <= obj->allocsize) {
                return ptr;
            }
        } else {
            return objalloc_malloc(obj, size);
        }
    } else {
        if (ptr) {
            assert(objalloc_is_managed(obj, ptr));
            objalloc_free(obj, ptr);
        }
    }

    return NULL;
}

void objalloc_clear(struct objalloc *obj) {
    struct objalloc_chunk *chunk,
                          *next;
    struct objalloc_alloc *alloc;

    /* in order to call FREELIKE_BLOCK on all current allocations, we're going
     * to call MALLOCLIKE_BLOCK on all allocations *not* allocated, and then
     * FREELIKE_BLOCK them all in chunkify.  Its a little hacky, but its simple
     * and it works */
    if (RUNNING_ON_VALGRIND) {
        alloc = obj->next;
        while (alloc) {
            VALGRIND_MALLOCLIKE_BLOCK(alloc, obj->allocsize, 0, 0);
            VALGRIND_MAKE_READABLE(alloc, sizeof(*alloc));
            alloc = alloc->next;
        }
    }

    obj->next = NULL;
    obj->allocated = 0;
    obj->reserved = 0;

    chunk = &obj->chunk;
    do {
        /* make chunk contents valid */
        VALGRIND_MAKE_READABLE(chunk, obj->chunksize);
        next = chunk->next;

        objalloc_chunkify(obj, chunk, 1);
    } while ((chunk = next));
}

void objalloc_drain(struct objalloc *obj) {
    struct objalloc_chunk *chunk,
                          *next,
                          *prevchunk;
    struct objalloc_alloc *alloc,
                          *prev,
                          *nextalloc;
    unsigned int capacity,
                 count;

    if (!obj->next) {
        return;
    }

    /* try to free as much memory as possible.  This algorithm sucks (iterates
     * over the list of objects *way* too many times), but its simple and
     * unlikely to cause problems */

    chunk = obj->chunk.next;
    prevchunk = &obj->chunk;
    while (chunk) {
        /* make chunk header valid */
        VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));

        next = chunk->next;
        capacity = chunk->size / (obj->allocsize + obj->redzone);
        count = 0;

        for (alloc = obj->next; alloc; alloc = nextalloc) {
            if ((((void *) alloc) >= mem_ptradd(chunk, sizeof(*chunk))) 
              && (((void *) alloc) 
                < mem_ptradd(chunk, sizeof(*chunk) + chunk->size))) {

                count++;
            }
            VALGRIND_MAKE_READABLE(alloc, sizeof(*alloc));
            nextalloc = alloc->next;
            VALGRIND_MAKE_NOACCESS(alloc, sizeof(*alloc));
        }

        assert(count <= capacity);
        if (count == capacity) {
            /* can remove this chunk */

            obj->reserved -= capacity;
            alloc = obj->next;
            while (alloc 
              && (((void *) alloc) >= mem_ptradd(chunk, sizeof(*chunk))) 
              && (((void *) alloc) 
                < mem_ptradd(chunk, sizeof(*chunk) + chunk->size))) {

                VALGRIND_MAKE_READABLE(alloc, sizeof(*alloc));
                alloc = obj->next = alloc->next;
            }

            prev = alloc;
            while (alloc) {
                if ((((void *) alloc) >= mem_ptradd(chunk, sizeof(*chunk))) 
                  && (((void *) alloc) 
                    < mem_ptradd(chunk, sizeof(*chunk) + chunk->size))) {

                    VALGRIND_MAKE_READABLE(alloc, sizeof(*alloc));
                    VALGRIND_MAKE_WRITABLE(prev, sizeof(*prev));
                    prev->next = alloc = alloc->next;
                    VALGRIND_MAKE_NOACCESS(prev, sizeof(*prev));
                } else {
                    prev = alloc;
                    VALGRIND_MAKE_READABLE(alloc, sizeof(*alloc));
                    nextalloc = alloc->next;
                    VALGRIND_MAKE_NOACCESS(alloc, sizeof(*alloc));
                    alloc = nextalloc;
                }
            }

            assert(prevchunk);
            prevchunk->next = next;
            obj->alloc.free(obj->alloc.opaque, chunk);
        } else {
            prevchunk = chunk;
            /* make chunk header invalid */
            VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));
        }

        chunk = next;
    }
}

unsigned int objalloc_allocated(struct objalloc *obj) {
    return obj->allocated;
}

int objalloc_is_managed(struct objalloc *obj, void *ptr) {
    struct objalloc_chunk *chunk,
                          *next;

    chunk = &obj->chunk;
    do {
        /* make chunk header valid */
        VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));

        if ((ptr >= mem_ptradd(chunk, sizeof(*chunk))) 
          && (ptr < mem_ptradd(chunk, sizeof(*chunk) + chunk->size))) {
            return 1;
        }

        next = chunk->next;
        /* make chunk header invalid */
        VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));
    } while ((chunk = next));

    return 0;
}

unsigned int objalloc_memsize(struct objalloc *obj, void *ptr) {
    struct objalloc_chunk *chunk,
                          *next;
    unsigned int chunks = 0;

    chunk = &obj->chunk;
    do {
        /* make chunk header valid */
        VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));
        chunks++;
        next = chunk->next;
        /* make chunk header invalid */
        VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));
    } while ((chunk = next));

    return sizeof(*obj) - sizeof(obj->chunk) + chunks * obj->chunksize;
}


unsigned int objalloc_overhead_first() {
    return sizeof(struct objalloc);
}

unsigned int objalloc_overhead() {
    return sizeof(struct objalloc_chunk);
}

unsigned int objalloc_objsize(struct objalloc *obj) {
    return obj->allocsize;
}

#ifdef OBJALLOC_TEST

#include <stdlib.h>

int main() {
    unsigned int i;
    int *arr[20];
    struct objalloc *alloc;

    /* just alloc and free */

    alloc = objalloc_new(sizeof(int), 0, 1, 10 * sizeof(int), NULL);
    assert(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = objalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }
    assert(!objalloc_malloc(alloc, sizeof(int) + 1));

    for (i = 0; i < 20; i++) {
        assert(*arr[i] == i);
        objalloc_free(alloc, arr[i]);
    }

    objalloc_delete(alloc); 

    /* different sizes */

    alloc = objalloc_new(sizeof(int), 0, 1, 10 * sizeof(int), NULL);
    assert(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = objalloc_malloc(alloc, i);
        if (i <= sizeof(int)) {
            assert(arr[i]);
            arr[i] = objalloc_realloc(alloc, arr[i], i);
            if (i) {
                assert(arr[i]);
            } else {
                assert(!arr[i]);
            }
        } else {
            assert(!arr[i]);
        }
    }
    assert(!objalloc_malloc(alloc, sizeof(int) + 1));

    for (i = 0; i < 20; i++) {
        objalloc_free(alloc, arr[i]);
    }

    objalloc_delete(alloc); 

    /* test drain */

    alloc = objalloc_new(sizeof(int), 0, 1, 10 * sizeof(int), NULL);
    assert(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = objalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }
    assert(!objalloc_malloc(alloc, sizeof(int) + 1));

    for (i = 0; i < 20; i++) {
        assert(*arr[i] == i);
        objalloc_free(alloc, arr[i]);
    }

    objalloc_drain(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = objalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }
    assert(!objalloc_malloc(alloc, sizeof(int) + 1));

    for (i = 0; i < 20; i++) {
        assert(*arr[i] == i);
        objalloc_free(alloc, arr[i]);
    }

    objalloc_drain(alloc);

    objalloc_delete(alloc); 

    /* perform some invalid accesses */

    alloc = objalloc_new(sizeof(int), 0, 1, 10 * sizeof(int), NULL);
    assert(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = objalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }

    /* valid access */
    *arr[0] = 0;

    /* FIXME: invalid accesses 
    i = arr[0][-1];
    arr[1][-1] = -1;
    arr[1][1] = -1; */

    for (i = 0; i < 20; i++) {
        assert(*arr[i] == i);
        objalloc_free(alloc, arr[i]);
    }

    objalloc_clear(alloc);
    objalloc_delete(alloc);

    /* cause leaks */

    alloc = objalloc_new(sizeof(int), 0, 1, 10 * sizeof(int), NULL);
    assert(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = objalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }

    for (i = 1; i < 19; i++) {
        assert(*arr[i] == i);
        objalloc_free(alloc, arr[i]);
    }

    objalloc_delete(alloc); 

    /* use clear */

    alloc = objalloc_new(sizeof(int), 0, 1, 10 * sizeof(int), NULL);
    assert(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = objalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }

    objalloc_clear(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = objalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }

    objalloc_clear(alloc);
    objalloc_delete(alloc); 

    return EXIT_SUCCESS;
}

#endif

