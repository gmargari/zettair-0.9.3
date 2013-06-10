/* staticalloc.h declares an interface to an object that manages a static piece
 * of memory, allocating it (all) in response to requests.  This is a useful
 * complement to the other allocators (poolalloc, objalloc) if you want to use
 * statically (i.e. on the stack) allocated memory.  It is designed to be as
 * lightweight as possible, using only a single word of overhead.
 *
 * The allocator will not allocate any further memory after the provided area
 * has been allocated, until it is next freed.
 *
 * written nml 2005-03-10
 *
 */

#ifndef STATICALLOC_H
#define STATICALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

struct staticalloc;

/* create a new static allocation object, which will allocate area (which is of
 * size bytes) in response to an allocation request.  In order to keep overhead
 * to a minimum, size cannot be greater than INT_MAX or construction will 
 * fail.  Note that the area must be great enough to support the allocations you
 * wish to make as well as one extra machine word of overhead. */
struct staticalloc *staticalloc_new(void *area, unsigned int size);

/* delete a static allocator object, releasing the given area */
void staticalloc_delete(struct staticalloc *alloc);

/* allocate some memory of at least size size from the allocator.  Returns a
 * pointer to the memory on success and NULL on failure. */
void *staticalloc_malloc(struct staticalloc *alloc, unsigned int size);

/* free a prior allocation, after which ptr is no longer valid. */
void staticalloc_free(struct staticalloc *alloc, void *ptr);

/* returns the number of allocations currently active.  Will be either 1 or
 * 0. */
unsigned int staticalloc_allocated(struct staticalloc *alloc);

/* returns 1 if the given pointer was allocated from this allocator, 0
 * otherwise. */
int staticalloc_is_managed(struct staticalloc *alloc, void *ptr);

unsigned int staticalloc_overhead();

/* STATICALLOC_DECL is a macro to make it convenient to allocate space 
 * on the stack.  Provide the variable name and a constant number of bytes 
 * required, and a properly aligned space of at least that size (sometimes a 
 * little padding is necessary) will be declared.  Overhead is included in the 
 * calculation, so you don't need to worry about it.  
 * You can then use name as a static allocator that will allocate its area 
 * of size bytes.  This macro can only be used in the same locations as 
 * you can declare variables.  Note that you'll still need to 
 * staticalloc_delete(name) after use to get valgrind leak detection 
 * working properly. */
#define STATICALLOC_DECL(name, bytes)                                         \
  /* declare a number of words on the stack, using void * as word type to     \
   * avoid sizeof(int) issues */                                              \
  void *name##_stackspace[1 + (bytes + sizeof(void *) - 1) / sizeof(void *)]; \
  /* now declare the static allocator */                                      \
  struct staticalloc *name                                                    \
    = staticalloc_new(name##_stackspace, sizeof(void*) + bytes)

#ifdef __cplusplus
}
#endif

#endif

