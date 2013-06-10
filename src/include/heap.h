/* heap.h declares some implicit minheap functions and a partial sorting 
 * function that uses the classic heapsort to work its magic :o)
 *
 * written nml 2003-06-03
 *
 */

#ifndef HEAP_H
#define HEAP_H

#ifdef __cplusplus
extern "C" {
#endif

/* heap sort sorts the given array (array can be in any order).  base is a 
 * pointer to the start of the
 * array, nmemb is the number of elements in the array, sort is the number of
 * elements in the array that you would like sorted [0, nmemb], 
 * size is the size of each individual element (shouldn't be 0), and compar is 
 * a comparison function for elements.  After execution, the top sort elements 
 * of the array will be in sorted order, and will be smaller than the rest of 
 * the elements. */
void heap_sort(void *base, unsigned int nmemb, unsigned int sort, 
  unsigned int size, int (*compar)(const void *, const void *));

/* heapify creates an implicit heap from the given array by moving elements
 * around within the array */
void heap_heapify(void *base, unsigned int nmemb, unsigned int size, 
  int (*compar)(const void *, const void *));

/* replace replaces the root (smallest) element of the heap, and then returns
 * the new position of element within the heap.  Old root is copied into 
 * element */
void *heap_replace(void *base, unsigned int nmemb, unsigned int size,
  int (*compar)(const void *, const void *), void *element);

/* returns a pointer to the smallest element in the heap */
void *heap_peek(void *base, unsigned int nmemb, unsigned int size);

/* remove the smallest element from the heap, placing it at the back of the
 * array, decrementing *nmemb and returning a pointer to it.  Returns NULL on
 * failure (which can only happen if *nmemb is 0) */
void *heap_pop(void *base, unsigned int *nmemb, unsigned int size, 
  int (*cmp)(const void *one, const void *two));

/* insert inserts element into the heap, growing it by size in the process
 * (nmemb will be incremented to reflect the new number of elements).  
 * It is assumed that the array is large enough to hold another element */
void *heap_insert(void *base, unsigned int *nmemb, unsigned int size,
  int (*compar)(const void *, const void *), void *element);

/* remove a given element from the heap, shrinking it in the process (nmemb will
 * be decremented to reflect the new number of elements). */
void *heap_remove(void *base, unsigned int *nmemb, unsigned int size,
  int (*compar)(const void *, const void *), void *element);

/* finding an arbitrary element has to be implemented as a linear search along
 * the array, which you're entirely capable of doing yourself ;o) */

/* returns true if the array has the heap property (max reverses heap condition,
 * with function returning whether the heap is a maxheap) */
int heap_isheap(void *base, unsigned int nmemb, unsigned int size,
  int (*compar)(const void *, const void *), int max);

/* returns true if the array is sorted (max indicates that the array is sorted
 * in reverse order) */
int heap_issorted(void *base, unsigned int nmemb, unsigned int size,
  int (*compar)(const void *, const void *), int max);

#ifdef __cplusplus
}
#endif

#endif

