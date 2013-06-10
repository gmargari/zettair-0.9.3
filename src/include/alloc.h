/* alloc.h is mostly a stub file that ties together all of the
 * allocation functions.  It typedefs function types for a few useful
 * things and provides wrappers around the system malloc, free and
 * realloc functions.
 *
 * written nml 2005-03-10
 *
 */

#ifndef ALLOC_H
#define ALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

/* form of a malloc-like function */
typedef void *(*alloc_mallocfn)(void *opaque, unsigned int size);

/* form of a free-like function */
typedef void (*alloc_freefn)(void *opaque, void *ptr);

/* structure of an allocator */
struct alloc {
    void *opaque;
    alloc_mallocfn malloc;
    alloc_freefn free;
};

/* wrapper around system malloc */
void *alloc_malloc(void *opaque, unsigned int size);

/* wrapper around system free */
void alloc_free(void *opaque, void *ptr);

/* system allocator */
extern const struct alloc alloc_system;

#ifdef __cplusplus
}
#endif

#endif

