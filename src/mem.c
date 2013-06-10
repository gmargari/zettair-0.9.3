/* mem.c implements functions for manipulating memory
 *
 * written nml 2003-09-17
 *
 */

#include "firstinclude.h"

#include "_mem.h"
#include "mem.h"
#include "bit.h"
#include "zstdint.h"

#include <assert.h>

unsigned long int mem_ptrdiff(const void *b1, const void *b2) {
    return MEM_PTRDIFF(b1, b2);
}

void *mem_ptradd(const void *b, unsigned int offset) {
    return MEM_PTRADD(b, offset);
}

unsigned long int mem_ptraddr(const void *b) {
    return MEM_PTRADDR(b);
}

void mem_ntoh(void *dst, const void *src, unsigned int size) {
    MEM_NTOH(dst, src, size);
}

void mem_hton(void *dst, const void *src, unsigned int size) {
    MEM_HTON(dst, src, size);
}

MEM_ALIGN_TYPE(char)
MEM_ALIGN_TYPE(short)
MEM_ALIGN_TYPE(int)
MEM_ALIGN_TYPE(long)
MEM_ALIGN_TYPE(float)
MEM_ALIGN_TYPE(double)

typedef void *ptr;

MEM_ALIGN_TYPE(ptr)

unsigned int mem_align_max() {
    return MEM_ALIGN_MAX;
}

void *mem_align(void *buf, unsigned int align) {
    /* align should be a power of two */
    assert(BIT_POW2(bit_log2(align)) == align);
    return MEM_ALIGN(buf, align);
}

typedef unsigned int word_t;

static struct {
    char first;
    word_t align;
} algn;

void *mem_cpy(void *dst0, const void *src, unsigned int len0) {
    unsigned int align = MEM_PTRDIFF(&algn.align, &algn.first) - 1;
    uint8_t *dst = dst0;
    unsigned int len;

    if ((src == dst) || !len0) {
        return dst0;
    }

    if ((unsigned long int) dst < (unsigned long int) src) {
        /* copy from start of src to end */

        if ((MEM_PTRADDR(dst) & align) == (MEM_PTRADDR(src) & align)) {
            /* they're aligned with respect to word boundary, we can do a little
             * optimisation */
            word_t *wdst;

            /* copy the initial stuff byte by byte */
            while (MEM_PTRADDR(dst) & align) {
                len0--;
                *dst++ = *((const uint8_t*) src);
                src = ((const uint8_t *) src) + 1;
            }

            /* calculate how much can be copied in word size chunks */
            len = len0 / sizeof(word_t);
            len0 = len0 & sizeof(word_t);

            /* copy as much as we can in word size chunks */
            wdst = (word_t *) dst;
            while (len--) {
                *wdst++ = *((const word_t *) src);
                src = ((const word_t *) src) + 1;
            }
            dst = (uint8_t *) wdst;

            /* copy the rest byte by byte */
            while (len0--) {
                *dst++ = *((const uint8_t*) src);
                src = ((const uint8_t *) src) + 1;
            }
        } else {
            /* they're not aligned with respect to word boundary, do it byte by
             * byte */

            while (len0--) {
                *dst++ = *((const uint8_t*) src);
                src = ((const uint8_t *) src) + 1;
            }
        }
    } else {
        /* copy from end of src to start */

        dst = ((uint8_t *) dst) + len0;
        src = ((uint8_t *) src) + len0;

        if ((MEM_PTRADDR(dst) & align) == (MEM_PTRADDR(src) & align)) {
            /* they're aligned with respect to word boundary, we can do a little
             * optimisation */
            word_t *wdst;

            /* copy the initial stuff byte by byte */
            while (MEM_PTRADDR(dst) & align) {
                len0--;
                src = ((const uint8_t *) src) - 1;
                *--dst = *((const uint8_t*) src);
            }

            /* calculate how much can be copied in word size chunks */
            len = len0 / sizeof(word_t);
            len0 = len0 & sizeof(word_t);

            /* copy as much as we can in word size chunks */
            wdst = (word_t *) dst;
            while (len--) {
                src = ((const word_t *) src) - 1;
                *(--wdst) = *((const word_t *) src);
            }
            dst = (uint8_t *) wdst;

            /* copy the rest byte by byte */
            while (len0--) {
                src = ((const uint8_t *) src) - 1;
                *--dst = *((const uint8_t*) src);
            }
        } else {
            /* they're not aligned with respect to word boundary, do it byte by
             * byte */

            while (len0--) {
                src = ((const uint8_t *) src) - 1;
                *--dst = *((const uint8_t *) src);
            }
        }
    }

    return dst0;
}

#if 0
/*
 * sizeof(word) MUST BE A POWER OF TWO
 * SO THAT wmask BELOW IS ALL ONES
 */
typedef int word;       /* "word" used for optimal copy speed */

#define wsize   sizeof(word)
#define wmask   (wsize - 1)

/*
 * Copy a block of memory, handling overlap.
 * This is the routine that actually implements
 * (the portable versions of) bcopy, memcpy, and memmove.
 */
void *
mem_cpy2(void *dst0, const void *src0, unsigned int length) {
    register char *dst = dst0;
    register const char *src = src0;
    register unsigned int t;

    if (length == 0 || dst == src)      /* nothing to do */
        goto done;

    /*
     * Macros: loop-t-times; and loop-t-times, t>0
     */
#define TLOOP(s) if (t) TLOOP1(s)
#define TLOOP1(s) do { s; } while (--t)

    if ((unsigned long)dst < (unsigned long)src) {
        /*
         * Copy forward.
         */
        t = (int)src;   /* only need low bits */
        if ((t | (int)dst) & wmask) {
            /*
             * Try to align operands.  This cannot be done
             * unless the low bits match.
             */
            if ((t ^ (int)dst) & wmask || length < wsize)
                t = length;
            else
                t = wsize - (t & wmask);
            length -= t;
            TLOOP1(*dst++ = *src++);
        }
        /*
         * Copy whole words, then mop up any trailing bytes.
         */
        t = length / wsize;
        TLOOP(*(word *)dst = *(word *)src; src += wsize; dst += wsize);
        t = length & wmask;
        TLOOP(*dst++ = *src++);
    } else {
        /*
         * Copy backwards.  Otherwise essentially the same.
         * Alignment works as before, except that it takes
         * (t&wmask) bytes to align, not wsize-(t&wmask).
         */
        src += length;
        dst += length;
        t = (int)src;
        if ((t | (int)dst) & wmask) {
            if ((t ^ (int)dst) & wmask || length <= wsize)
                t = length;
            else
                t &= wmask;
            length -= t;
            TLOOP1(*--dst = *--src);
        }
        t = length / wsize;
        TLOOP(src -= wsize; dst -= wsize; *(word *)dst = *(word *)src);
        t = length & wmask;
        TLOOP(*--dst = *--src);
    }
done:
    return (dst0);
}
#endif

#ifdef MEM_TEST

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    char buf[BUFSIZ];
    char buf2[BUFSIZ];
    unsigned int loops,
                 i;

    if ((argc != 3) || !sscanf(argv[2], "%u", &loops)) {
        printf("usage: %s [std|opt|bsd] loops\n", *argv);
        return EXIT_FAILURE;
    }

    for (i = 0; i < BUFSIZ; i++) {
        buf[i] = '\0';
    }

    memset(buf2, 1, BUFSIZ);

    if (!strcmp("std", argv[1])) {
        for (i = 0; i < loops; i++) {
            snprintf(buf, BUFSIZ, "%u", i);
            memcpy(buf2, buf, BUFSIZ);
        }
    } else if (!strcmp("opt", argv[1])) {
        for (i = 0; i < loops; i++) {
            snprintf(buf, BUFSIZ, "%u", i);
            mem_cpy(buf2, buf, BUFSIZ);
        }
    } else if (!strcmp("bsd", argv[1])) {
        for (i = 0; i < loops; i++) {
            snprintf(buf, BUFSIZ, "%u", i);
            mem_cpy2(buf2, buf, BUFSIZ);
        }
    } else {
        printf("usage: %s [std|opt|bsd] loops\n", *argv);
        return EXIT_FAILURE;
    }

    if (!memcmp(buf2, buf, BUFSIZ)) {
        return EXIT_SUCCESS;
    } else {
        printf("failed!\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#endif

