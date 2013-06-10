/* objalloc.h declares an interface to an object that quickly allocates objects 
 * of the same size from a common memory pool.  The object also has various
 * debugging features, including integration with valgrind
 * (http://valgrind.kde.org/)
 *
 * written nml 2004-08-30
 *
 */

#ifndef OBJALLOC_H
#define OBJALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "alloc.h"

struct objalloc;

/* create a new object allocator, which allocates chunks of memory of size
 * size from the alloc allocator, that are aligned on an address divisible by
 * align.  An alignment of 0 returns objects suitable for anything on this
 * architecture.  A redzone of at least redzone bytes is 
 * placed before and after each object allocated from this allocator.  
 * The underlying allocation mechanism is used to allocate bulkalloc bytes at 
 * a time (can be rounded up if its too small).  
 * If alloc is NULL the system allocation functions (malloc, free) are used */
struct objalloc *objalloc_new(unsigned int size, unsigned int align, 
  unsigned int redzone, unsigned int bulkalloc, const struct alloc *alloc);
    
/* delete an object allocator, freeing all memory utilised by it, regardless of
 * whether it is being used or not.  Note that outstanding allocations will be
 * reported by valgrind as leaks unless you call objalloc_clear first. */
void objalloc_delete(struct objalloc *alloc);

/* allocate an object from the allocator.  the call will fail if size is
 * greater than the size of object this allocator can allocate.  Otherwise
 * returns a pointer to the memory on success.  Note that all allocations
 * occupy the number of bytes specified in the constructor, regardless of how
 * small size is here */
void *objalloc_malloc(struct objalloc *alloc, unsigned int size);

/* free a prior allocation, after which ptr is no longer valid. */
void objalloc_free(struct objalloc *alloc, void *ptr);

/* reallocate an object which has been previously allocated to be of size size.
 * Will return a pointer to the new location of the allocation on success, and
 * NULL on failure (which will occur if size is greater than the size specified
 * in the constructor).  Will free the given allocation if the given size is 0,
 * and will return a new allocation if the given pointer is NULL and size is
 * greater than 0 (same semantics as relloc(3)). */
void *objalloc_realloc(struct objalloc *alloc, void *ptr, unsigned int size);

/* return all allocated memory to this allocator (but doesn't return the memory
 * to the underlying alloctor though, see _drain below for that) */
void objalloc_clear(struct objalloc *alloc);

/* return as much memory as possible to the underlying memory allocator */
void objalloc_drain(struct objalloc *alloc);

/* returns the number of allocations currently active */
unsigned int objalloc_allocated(struct objalloc *alloc);

/* ensure that at least enough space is available to allocate reserve objects
 * without failure.  Value returned is the minimum number of allocations 
 * available without possible failure, and should be greater than or equal to
 * reserve if the call succeeded. */
unsigned int objalloc_reserve(struct objalloc *alloc, unsigned int reserve);

/* returns 1 if the given pointer is an object allocated from this allocator, 0
 * otherwise.  This is not a particularly efficient operation, so don't rely on
 * it being fast (intended for debugging purposes).  It can also return false
 * positives if you pass it pointers that lie between valid objects. */
int objalloc_is_managed(struct objalloc *alloc, void *ptr);

/* returns size of objects allocated */
unsigned int objalloc_objsize(struct objalloc *obj);

/* overhead on first block allocated */
unsigned int objalloc_overhead_first();

/* overhead on blocks after first allocated */
unsigned int objalloc_overhead();

/* returns the amount of memory held by the allocator */
unsigned int objalloc_memsize();

#ifdef __cplusplus
}
#endif

#endif

