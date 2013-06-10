/* heap.c implements an efficient partial sort using a heap.
 *
 * heaps are a classic computer science structure.  If you don't know what one
 * is i refer you to The Art of Computer Programming, Volume 3, Donald E Knuth.
 * For a less in-depth but more readable resource you could check out the 
 * Wikipedia entry on heaps (http://www.wikipedia.org/wiki/Heap), binary heaps
 * (http://www.wikipedia.org/wiki/Binary_heap), and 
 * heapsort (http://www.wikipedia.org/wiki/Heapsort).
 *  
 * I'll summarise the heap structure so that i can explain some potential
 * optimisations to this code.  A heap is a tree having the property that each
 * node is more extreme (greater than or less than, depending whether it is a
 * minheap or a maxheap) or equal to its children nodes.  This code implements a
 * minheap, so from now on i'll just refer to parent nodes being <= their 
 * children.  The minheap property ensures that the (possibly equal) smallest 
 * node is at the root of the tree, so its an O(1) operation to get the 
 * smallest node from a heap.  Restoring the heap property to a heap minus its
 * smallest element takes O(logN) time.  Heaps can be constructed in O(N) time 
 * given any old bunch of elements.  
 *
 * Heapsort is essentially these 4 steps:
 *    1) make a heap from the elements
 *    2) for each element in heap
 *    3)   remove smallest element
 *    4)   restore heap property to remaining heap
 *
 * An optimisation is to build a heap of the number of elements that you want
 * sorted.  Once your heap has been constructed you can compare each of the
 * other elements to the largest element on the heap, swapping them if the
 * candidate item is smaller.  Once this process is completed you have the
 * smallest N items in the heap, which you can remove in order largest to
 * smallest to sort them.  Note that to perform the same sort as a unoptimised
 * minheap, this requires a maxheap.
 *
 * we can see from the above complexity measures that this is an 
 * O(N) (to build heap) + O(NlogN) (to reheapify N times) = O(NlogN) operation.
 * Heapsort is typically not as fast as quicksort, but its worst case is
 * O(NlogN), which is far better than quicksort's O(N^2) worst case.
 * Heapsort can also be made to work in-place, with only one byte of extra
 * storage required (which this implementation does).  It also has the
 * interesting property that you can modify step 2 to only iterate a set number
 * of times, which will give you what is known as a partial sort (where you
 * extract the M smallest values in sorted order from your heap).
 *
 * Binary heaps (where each node has two children) can be represented by an
 * array, (numbered from 1 ... N for convenience) where each node n has children
 * n * 2 and n * 2 + 1.  E.g. a heap (ASCII art from wikipedia)
 * 
 *         1
 *        / \
 *       /   \
 *      4     3
 *     / \   / \
 *    5   6 7   8
 *
 * could be represented as array [1, 4, 3, 5, 6, 7, 8].  
 *
 * Given an array of elements, we can construct a heap from it.  Start at
 * element floor(N / 2) (since this is the first element that can have
 * children).  Find out which child is smaller (the first node might not have 
 * both children) and compare the smaller child with the parent.  If the child
 * is smaller than the parent, swap them.  After a swap, you need to recursively
 * repeat this process with the subheap formed by the newly swapped child and 
 * its children to ensure the heap property.  After finishing with node floor(N
 * / 2), repeat with the previous node (floor(N / 2) - 1) until the first node
 * has been heapified as well.  You now have a heap.
 *
 * The second stage of heapsort involves repeatedly removing the smallest
 * element and reheapifying.  Reheapification is exactly the same recursive
 * process as we applied to the first half of the array to make the heap, and is
 * known as siftup or siftdown.  With our array representation, we swap the 
 * smallest element with the last element and shrink the size of the heap by 
 * one.  As the heap shrinks, we will accumulate a set of the smallest 
 * elements, in order, behind the heap (which is exactly what we want).  So, 
 * with an example: 
 *
 * start:                       [1, 4, 3, 5, 6, 7, 8]
 * swap smallest and last:      [8, 4, 3, 5, 6, 7], 1 
 * siftdown:                    [3, 4, 8, 5, 6, 7], 1
 *                              [3, 4, 7, 5, 6, 8], 1
 * swap smallest and last:      [8, 4, 7, 5, 6], 3, 1 
 * siftdown:                    [4, 8, 7, 5, 6], 3, 1
 *                              [4, 5, 7, 8, 6], 3, 1
 * swap smallest and last:      [6, 5, 7, 8], 4, 3, 1
 * siftdown:                    [5, 6, 7, 8], 4, 3, 1
 * swap smallest and last:      [8, 6, 7], 5, 4, 3, 1
 * siftdown:                    [6, 8, 7], 5, 4, 3, 1
 * swap smallest and last:      [7, 8], 6, 5, 4, 3, 1
 * siftdown:                    [7, 8], 6, 5, 4, 3, 1
 * swap smallest and last:      8, 7, 6, 5, 4, 3, 1         (finished)
 *
 * note how the heap shrunk and the sorted array at the back grows.  
 *
 * This code implements a basic heapsort.  It sorts about twice as slowly as the
 * standard library qsort on my machine at the moment.  I believe that this is
 * acceptible performance for the moment.  There are a number of optimisations
 * that should make it faster:
 *
 *   - improved siftup algorithm
 *   - altering code to be a ternary heap
 *
 * During the siftup algorithm, instead of finding the smallest child and
 * comparing it to the parent, you can find the smallest child and swap it with
 * the parent.  This propagates the parent to the bottom of the heap, from where
 * you have to compare it with its (new) parents and swap it back up to find its
 * place in the heap.  While this involves more copying of elements, it
 * apparently saves comparisons, and should improve the performance of this heap
 * (particularly with a generic comparison function pointer and in-line
 * swapping).  The downside to this is that i can't find a way to do it with
 * fast math.
 *
 * A ternary heap (where each parent has 3 children) is apparently faster than a
 * binary heap (and fast math can still be done: x * 3 == (x << 1) + x).
 *
 * Another algorithmic improvement might be to implement a weak-heap as
 * described by Dr Stefan Edelkamp in his master's thesis: Weak-Heapsort: A Fast * Sorting Algorithm, Institute of Computer Science, University of Dortmund.
 * Unfortunately its in german, and there doesn't seem to be a lot of english
 * literature on it.  It utilises a weak heap, which is a tree where each parent
 * is more extreme or equal to the elements in its right subtree, and the root
 * has no left subtree (this constraint is weaker than the heap constraint, but
 * is still sufficient to sort the elements, apparently).  Unfortunately it
 * requires an additional array or r bits, which indicate which elements an
 * element considers its right subtree.
 *
 * written nml 2003-06-03
 *
 */

#include "firstinclude.h"

#include "heap.h"

#include "bit.h"
#include "def.h"  /* for DEAR_DEBUG */

#include "zstdint.h"

#include <stdlib.h>

/* internal function to determine whether an array has the heap property */
int heap_isheap(void *base, unsigned int nmemb, unsigned int size, 
  int (*cmp)(const void *, const void *), int max) {
    char *cbase = base;
    unsigned int pos = BIT_DIV2(nmemb, 1) - 1,
                 lchild,
                 rchild;
    int mul;

    if (max) {
        mul = -1;
    } else {
        mul = 1;
    }

    /* heap property: every node is less than (or equal to) its children */

    if (nmemb < 2) {
        return 1;
    }

    while (pos + 1 > 0) {
        lchild = pos * 2 + 1;
        rchild = lchild + 1;

        if (((cmp(&cbase[pos * size], &cbase[lchild * size]) * mul) > 0) 
          || ((rchild < nmemb) 
            && ((cmp(&cbase[pos * size], &cbase[rchild * size]) * mul) > 0))) { 
            return 0;
        }

        pos--;
    }

    return 1;
}

/* internal function to determine whether an array is sorted */
int heap_issorted(void *base, unsigned int nmemb, unsigned int size, 
  int (*cmp)(const void *, const void *), int max) {
    char *pos,
         *next,
         *end = ((char *) base) + nmemb * size;
    int mul;

    if (max) {
        mul = -1;
    } else {
        mul = 1;
    }

    /* sorted property: every node is smaller than or equal to the next */

    if (nmemb < 2) {
        return 1;
    }

    for (pos = base, next = pos + size; next < end; pos = next, next += size) {
        if ((cmp(next, pos) * mul) < 0) {
            return 0;
        }
    }

    return 1;
}

/* macro to swap two data elements in place, one byte at a time.  I tried
 * replacing this with a duff device for optimisation, but it had no noticeable
 * effect. */
#define SWAP(one, two, size)                                                  \
    do {                                                                      \
        uint8_t SWAP_tmp,                                                     \
               *SWAP_tone = (uint8_t *) one,                                  \
               *SWAP_ttwo = (uint8_t *) two;                                  \
        unsigned int SWAP_size = size;                                        \
        do {                                                                  \
            SWAP_tmp = *SWAP_tone;                                            \
            *SWAP_tone = *SWAP_ttwo;                                          \
            *SWAP_ttwo = SWAP_tmp;                                            \
            ++SWAP_tone;                                                      \
            ++SWAP_ttwo;                                                      \
        } while (--SWAP_size);                                                \
    } while (0)

/* sift an element that is out of heap order down the heap (from root to leaf 
 * if necessary) */
static void *heap_siftdown(char *element, char *endel, 
  unsigned int size, unsigned int diff, 
  int (*cmp)(const void *one, const void *two)) {
    char *lchild = element + diff;          /* note that the right child is one 
                                             * array element greater than 
                                             * lchild */
    unsigned int right;                     /* whether right child is smaller */

    /* perform all heapifications where both children exist */
    while (lchild < endel) {
        /* compare left and right children */
        right = (cmp(lchild, lchild + size) > 0) * size;

        /* if current element is less extreme than more extreme child ... */
        if (cmp(element, lchild + right) > 0) {
            /* swap current element with greatest child */
            SWAP(element, lchild + right, size);
            element = lchild + right;

            /* calculate new child positions */
            diff = BIT_MUL2(diff, 1) + right;
            lchild = element + diff;
        } else {
            /* element is at its final position */
            break;
        }
    }

    /* perform (possible) heapification where only left child exists */
    if (lchild == endel) {
        /* if left child is more extreme than current element ... */
        if (cmp(element, endel) > 0) {
            /* swap current element with last element */
            SWAP(element, endel, size);
            return endel;
        }
    } 

    return element;
}

/* sift an element that is out of heap order up the heap (from leaf to root) */
static void *heap_siftup(char *root, unsigned int element, 
  unsigned int size, int (*cmp)(const void *one, const void *two)) {
    char *curr = root,
                  *parent;

    while (element) {
        /* compare with parent */
        curr = root + size * element;
        element = BIT_DIV2(element - 1, 1);
        parent = root + size * element;
        if (cmp(curr, parent) < 0) {
            /* swap element and parent and repeat */
            SWAP(curr, parent, size);
        } else {
            break;
        }
    }

    return curr;
}

/* sift an element that is out of heap order down the heap (from root to leaf 
 * if necessary) */
static void *maxheap_siftdown(char *element, char *endel, 
  unsigned int size, unsigned int diff, 
  int (*cmp)(const void *one, const void *two)) {
    char *lchild = element + diff;          /* note that the right child is one 
                                             * array element greater than 
                                             * lchild */
    unsigned int right;                     /* whether right child is smaller */

    /* perform all heapifications where both children exist */
    while (lchild < endel) {
        /* compare left and right children */
        right = (cmp(lchild, lchild + size) < 0) * size;

        /* if current element is less extreme than more extreme child ... */
        if (cmp(element, lchild + right) < 0) {
            /* swap current element with greatest child */
            SWAP(element, lchild + right, size);
            element = lchild + right;

            /* calculate new child positions */
            diff = BIT_MUL2(diff, 1) + right;
            lchild = element + diff;
        } else {
            /* element is at its final position */
            break;
        }
    }

    /* perform (possible) heapification where only left child exists */
    if (lchild == endel) {
        /* if left child is more extreme than current element ... */
        if (cmp(element, endel) < 0) {
            /* swap current element with last element */
            SWAP(element, endel, size);
            return endel;
        }
    } 

    return element;
}

void heap_heapify(void *base, unsigned int nmemb, unsigned int size, 
  int (*cmp)(const void *, const void *)) {
    char *middle, 
         *element,
         *lchild,
         *endel;
    unsigned int right;

    if (nmemb < 2) {
        /* arrays of size 1 or 0 are degenerate heaps already */
        return;
    }

    /* initialise positions */
    element = ((char *) base) + size * (BIT_DIV2(nmemb, 1) - 1);
    middle = element;
    lchild = element + (size * BIT_DIV2(nmemb, 1));
    endel = ((char *) base) + size * (nmemb - 1);

    /* do (optional) first heapification, where only left child exists */
    if (lchild == endel) {
        if (cmp(element, lchild) > 0) {
            /* child is more extreme, swap for parent (no siftdown because the
             * child can't have children of its own) */
            SWAP(element, lchild, size);
        }
        element -= size;
        lchild -= BIT_MUL2(size, 1);
    }

    /* perform all heapifications where we know that we don't have to sift down,
     * because the child subheap will be a single element */
    while (lchild > middle) {
        /* compare left and right children */
        right = (cmp(lchild, lchild + size) > 0) * size;

        /* if current element is less extreme than the most extreme child ... */
        if (cmp(element, lchild + right) > 0) {
            /* swap current element with smallest child */
            SWAP(element, lchild + right, size);
        }
        element -= size;
        lchild -= BIT_MUL2(size, 1);
    }

    /* do all subsequent heapifications, where both children exist and we have
     * to restore the heap property to non-trivial subheaps */
    while (element >= (char *) base) {
        /* compare left and right children */
        right = (cmp(lchild, lchild + size) > 0) * size;

        /* if current element is less extreme than the most extreme child ... */
        if (cmp(element, lchild + right) > 0) {
            /* swap current element with smallest child and then restore heap
             * property to subheap */
            SWAP(element, lchild + right, size);
            heap_siftdown(lchild + right, endel, size, 
              BIT_MUL2(lchild - element, 1) + right, cmp);
        }
        element -= size;
        lchild -= BIT_MUL2(size, 1);
    }

    /* if debugging is on, check the heap order */
    if (DEAR_DEBUG) {
        assert(heap_isheap(base, nmemb, size, cmp, 0));
    }

    return;
}

static void maxheap_heapify(void *base, unsigned int nmemb, unsigned int size, 
  int (*cmp)(const void *, const void *)) {
    char *middle,
         *element,
         *lchild,
         *endel;
    unsigned int right;

    if (nmemb < 2) {
        /* arrays of size 1 or 0 are degenerate heaps already */
        return;
    }

    /* initialise positions */
    element = ((char *) base) + size * (BIT_DIV2(nmemb, 1) - 1);
    middle = element;
    lchild = element + (size * BIT_DIV2(nmemb, 1));
    endel = ((char *) base) + size * (nmemb - 1);

    /* do (optional) first heapification, where only left child exists */
    if (lchild == endel) {
        if (cmp(element, lchild) < 0) {
            /* child is more extreme, swap for parent (no siftdown because the
             * child can't have children of its own) */
            SWAP(element, lchild, size);
        }
        element -= size;
        lchild -= BIT_MUL2(size, 1);
    }

    /* perform all heapifications where we know that we don't have to sift down,
     * because the child subheap will be a single element */
    while (lchild > middle) {
        /* compare left and right children */
        right = (cmp(lchild, lchild + size) < 0) * size;

        /* if current element is less extreme than the most extreme child ... */
        if (cmp(element, lchild + right) < 0) {
            /* swap current element with smallest child */
            SWAP(element, lchild + right, size);
        }
        element -= size;
        lchild -= BIT_MUL2(size, 1);
    }

    /* do all subsequent heapifications, where both children exist and we have
     * to restore the heap property to non-trivial subheaps */
    while (element >= (char *) base) {
        /* compare left and right children */
        right = (cmp(lchild, lchild + size) < 0) * size;

        /* if current element is less extreme than the most extreme child ... */
        if (cmp(element, lchild + right) < 0) {
            /* swap current element with smallest child and then restore heap
             * property to subheap */
            SWAP(element, lchild + right, size);
            maxheap_siftdown(lchild + right, endel, size, 
              BIT_MUL2(lchild - element, 1) + right, cmp);
        }
        element -= size;
        lchild -= BIT_MUL2(size, 1);
    }

    /* if debugging is on, check the heap order */
    if (DEAR_DEBUG) {
        assert(heap_isheap(base, nmemb, size, cmp, 1));
    }

    return;
}

void *heap_pop(void *base, unsigned int *nmemb, unsigned int size, 
  int (*cmp)(const void *one, const void *two)) {
    char *end;

    if (*nmemb) {
        (*nmemb)--;
        end = ((char *) base + *nmemb * size);

        /* swap out root element and restore heap condition */
        SWAP(base, end, size);
        heap_siftdown(base, end - size, size, size, cmp);

        return end;
    } else {
        return NULL;
    }
}

void heap_sort(void *base, unsigned int nmemb, unsigned int sort, 
  unsigned int size, int (*cmp)(const void *, const void *)) {
    char *element,
         *endel,
         *heap_endel;

    /* we don't have to sort 'degenerate' arrays */
    if (!sort || !size || (nmemb < 2)) {
        return;
    }

    /* can only sort as many elements as we are given */
    sort = (nmemb < sort) ? nmemb : sort;

    /* create a max heap so we can collect the sort lowest elements (knowing
     * what the highest elements in the lowest element set allows us to do 
     * this) */
    maxheap_heapify(base, sort, size, cmp);

    if (DEAR_DEBUG) {
        assert(heap_isheap(base, sort, size, cmp, 1));
    }

    /* compare each of the other elements with the largest element currently in
     * the heap, replacing the largest element in the heap if its smaller */
    endel = ((char *) base) + (nmemb - 1) * size;
    element = ((char *) base) + sort * size;
    heap_endel = element - size;
    for (; element <= endel; element += size) {
        if (cmp(element, base) < 0) {
            /* swap root out */
            SWAP(element, base, size);

            /* reheapify */
            maxheap_siftdown(base, heap_endel, size, size, cmp);
        }
    }

    /* extract elements from the heap in sorted order */
    while (heap_endel > (char *) base) {
        /* swap out root element and restore heap condition */
        SWAP(base, heap_endel, size);
        heap_endel -= size;
        maxheap_siftdown(base, heap_endel, size, size, cmp);
    }

    /* if debugging is on, check the sort order */
    if (DEAR_DEBUG) {
        assert(heap_issorted(base, sort, size, cmp, 0));
    }

    /* the number of elements they requested should now be sorted */
    return;
}

void *heap_replace(void *base, unsigned int nmemb, unsigned int size,
  int (*cmp)(const void *, const void *), void *element) {
    char *end;

    if (nmemb) {
        end = ((char *) base + nmemb * size);

        /* swap out root element and restore heap condition */
        SWAP(base, element, size);
        return heap_siftdown(base, end - size, size, size, cmp);
    } else {
        return NULL;
    }
}

void *heap_peek(void *base, unsigned int nmemb, unsigned int size) {
    /* most extreme is at root array element */
    if (nmemb) {
        return base;
    } else {
        return NULL;
    }
}

void *heap_insert(void *base, unsigned int *nmemb, unsigned int size,
  int (*cmp)(const void *, const void *), void *element) {
    /* put in last place, siftup */
    memmove(((char *) base) + size * *nmemb, element, size);
    return heap_siftup(base, (*nmemb)++, size, cmp);
}

void *heap_remove(void *base, unsigned int *nmemb, unsigned int size,
  int (*cmp)(const void *, const void *), void *remove) {
    /* linear search for element, copy last element into its place, siftdown */
    char *element = base,
         *endel = element + (size * *nmemb) - size;

    for (element = base; element <= endel; element += size) {
        if (cmp(element, remove) == 0) {
            SWAP(element, endel, size);
            (*nmemb)--;
            /* note that we use endel - size for the end element because we've
             * just removed one element from the heap */
            return heap_siftdown(element, endel - size,
              size, element - ((char *) base) + size, cmp);
        } 
    }

    return NULL;
}

/* module test code */
#ifdef HEAP_TEST
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

static void printarr(FILE *output, unsigned int *arr, unsigned int elements) {
    unsigned int i;

    fprintf(output, "\n");
    for (i = 0; i < elements; i++) {
        fprintf(output, "%u %u\n", i, arr[i]);
    }
    fprintf(output, "\n");
    return;
}

static void fillarr(unsigned int *arr, unsigned int elements, 
  unsigned int seed) {
    unsigned int i;

    for (srand(seed), i = 0; i < elements; i++) {
        arr[i] = rand() % (elements << 1);
    }

    return;
}

static int cmp_int(const void *vone, const void *vtwo) {
    const unsigned int *one = vone,
                       *two = vtwo;

    if (*two < *one) {
        return 1;
    } else if (*one < *two) {
        return -1;
    } else {
        return 0;
    }
}

int main(int argc, char **argv) {
    unsigned int elements;
    unsigned int seed;
    unsigned int *arr,
                 i = 0;

    seed = time(NULL);

    if ((argc == 2) || (argc == 3)) {
        elements = strtol(argv[1], NULL, 0);
    } else {
        fprintf(stderr, "usage: %s elements [seed]\n", *argv);
        return EXIT_FAILURE;
    }

    if (argc == 3) {
        seed = strtol(argv[2], NULL, 0);
    }

    if (!(arr = malloc(sizeof(*arr) * elements))) {
        fprintf(stderr, "can't get memory\n");
    }

    fillarr(arr, elements, seed);

    /* create heap */
    for (i = 1; i < elements;) {
        heap_insert(arr, &i, sizeof(*arr), cmp_int, &arr[i]);
    }

    if (!heap_isheap(arr, elements, sizeof(*arr), cmp_int, 0)) {
        printarr(stderr, arr, elements);
        printf("arr is not a heap (seed %u)!\n", seed);
    } 

    /* sort by popping elements off heap */
    for (i = elements; i > 1;) {
        heap_pop(arr, &i, sizeof(*arr), cmp_int);
    }

    if (!heap_issorted(arr, elements, sizeof(*arr), cmp_int, 1)) {
        printarr(stderr, arr, elements);
        printf("arr is not sorted (seed %u)!\n", seed);
    } 

    free(arr);

    return EXIT_SUCCESS;
}

#endif

