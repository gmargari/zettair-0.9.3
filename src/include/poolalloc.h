/* poolalloc.h declares an interface to an object that quickly allocates memory
 * from a common pool.  The pool allocator does not reuse free'd memory, so
 * memory consumption will grow until you delete or clear the pool.  This makes
 * it suitable for allocating a set of entities quickly from a common pool and
 * then free'ing them all at once.  The pool allocator also supports a number 
 * of debugging features, including integration with valgrind.
 * (http://valgrind.kde.org/)
 *
 * written nml 2004-09-02
 *
 */

#ifndef POOLALLOC_H
#define POOLALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "alloc.h"

struct poolalloc;

/* create a new pool allocator, which requests memory in chunks of at least
 * bulkalloc size from the alloc allocator.  
 * It will request larger chunks of memory in response to
 * requests that can't be satisfied any other way, or if you specify a size
 * that is too small to support normal operation.  A redzone of at least 
 * redzone bytes is placed before and after each object allocated from this 
 * allocator.  If alloc_opaque, mallocfn and freefn are NULL, system malloc/free
 * are used. */
struct poolalloc *poolalloc_new(unsigned int redzone, unsigned int bulkalloc, 
  const struct alloc *alloc);

/* delete a pool allocator, freeing all memory utilised by it, regardless of
 * whether it is being used or not */
void poolalloc_delete(struct poolalloc *pool);

/* allocate some memory of at least size size from the allocator.  Returns a
 * pointer to the memory on success and NULL on failure. */
void *poolalloc_malloc(struct poolalloc *pool, unsigned int size);

/* free a prior allocation, after which ptr is no longer valid. */
void poolalloc_free(struct poolalloc *pool, void *ptr);

/* allocate some memory that is aligned on a boundary evenly divisible by
 * align.  0 is taken to mean no alignment requirement.  align is assumed to be
 * a power of two */
void *poolalloc_memalign(struct poolalloc *pool, unsigned int size, 
  unsigned int align);

/* no realloc because otherwise we'd have to track how large memory allocations
 * are */

/* return all allocated memory to this allocator (but doesn't return the memory
 * to the underlying alloctor though, see _drain below for that) */
void poolalloc_clear(struct poolalloc *pool);

/* return as much memory as possible to the underlying memory allocator.  Note
 * that due to the nature of the allocator (free'd memory isn't reused),
 * _drain()ing will do nothing unless you've first _clear()'d the pool. */
void poolalloc_drain(struct poolalloc *pool);

/* returns the number of allocations currently active */
unsigned int poolalloc_allocated(struct poolalloc *pool);

/* returns 1 if the given pointer was allocated from this allocator, 0
 * otherwise.  This is not a particularly efficient operation, so don't rely on
 * it being fast (intended for debugging purposes). */
int poolalloc_is_managed(struct poolalloc *pool, void *ptr);

/* memory overhead of the first and subsequent chunks allocated */
unsigned int poolalloc_overhead_first();
unsigned int poolalloc_overhead();

#ifdef __cplusplus
}
#endif

#endif

