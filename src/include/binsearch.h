/* binsearch.h declares a function for binary searching that is much like
 * the standard library bsearch
 *
 * written nml 2002-12-12
 *
 */

#ifndef BINSEARCH_H
#define BINSEARCH_H

#include <stdlib.h>

/* returns the position where the element given would be in the sorted array.
   If it is there then you can use compar to establish that with one further
   comparison.  If not then you could possibly insert it there.  Bear in mind
   that this routine can return one element past the end of the array (if it
   is greater than all elements in current array) so that address needs to be
   valid. (sorry, this is the nature of the algorithm, think about it).  Note
   that this differs from c89 bsearch in that this can be used for sorted 
   insertion as well as pure binary search, as the c89 version returns NULL if
   the key is not found */
void *binsearch(const void* key, const void* base, size_t nel, 
  size_t width, int (*compar)(const void* one, const void* two));

#endif

