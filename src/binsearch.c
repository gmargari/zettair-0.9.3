/* binsearch.c implements a function for binary searching that is much like
 * the standard library qsort
 *
 * written nml 2002-12-12
 *
 */

#include "firstinclude.h"

#include "binsearch.h"

#include "bit.h"

/* FIXME: can be optimised by looping until number of elements between l and r
 * is 0 */

void* binsearch(const void* key, const void* base, size_t nel, 
  size_t width, int (*compar)(const void* one, const void* two)) {
    const char* cbase = base;          /* to do dodgy pointer arithmetic on */
    unsigned int l = 0,                /* left end of search */
                 r = nel - 1,          /* right end of search */
                 m;                    /* middle of search */
    int res;                           /* result of comparison */

    /* indexes must be unsigned int, else risk overflow, but unsigned int gives
     * us problems moving r to the left of m below */

    /* if nel is 0, then r is (unsigned) -1, so check for it */
    if (!nel || !width) {
        return (void*) cbase;
    }

    while (l <= r) {
        m = (r + l) >> 1;
        if ((res = compar(key, cbase + m * width)) > 0) {
            l = m + 1;
        } else if (res < 0) {
            if (m) {
                r = m - 1;
            } else {
                /* avoid underflow of r (and prevention of crossing) */
                return (void*) cbase;
            }
        } else {
            return (void*) (cbase + m * width);
        }
    }

    return (void*) (cbase + l * width);
}

#if 0

/* this version of binsearch contains a possible optimisation based on
 * eliminating multiplication from the loop, but it doesn't seem to work, since
 * you still need some multiplication (although its only by 0 or 1).  The below
 * implementation has bugs. */

void* binsearch(const void* key, const void* base, size_t nel, 
  size_t width, int (*compar)(const void* one, const void* two)) {
    const char *l = base,           /* left end of search */
               *r,                  /* right end of search */
               *m;                  /* middle of search */
    int res,                        /* result of comparison */
        odd;                        /* whether last division was odd */
    unsigned int halfwidth;         /* half of width */

    /* check for degenerate cases */
    if (!nel || !width) {
        return (void*) base;
    }

    halfwidth = BIT_DIV2(width, 1);

    /* FIXME: more... initialise r to the end of the array */
    for (r = l + (nel - 1) * width; nel; nel = BIT_DIV2(nel - 1, 1)) {

        odd = BIT_MOD2(nel, 1);

        /* the middle is halfway in between l and r, (l + (r - l) / 2),
         * except that if theres an odd number of elements between them we 
         * have to subtract the half an element that would otherwise screw up 
         * our calculations (this is the price of not using multiplication) */
        m = l + BIT_DIV2(r - l, 1);
        
        if (odd) {
            /* correct for incorrect division due to oddness */
            m -= halfwidth;           

            /* compare them */
            res = compar(key, m);

            if (res > 0) {
                /* if the key is greater than the middle, we need to shift the 
                 * left bounds past the middle */
                l = m + width;
            } else if (res < 0) {
                /* if the key is less than the middle, we need to shift the 
                 * right bounds past the middle */
                r = m - width;
            } else {
                return (void*) m;
            }
        } else {
            /* compare them */
            res = compar(key, m);

            if (res > 0) {
                /* if the key is greater than the middle, we need to shift the 
                 * left bounds past the middle */
                l = m + width;
                nel -= 1;
            } else if (res < 0) {
                /* if the key is less than the middle, we need to shift the 
                 * right bounds past the middle */
                r = m - width;
            } else {
                return (void*) m;
            }
        }
    }

    return (void*) l;
}
#endif

