/* poolalloc.c implements a pooled allocator that efficiently returns memory
 * via a malloc-like interface.  This allocator is more efficient than general
 * purpose malloc because it does not track free memory.  This greatly
 * simplifies memory management, but also means that memory consumption will
 * grow until the manager is clear()'d or delete()'d
 *
 * written nml 2004-09-02
 *
 */

#include "firstinclude.h"

#include "chash.h"
#include "def.h"
#include "poolalloc.h"
#include "mem.h"
#include "_mem.h"
#include "zvalgrind.h"

#include <assert.h>
#include <stdlib.h>

struct poolalloc_chunk {
    struct poolalloc_chunk *next;   /* pointer to next chunk in linked list */
    char *end;                      /* pointer to one past the end of chunk */
    char *curr;                     /* pointer to the current position in the 
                                     * chunk */
};

struct poolalloc {
    unsigned int redzone;           /* size of the redzone we're putting after 
                                     * each allocation */
    unsigned int chunksize;         /* size of chunks we're requesting from 
                                     * the underlying allocator */
    unsigned int allocated;         /* how many entities we've allocated */
    struct chash *allocations;      /* hashtable of current allocations (only 
                                     * used under valgrind, because valgrind 
                                     * is the only tool sensitive enough to 
                                     * pick up memory leaks within this 
                                     * module) */
    struct alloc alloc;             /* underlying allocator */
    struct poolalloc_chunk *curr;   /* chunk we're currently allocating from */
    struct poolalloc_chunk chunk;   /* first chunk */
};

#ifndef NDEBUG
static int poolalloc_invariant(struct poolalloc *pool) {
    struct poolalloc_chunk *chunk = pool->curr,
                           *next;
    unsigned int chunklen;

    if (DEAR_DEBUG) {
        /* the allocator must always have a current chunk */
        if (!pool->curr) {
            assert(!CRASH);
            return 0;
        }

        while (chunk) {
            VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));
            next = chunk->next;

            if (chunk->curr > chunk->end) {
                VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));
                assert(!CRASH);
                return 0;
            }

            chunklen = MEM_PTRDIFF(chunk->end, chunk);
            if (chunk == &pool->chunk) {
                /* this is the original chunk, check size differently */
                chunklen -= sizeof(*chunk);
                if (chunklen 
                  < pool->chunksize - (mem_align_max() - 1) - sizeof(*pool)) {
                    VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));
                    assert(!CRASH);
                    return 0;
                }
            } else {
                if (chunklen < pool->chunksize - (mem_align_max() - 1)) {
                    VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));
                    assert(!CRASH);
                    return 0;
                }
            }
            VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));
            chunk = next;
        }

        return 1;
    } else {
        return 1;
    }
}
#endif

struct poolalloc *poolalloc_new(unsigned int redzone, unsigned int bulkalloc, 
  const struct alloc *alloc) {
    struct poolalloc *pool;
    unsigned int min;
    
    min = sizeof(struct poolalloc) + redzone + mem_align_max() + sizeof(int);

    if (bulkalloc < min) {
        bulkalloc = min;
    }

    if (!alloc) {
        alloc = &alloc_system;
    }

    if ((pool = alloc->malloc(alloc->opaque, bulkalloc))) {
        pool->redzone = redzone;
        pool->chunksize = bulkalloc;
        pool->allocated = 0;
        pool->curr = &pool->chunk;
        pool->chunk.next = NULL;
        pool->chunk.curr = MEM_PTRADD(&pool->chunk, sizeof(pool->chunk));
        pool->chunk.end = MEM_PTRADD(pool, bulkalloc);
        pool->alloc = *alloc;
        pool->allocations = NULL;

        if (RUNNING_ON_VALGRIND) {
            /* note that we use luint as type for chash because ptr needs 
             * comparison functions et al */
            if ((pool->allocations = chash_luint_new(0, 0.5))) {
                /* succeeded, do nothing */
            } else {
                alloc->free(alloc->opaque, pool);
                return NULL;
            }

            VALGRIND_MAKE_NOACCESS(&pool->chunk, bulkalloc);
        }
    }

    return pool;
}

void poolalloc_delete(struct poolalloc *pool) {
    struct poolalloc_chunk *chunk,
                           *next;

    chunk = pool->curr;
    assert(chunk);
    while (chunk) {
        VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));
        next = chunk->next;
        if (chunk != &pool->chunk) {
            pool->alloc.free(pool->alloc.opaque, chunk);
        }
        chunk = next;
    }

    if (RUNNING_ON_VALGRIND) {
        chash_delete(pool->allocations);
    }

    pool->alloc.free(pool->alloc.opaque, pool);
}

void *poolalloc_malloc(struct poolalloc *pool, unsigned int size) {
    return poolalloc_memalign(pool, size, mem_align_max());
}

void *poolalloc_memalign(struct poolalloc *pool, unsigned int size, 
  unsigned int align) {
    unsigned int min;
    char *ptr;
    struct poolalloc_chunk *chunk = pool->curr,
                           *next;
    do {
        assert(chunk);
        do {
            VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));
            next = chunk->next;
            ptr = MEM_ALIGN(chunk->curr, align);
            if ((ptr < chunk->end) 
              && ((unsigned int) MEM_PTRDIFF(chunk->end, ptr)) 
                >= size + pool->redzone) {

                /* allocate from current chunk */
                VALGRIND_MAKE_WRITABLE(ptr, size);
                VALGRIND_MALLOCLIKE_BLOCK(ptr, size, 0, 0);
                if (RUNNING_ON_VALGRIND) {
                    /* insert entry into hashtable so we can keep track of
                     * allocations */
                    chash_luint_ptr_insert(pool->allocations, 
                      (unsigned long int) ptr, ptr);
                }
                chunk->curr = MEM_PTRADD(ptr, size + pool->redzone);
                assert(chunk->curr <= chunk->end);
                VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));
                assert(poolalloc_invariant(pool));
                pool->allocated++;
                return ptr;
            }

            VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));
        } while ((chunk = next));

        /* failed to allocate memory from existing chunks */

        /* figure out the maximum space this allocation will consume */
        min = size + pool->redzone + align + sizeof(*chunk);

        /* up it to the chunksize if its below */
        if (min < pool->chunksize) {
            min = pool->chunksize;
        }

        /* allocate new chunk */
        if ((chunk = pool->alloc.malloc(pool->alloc.opaque, min))) {
            chunk->next = pool->curr;
            chunk->curr = MEM_PTRADD(chunk, sizeof(*chunk));
            chunk->end = MEM_PTRADD(chunk, min);
            next = pool->curr = chunk;

            /* make sure we'll allocate next time around */
            assert(((unsigned int) 
              MEM_PTRDIFF(chunk->end, MEM_ALIGN(chunk->curr, align)))
                >= size + pool->redzone);

            VALGRIND_MAKE_NOACCESS(chunk, min);
        } else {
            return NULL;
        }
        /* loop, and we'll allocate it the next time around */
    } while (1);
}

void poolalloc_free(struct poolalloc *pool, void *ptr) {
    VALGRIND_FREELIKE_BLOCK(ptr, 0);
    VALGRIND_MAKE_NOACCESS(ptr, 1);
    if (RUNNING_ON_VALGRIND) {
        void *dummy = NULL;

        /* remove allocation from allocations table */
        chash_luint_ptr_remove(pool->allocations, (unsigned long int) ptr, 
          &dummy);
        assert(dummy == ptr);
    }
    pool->allocated--;
}

void poolalloc_clear(struct poolalloc *pool) {
    struct poolalloc_chunk *chunk = pool->curr,
                           *next;

    if (RUNNING_ON_VALGRIND) {
        /* free all blocks so we don't get leak reports */
        struct chash_iter *iter = chash_iter_new(pool->allocations);

        if (iter) {
            unsigned long int key;
            void **data;

            while (chash_iter_luint_ptr_next(iter, &key, &data) == CHASH_OK) {
                VALGRIND_FREELIKE_BLOCK(*data, 0);
                pool->allocated--;
            }

            chash_iter_delete(iter);
        }
        assert(pool->allocated == 0);
        chash_clear(pool->allocations);
    }

    assert(chunk);
    do {
        VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));
        next = chunk->next;
        chunk->curr = MEM_PTRADD(chunk, sizeof(*chunk));
        VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));
    } while ((chunk = next));

    pool->allocated = 0;
}

void poolalloc_drain(struct poolalloc *pool) {
    struct poolalloc_chunk *chunk = pool->curr,
                           *next,
                           *prev = NULL;


    /* note that we start at chunk after the first one */

    do {
        VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));
        next = chunk->next;
        if ((chunk->curr == MEM_PTRADD(chunk, sizeof(*chunk))) 
          && (chunk != &pool->chunk)) {
            if (prev) {
                VALGRIND_MAKE_READABLE(prev, sizeof(*chunk));
                prev->next = next;
                VALGRIND_MAKE_NOACCESS(prev, sizeof(*chunk));
            } else {
                pool->curr = next;
            }
            pool->alloc.free(pool->alloc.opaque, chunk);
        } else {
            prev = chunk;
        }
        VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));
    } while ((chunk = next));
}

unsigned int poolalloc_allocated(struct poolalloc *pool) {
    return pool->allocated;
}

int poolalloc_is_managed(struct poolalloc *pool, void *ptr) {
    struct poolalloc_chunk *chunk = pool->curr,
                           *next;

    do {
        VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));
        next = chunk->next;
        if ((ptr >= (void *) MEM_PTRADD(chunk, sizeof(*chunk))) 
          && (ptr < (void *) chunk->end)) {
            VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));
            return 1;
        }
        VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));
    } while ((chunk = next));

    return 0;
}

unsigned int poolalloc_pages(struct poolalloc *pool) {
    struct poolalloc_chunk *chunk = pool->curr,
                           *next;
    unsigned int pages = 0;

    assert(chunk);
    do {
        VALGRIND_MAKE_READABLE(chunk, sizeof(*chunk));
        next = chunk->next;
        pages++;
        VALGRIND_MAKE_NOACCESS(chunk, sizeof(*chunk));
    } while ((chunk = next));

    return pages;
}

unsigned int poolalloc_overhead_first(struct poolalloc *pool) {
    return sizeof(struct poolalloc);
}

unsigned int poolalloc_overhead(struct poolalloc *pool) {
    return sizeof(struct poolalloc_chunk);
}

#ifdef POOLALLOC_TEST

#include <stdlib.h>

int main() {
    unsigned int i;
    int *arr[20];
    struct poolalloc *alloc;

    /* just alloc and free */

    alloc = poolalloc_new(1, 10 * sizeof(int), NULL);
    assert(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = poolalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }

    for (i = 0; i < 20; i++) {
        assert(*arr[i] == i);
        assert(poolalloc_is_managed(alloc, arr[i]));
        poolalloc_free(alloc, arr[i]);
    }

    poolalloc_delete(alloc); 

    /* different sizes */

    alloc = poolalloc_new(1, 10 * sizeof(int), NULL);
    assert(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = poolalloc_malloc(alloc, i);
        assert(arr[i]);
    }

    for (i = 0; i < 20; i++) {
        assert(poolalloc_is_managed(alloc, arr[i]));
        poolalloc_free(alloc, arr[i]);
    }

    for (i = 0; i < 20; i++) {
        arr[i] = poolalloc_malloc(alloc, 200 + i);
        assert(arr[i]);
    }

    for (i = 0; i < 20; i++) {
        assert(poolalloc_is_managed(alloc, arr[i]));
        poolalloc_free(alloc, arr[i]);
    }

    poolalloc_delete(alloc); 

    /* test drain */

    alloc = poolalloc_new(1, 10 * sizeof(int), NULL);
    assert(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = poolalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }

    for (i = 0; i < 20; i++) {
        assert(*arr[i] == i);
        assert(poolalloc_is_managed(alloc, arr[i]));
        poolalloc_free(alloc, arr[i]);
    }

    poolalloc_drain(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = poolalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }

    for (i = 0; i < 20; i++) {
        assert(*arr[i] == i);
        assert(poolalloc_is_managed(alloc, arr[i]));
        poolalloc_free(alloc, arr[i]);
    }

    poolalloc_clear(alloc);
    poolalloc_drain(alloc);
    assert(poolalloc_pages(alloc) == 1);

    poolalloc_delete(alloc); 

    /* perform some invalid accesses */

    alloc = poolalloc_new(1, 10 * sizeof(int), NULL);
    assert(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = poolalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }

    /* valid access */
    *arr[0] = 0;

    /* FIXME: invalid accesses 
    i = arr[0][-1];
    arr[1][-1] = -1;
    arr[1][1] = -1;  */

    for (i = 0; i < 20; i++) {
        assert(*arr[i] == i);
        assert(poolalloc_is_managed(alloc, arr[i]));
        poolalloc_free(alloc, arr[i]);
    }

    poolalloc_clear(alloc);
    poolalloc_delete(alloc);

    /* cause leaks */

    alloc = poolalloc_new(1, 10 * sizeof(int), NULL);
    assert(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = poolalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }

    for (i = 1; i < 19; i++) {
        assert(*arr[i] == i);
        assert(poolalloc_is_managed(alloc, arr[i]));
        poolalloc_free(alloc, arr[i]);
    }

    poolalloc_delete(alloc); 

    /* use clear */

    alloc = poolalloc_new(1, 10 * sizeof(int), NULL);
    assert(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = poolalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }

    poolalloc_clear(alloc);

    for (i = 0; i < 20; i++) {
        arr[i] = poolalloc_malloc(alloc, sizeof(int));
        *arr[i] = i;
    }

    poolalloc_clear(alloc);
    poolalloc_delete(alloc); 

    return EXIT_SUCCESS;
}

#endif

