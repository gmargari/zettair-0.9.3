/* freemap.h declares an interface to a structure that manages free
 * space.  
 *
 * This interface assumes that the need for space outstrips the word length 
 * of the current architecture, thus pointers to files are represented by 
 * [fileno,offset] pairs.
 *
 * written nml 2003-04-28
 *
 */

#ifndef FREEMAP_H
#define FREEMAP_H

#ifdef __cplusplus
extern "C" {
#endif

enum freemap_strategies {
    FREEMAP_STRATEGY_FIRST,       /* first fit */
    FREEMAP_STRATEGY_BEST,        /* best fit */
    FREEMAP_STRATEGY_WORST,       /* worst fit */
    FREEMAP_STRATEGY_CLOSE        /* close fit, choose via size-lists */
};

struct freemap;

/* create a new, empty freemap.  append is how much space we're allowed to
 * append to entries in order to keep the number of entries down */
struct freemap *freemap_new(enum freemap_strategies strategy, 
  unsigned int append, void *opaque, 
  int (*addfile)(void *opaque, unsigned int file, unsigned int *maxsize));

/* delete the freemap */
void freemap_delete(struct freemap *map);

enum {
    FREEMAP_OPT_NONE = 0,                      /* no options */
    FREEMAP_OPT_EXACT = (1 << 1),              /* don't overallocate */
    FREEMAP_OPT_LOCATION = (1 << 2)            /* allocate at location
                                                * (given as unsigned int, 
                                                * unsigned long int arguments) 
                                                * or fail */
};
 
/* allocate size bytes of memory.  fileno and offset are written into
 * parameters on success (true is returned).  Size is read from the size
 * parameter, and then the total space allocated is written into it.
 * On failure 0 is returned.  If exact is true then size will always be
 * unchanged, otherwise the freemap is free to overallocate slightly to suit
 * itself.  Note that if the structure doesn't have space to
 * allocate requested space, zero is returned but _err is not set. */
int freemap_malloc(struct freemap *fm, unsigned int *fileno, 
  unsigned long int *offset, unsigned int *size, int options, ...);

/* return some memory to the freemap */
int freemap_free(struct freemap *fm, unsigned int fileno, 
  unsigned long int offset, unsigned int size);

/* waste some memory (don't do this!), removing it from the freemap 
 * permanently (freemap keeps stats about this though) */
int freemap_waste(struct freemap *fm, unsigned int fileno, 
  unsigned long int offset, unsigned int size);
/* XXX: should probably be folded into freemap_free as option */

/* change the size of a previously allocated section of memory to greater than
 * or equal to size + additional.
 * Unlike realloc(3), there is no NULL pointer and so the previous
 * memory must have been allocated by _malloc.  Also, because the
 * freemap doesn't access disk, it doesn't know how move the contents
 * of a previous allocation to a new location, so it will fail if it
 * can't grow the current allocation.  On success, the additional amount of 
 * space allocated is returned (0 on failure) */
unsigned int freemap_realloc(struct freemap *fm, unsigned int fileno, 
  unsigned long int offset, unsigned int size, unsigned int additional, 
  int options, ...);
/* FIXME: additional should be an int */

/* last error that occurred with the freemap */
int freemap_err(const struct freemap *fm);

/* get the utilisation of the freemap, as a percentage [0.0, 1.0] */
double freemap_utilisation(const struct freemap *fm);

/* get the total amount of space managed by the freemap */
double freemap_space(const struct freemap *fm);

/* get the total amount of space wasted */
double freemap_wasted(const struct freemap *fm);

/* return the number of entries in the freemap */
unsigned int freemap_entries(const struct freemap *fm);

/* return the maximum amount that the freemap will append onto an entry */
unsigned int freemap_append(const struct freemap *fm);

/* checks that the freemap is internally consistent */
int freemap_consistent(const struct freemap *fm);

/* return the number of freemap entries in the index */
unsigned int freemap_indexed_entries(const struct freemap *fm);

/* return the strategy that this freemap is using. */
enum freemap_strategies freemap_strategy(const struct freemap *fm);

#include <stdio.h>

/* print the map to a stream for debugging purposes */
void freemap_print(const struct freemap *fm, FILE *output);

#ifdef __cplusplus
}
#endif

#endif

