/* bucket.c implements a structure that manages short inverted lists by 
 * keeping them in fixed size buckets.
 *
 * actually, since i need to do some experimentation to find the best bucket
 * design, it will probably implement a number of structures.  There are a
 * number of good reasons for having different bucket structures around,
 * including specialising to match the current architecture endianness and
 * reducing costs for fixed-length entries and keys.
 *
 * The first bucket design that we will use is as follows:
 *
 * +-------------------------------+
 * |num|t1p|t1l|t2p|t2l|           |
 * +-------------------------------+
 * |   |xxxxx,term2|xxxxxxxxx,term1|
 * +-------------------------------+
 *
 * term vectors are stored in lexographic order with the corresponding term 
 * immediately afterward. (shown as xxxx,term1 etc).  
 * The term length is stored in the directory as t1l, t2l etc.  
 * The term pointers (t1p, t2p etc) point to the start of the entry for that 
 * term, where entries are kept packed to the back of the bucket (to allow 
 * easy appending of new entries).  All entries in the directory (shown on top 
 * row) are fixed length numbers of size 16 bits (stored in big-endian format),
 * which allows quick lookup given the index of the term.  
 * The term pointers combined with knowledge of the size of the bucket 
 * and the term lengths can be used to deduce the length of each vector.  
 *
 * Bucket design 2 is the same as one, except that all entries have uniform
 * length and are stored (once) at the start of the bucket. 
 *
 * +-------------------------------+
 * |num|size|t1p|t2p|              |
 * +-------------------------------+
 * |   |xxxxx,term2|xxxxxxxxx,term1|
 * +-------------------------------+
 *
 * written nml 2003-10-06
 *
 */

#include "firstinclude.h"

#include "bucket.h"

#include "_mem.h"

#include "bit.h"
#include "mem.h"
#include "str.h"      /* for str_ncmp use in assert */
#include "vec.h"
#include "zstdint.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* return address of entries (it's at the start of memory) for bucket designs
 * 1 and 2 */
#define B1_ENTRIES_ADDR(base) (base)
#define B2_ENTRIES_ADDR(base) (base)

/* return address of the pointer for entries for bucket designs 1 and 2 */
#define B1_PTR(entry) (sizeof(uint16_t) * (2 * (entry) + 1))
#define B1_PTR_ADDR(base, entry) (((char *) base) + B1_PTR(entry))
#define B2_PTR(entry) (sizeof(uint16_t) * ((entry) + 2))
#define B2_PTR_ADDR(base, entry) (((char *) base) + B2_PTR(entry))

/* return address of the size for an entry for bucket designs 1 and 2 */
#define B1_SIZE(entry) (sizeof(uint16_t) * (2 * (entry) + 2))
#define B1_SIZE_ADDR(base, entry) (((char *) base) + B1_SIZE(entry))
#define B2_SIZE(entry) (sizeof(uint16_t))
#define B2_SIZE_ADDR(base, entry) (((char *) base) + B1_SIZE(entry))

/* macro to read a vbyte number from cmem (which is incremented over the number)
 * into n.  Note that cmem must be a uchar * */
#define GETVBYTE(cmem, n)                                                     \
    if (1) {                                                                  \
        unsigned int GETVBYTE_count = 0;                                      \
                                                                              \
        *(n) = 0;                                                             \
        while ((STR_FROM_CHAR(*(cmem)) >= 128)) {                             \
            *(n) |= (*(cmem) & 0x7f) << GETVBYTE_count;                       \
            GETVBYTE_count += 7;                                              \
            (cmem)++;                                                         \
        }                                                                     \
        *(n) |= STR_FROM_CHAR(*(cmem)) << GETVBYTE_count;                     \
        (cmem)++;                                                             \
    } else 

/* macro to put a vbyte number to cmem (which is incremented over the number)
 * from n (which is destroyed by the operation).  Note that cmem must be 
 * a uchar * */
#define PUTVBYTE(cmem, n)                                                  \
    if (1) {                                                                  \
    while ((n) >= 128) {                                                      \
            *(cmem) = ((n) & 0x7f) | 0x80;                                    \
            (n) >>= 7;                                                        \
            (cmem)++;                                                         \
        }                                                                     \
        *(cmem) = ((unsigned char) n);                                        \
        (cmem)++;                                                             \
    } else

int bucket_new(void *mem, unsigned int bucketsize, int strategy) {
    if (bucketsize >= UINT16_MAX) {
        return 0;
    }

    /* this isn't strictly necessary, but means that everything is guaranteed
     * to be initialised (even the space we don't use) */
    memset(mem, 0, bucketsize);

    switch (strategy) {
    case 1:
    case 2:
        return 1;

    default:
        return 0;
    };
}

int bucket_sorted(int strategy) {
    switch (strategy) {
    case 1:
    case 2:
        return 1;

    default:
        return 0;
    }
}

unsigned int bucket_entries(void *mem, unsigned int bucketsize, int strategy) {
    uint16_t entries;

    switch (strategy) {
    case 1:
    case 2:
        /* read number of entries */
        MEM_NTOH(&entries, mem, sizeof(entries));
        return entries;
        break;

    default:
        assert(0);
        return 0;
        break;
    }
}

unsigned int bucket_utilised(void *mem, unsigned int bucketsize, int strategy) {
    unsigned int i;
    uint16_t entries,
             length,
             utilised = 0;

    switch (strategy) {
    case 1:
        /* read number of entries */
        MEM_NTOH(&entries, mem, sizeof(entries));

        /* read each entry */
        for (i = 0; i < entries; i++) {
            MEM_NTOH(&length, B1_SIZE_ADDR(mem, i), sizeof(length));
            utilised += length;
        }
        return utilised;
        break;

    case 2:
        /* read number of entries */
        MEM_NTOH(&entries, mem, sizeof(entries));

        /* read each entry */
        for (i = 0; i < entries; i++) {
            MEM_NTOH(&length, B2_SIZE_ADDR(mem, i), sizeof(length));
            utilised += length;
        }
        return utilised;
        break;

    default:
        assert(0);
        return 0;
        break;
    }
}

unsigned int bucket_string(void *mem, unsigned int bucketsize, int strategy) {
    unsigned int i;
    uint16_t entries,
             prevptr = bucketsize,
             ptr,
             length,
             string = 0;
 
    switch (strategy) {
    case 1:
        /* read number of entries */
        MEM_NTOH(&entries, mem, sizeof(entries));

        /* read each entry */
        for (i = 0; i < entries; i++) {
            MEM_NTOH(&ptr, B1_PTR_ADDR(mem, i), sizeof(ptr));
            MEM_NTOH(&length, B1_SIZE_ADDR(mem, i), sizeof(length));

            string += (prevptr - ptr) - length;

            prevptr = ptr;
        }
        return string;
        break;

    case 2:
        /* read number of entries */
        MEM_NTOH(&entries, mem, sizeof(entries));

        /* read each entry */
        for (i = 0; i < entries; i++) {
            MEM_NTOH(&ptr, B2_PTR_ADDR(mem, i), sizeof(ptr));
            MEM_NTOH(&length, B2_SIZE_ADDR(mem, i), sizeof(length));

            string += (prevptr - ptr) - length;

            prevptr = ptr;
        }
        return string;
        break;

    default:
        assert(0);
        return 0;
        break;
    }
}

unsigned int bucket_overhead(void *mem, unsigned int bucketsize, int strategy) {
    uint16_t entries;

    switch (strategy) {
    case 1:
    case 2:
        /* read number of entries */
        MEM_NTOH(&entries, mem, sizeof(entries));

        return entries * 2 * sizeof(uint16_t) + sizeof(entries);
        break;

    default:
        assert(0);
        return 0;
        break;
    }
}

unsigned int bucket_unused(void *mem, unsigned int bucketsize, int strategy) {
    uint16_t entries,
             lastptr;
 
    switch (strategy) {
    case 1:
        /* read number of entries */
        MEM_NTOH(&entries, mem, sizeof(entries));

        /* read last entry */
        if (entries) {
            MEM_NTOH(&lastptr, B1_PTR_ADDR(mem, entries - 1), sizeof(lastptr));

            return lastptr - B1_PTR(entries);
        } else {
            return bucketsize - sizeof(entries);
        }
        break;

    case 2:
        /* read number of entries */
        MEM_NTOH(&entries, mem, sizeof(entries));

        /* read last entry */
        if (entries) {
            MEM_NTOH(&lastptr, B2_PTR_ADDR(mem, entries - 1), sizeof(lastptr));

            return lastptr - B2_PTR(entries);
        } else {
            return bucketsize - sizeof(entries);
        }
        break;

    default:
        assert(0);
        return 0;
        break;
    }
}

/* functions for strategy 1 */

/* binary searches a bucket for a term, returning the largest thing that is less
 * than or equal to the given term (this property, which the usual binary search
 * must be modified to provide, is important because we can then use it for
 * insertion sorting as well as searching) */
static unsigned int bucket1_binsearch(void *mem, unsigned int bucketsize, 
  unsigned int entries, const char *term, unsigned int termlen) {
    unsigned int l,
                 r,
                 m,
                 i,
                 len,
                 tlen;
    uint16_t ptr,
             prevptr,
             size;

    assert(entries);

    l = 0;
    r = entries - 1;

    while (l < r) {
        m = BIT_DIV2((r + l + 1), 1);

        /* decode m ptr */
        MEM_NTOH(&ptr, B1_PTR_ADDR(mem, m), sizeof(ptr));
        MEM_NTOH(&size, B1_SIZE_ADDR(mem, m), sizeof(ptr));

        /* decode next pointer, so we know where this one ends */
        if (m) {
            MEM_NTOH(&prevptr, B1_PTR_ADDR(mem, m - 1), sizeof(prevptr));

            tlen = prevptr - ptr - size;
            len = (tlen > termlen) ? termlen : tlen;

            for (i = 0; i < len; i++) {
                if (term[i] < ((char *) mem)[i + ptr + size]) {
                    r = m - 1;
                    break;
                } else if (term[i] > ((char *) mem)[i + ptr + size]) {
                    l = m;
                    break;
                }
            }

            /* exact match of prefixes */
            if (i == len) {
                if (termlen == tlen) {
                    return m;
                } else if (termlen < tlen) {
                    r = m - 1;
                } else {
                    l = m;
                }
            }
        } else {
            /* m is first entry */
            prevptr = bucketsize;

            tlen = prevptr - ptr - size;
            len = (tlen > termlen) ? termlen : tlen;

            for (i = 0; i < len; i++) {
                if (term[i] < ((char *) mem)[i + ptr + size]) {
                    /* no exact match, act now to prevent underflow */
                    return 0;
                    break;
                } else if (term[i] > ((char *) mem)[i + ptr + size]) {
                    l = m;
                    break;
                }
            }

            /* exact match of prefixes */
            if (i == len) {
                if (termlen <= tlen) {
                    /* no match, act now to prevent underflow */
                    return 0;
                } else {
                    l = m;
                }
            }
        }
    }

    /* no exact match */
    return l;
}

void *bucket1_find(void *mem, unsigned int bucketsize, const char *term, 
  unsigned int termlen, unsigned int *veclen, unsigned int *idx) {
    unsigned int index,
                 i;
    uint16_t entries,
             size,
             ptr,
             prevptr;
             
    /* read number of entries */
    MEM_NTOH(&entries, B1_ENTRIES_ADDR(mem), sizeof(entries));

    /* handle 0 entries case */
    if (!entries) {
        return NULL;
    }

    index = bucket1_binsearch(mem, bucketsize, entries, term, termlen);

    /* decode ptr */
    MEM_NTOH(&ptr, B1_PTR_ADDR(mem, index), sizeof(ptr));
    MEM_NTOH(&size, B1_SIZE_ADDR(mem, index), sizeof(ptr));

    /* decode next pointer, so we know where this one ends */
    if (index) {
        MEM_NTOH(&prevptr, B1_PTR_ADDR(mem, index - 1), sizeof(prevptr));
    } else {
        prevptr = bucketsize;
    }

    assert(prevptr >= ptr);
    if (((unsigned int) prevptr - ptr) - size == termlen) {
        for (i = 0; i < termlen; i++) {
            if (term[i] != ((char *) mem)[i + ptr + size]) {
                return NULL;
            }
        }

        *veclen = size;
        if (idx) {
            *idx = index;
        }
        return ((char *) mem) + ptr;
    }

    return NULL;
}

void *bucket1_search(void *mem, unsigned int bucketsize, const char *term, 
  unsigned int termlen, unsigned int *veclen, unsigned int *idx) {
    unsigned int index;
    uint16_t entries,
             size,
             ptr,
             prevptr;
             
    /* read number of entries */
    MEM_NTOH(&entries, B1_ENTRIES_ADDR(mem), sizeof(entries));

    /* handle 0 entries case */
    if (!entries) {
        return NULL;
    }

    index = bucket1_binsearch(mem, bucketsize, entries, term, termlen);

    /* decode ptr */
    MEM_NTOH(&ptr, B1_PTR_ADDR(mem, index), sizeof(ptr));
    MEM_NTOH(&size, B1_SIZE_ADDR(mem, index), sizeof(ptr));

    /* decode next pointer, so we know where this one ends */
    if (index) {
        MEM_NTOH(&prevptr, B1_PTR_ADDR(mem, index - 1), sizeof(prevptr));
    } else {
        prevptr = bucketsize;
    }

    if (idx) {
        *idx = index;
    }
    *veclen = size;
    return ((char *) mem) + ptr;
}

void *bucket1_realloc_at(void *mem, unsigned int bucketsize, 
  unsigned int index, unsigned int newsize, int *toobig) {
    unsigned int termlen;
    uint16_t size,
             lastaddr,
             entries,
             new_bucket_size,
             ptr,
             prevptr,
             movesize;

    new_bucket_size = bucketsize - sizeof(uint16_t);

    /* read number of entries */
    MEM_NTOH(&entries, B1_ENTRIES_ADDR(mem), sizeof(entries));

    /* handle 0 entries case */
    if (!entries || (index >= entries)) {
        *toobig = 0;
        return NULL;
    }

    /* decode ptr */
    MEM_NTOH(&ptr, B1_PTR_ADDR(mem, index), sizeof(ptr));
    MEM_NTOH(&size, B1_SIZE_ADDR(mem, index), sizeof(ptr));

    /* decode next pointer, so we know where this one ends */
    if (index) {
        MEM_NTOH(&prevptr, B1_PTR_ADDR(mem, index - 1), sizeof(prevptr));
    } else {
        prevptr = bucketsize;
    }

    assert(prevptr >= ptr);

    /* read the last entry */
    MEM_NTOH(&lastaddr, B1_PTR_ADDR(mem, (entries - 1)), 
      sizeof(lastaddr));

    /* infer term length */
    termlen = prevptr - ptr - size;

    if (newsize > size) {
        /* allocation is growing */

        if ((newsize - size) > lastaddr - B1_PTR(entries)) {
            /* not enough space to grow the entry */
            if (termlen + newsize + sizeof(uint16_t) * 2 
              > new_bucket_size) {
                /* ... even if the bucket was empty */
                *toobig = 1;
            }
            return NULL;
        }

        /* only need to move the bytes from the old entry */
        movesize = size;
    } else {
        /* shrinking realloc, move only the bytes that will be preserved */
        movesize = newsize;
    }

    /* move strings and vectors (careful, this line is tricky) */
    memmove(((char *) mem) + lastaddr - (newsize - size), 
      ((char *) mem) + lastaddr, ptr + movesize - lastaddr);

    /* adjust pointers */
    assert(((newsize > size) && ptr > (newsize - size)) 
      || ((newsize < size) && ptr > (size - newsize))
      || (newsize == size));
    ptr -= (newsize - size);
    MEM_HTON(B1_PTR_ADDR(mem, index), &ptr, sizeof(ptr));
    MEM_HTON(B1_SIZE_ADDR(mem, index), &newsize, sizeof(ptr));
    prevptr = ptr;

    for (index++; index < entries; index++) {
        MEM_NTOH(&ptr, B1_PTR_ADDR(mem, index), 
          sizeof(ptr));
        ptr -= (newsize - size);
        MEM_HTON(B1_PTR_ADDR(mem, index), &ptr, 
          sizeof(ptr));
    }

    /* done */
    return ((char *) mem) + prevptr;
}

void *bucket1_realloc(void *mem, unsigned int bucketsize, const char *term,
  unsigned int termlen, unsigned int newsize, int *toobig) {
    unsigned int i,
                 index;
    uint16_t entries,
             size,
             ptr,
             prevptr;

    /* read number of entries */
    MEM_NTOH(&entries, B1_ENTRIES_ADDR(mem), sizeof(entries));

    /* handle 0 entries case */
    if (!entries) {
        *toobig = 0;
        return NULL;
    }

    index = bucket1_binsearch(mem, bucketsize, entries, term, termlen);

    /* verify that index points to requested term */

    /* decode ptr */
    MEM_NTOH(&ptr, B1_PTR_ADDR(mem, index), sizeof(ptr));
    MEM_NTOH(&size, B1_SIZE_ADDR(mem, index), sizeof(ptr));

    /* decode next pointer, so we know where this one ends */
    if (index) {
        MEM_NTOH(&prevptr, B1_PTR_ADDR(mem, index - 1), sizeof(prevptr));
    } else {
        prevptr = bucketsize;
    }

    assert(prevptr >= ptr);
    if (((unsigned int) prevptr - ptr - size) == termlen) {
        for (i = 0; i < termlen; i++) {
            if (term[i] != ((char *) mem)[ptr + size + i]) {
                return NULL;
            }
        }

        return bucket1_realloc_at(mem, bucketsize, index, newsize, toobig);
    } else {
        return NULL;
    }
}

void *bucket1_alloc(void *mem, unsigned int bucketsize, const char *term, 
  unsigned int termlen, unsigned int newsize, int *toobig, 
  unsigned int *idx) {
    unsigned int index,
                 i,
                 len;
    uint16_t size,
             lastaddr,
             entries,
             ptr,
             prevptr;

    /* read number of entries */
    MEM_NTOH(&entries, B1_ENTRIES_ADDR(mem), sizeof(entries));
    size = newsize;

    if (entries) {
        /* read the last entry */
        MEM_NTOH(&lastaddr, B1_PTR_ADDR(mem, (entries - 1)), 
          sizeof(lastaddr));

        /* check that we have enough space to allocate entry */
        if ((size + termlen + 2 * (sizeof(uint16_t))) 
          > (lastaddr - B1_PTR(entries))) {
            /* test whether theres enough space to allocate the entry if the
             * bucket was empty */
            *toobig = (termlen + newsize + sizeof(uint16_t) * 2 
              > bucketsize - sizeof(entries));
            return NULL;
        }

        index = bucket1_binsearch(mem, bucketsize, entries, term, termlen);

        /* need to do a last comparison to figure out whether we want to insert
         * here or in the next position */
        MEM_NTOH(&ptr, B1_PTR_ADDR(mem, index), sizeof(ptr));
        MEM_NTOH(&size, B1_SIZE_ADDR(mem, index), sizeof(ptr));

        /* decode next pointer, so we know where this one ends */
        if (index) {
            MEM_NTOH(&prevptr, B1_PTR_ADDR(mem, index - 1), 
              sizeof(prevptr));
        } else {
            prevptr = bucketsize;
        }

        /* the way we do our binary search, the item may need to be inserted 
         * at the *next* location (assuming that there is no exact match) */

        assert(prevptr >= ptr);
        len = (unsigned int) (((unsigned int) prevptr - ptr - size) > termlen) 
            ? termlen : prevptr - ptr - size;

        for (i = 0; i < len; i++) {
            if (term[i] < ((char *) mem)[i + ptr + size]) {
                break;
            } else if (term[i] > ((char *) mem)[i + ptr + size]) {
                index++;
                prevptr = ptr;
                MEM_NTOH(&ptr, B1_PTR_ADDR(mem, index), sizeof(ptr));
                MEM_NTOH(&size, B1_SIZE_ADDR(mem, index), sizeof(ptr));
                break;
            }
        }

        /* exact match of prefixes */
        /* XXX: should check for exact match */
        if (i == len) {
            if (termlen > len) {
                index++;
                prevptr = ptr;
                MEM_NTOH(&ptr, B1_PTR_ADDR(mem, index), sizeof(ptr));
                MEM_NTOH(&size, B1_SIZE_ADDR(mem, index), sizeof(ptr));
            }
        }

        /* new allocation needs to be inserted at index */
        if (index < entries) {
            MEM_NTOH(&ptr, B1_PTR_ADDR(mem, index), sizeof(ptr));
            MEM_NTOH(&size, B1_SIZE_ADDR(mem, index), sizeof(ptr));

            /* decode next pointer, so we know where this one ends */
            if (index) {
                MEM_NTOH(&prevptr, B1_PTR_ADDR(mem, index - 1), 
                  sizeof(prevptr));
            } else {
                prevptr = bucketsize;
            }
 
            /* move strings and data down */
            memmove(((char *) mem) + lastaddr - termlen - newsize,
              ((char *) mem) + lastaddr,
              ptr - lastaddr + (prevptr - ptr));

            /* shift pointers up */
            for (i = entries; (i > index); i--) {
                MEM_NTOH(&ptr, B1_PTR_ADDR(mem, i - 1), sizeof(ptr));
                ptr -= termlen + newsize;
                MEM_HTON(B1_PTR_ADDR(mem, i), &ptr, sizeof(ptr));
                memcpy(B1_SIZE_ADDR(mem, i), B1_SIZE_ADDR(mem, i - 1), 
                  sizeof(size));
            }
            ptr = prevptr - termlen - newsize;
        } else {
            ptr = lastaddr - termlen - newsize;
        }
    } else {
        /* check that we have enough space to allocate entry */
        if (termlen + newsize + sizeof(uint16_t) * 2 
          > bucketsize - sizeof(entries)) {
            *toobig = 1;
            return NULL;
        }

        ptr = bucketsize - termlen - newsize;
        index = 0;
    }

    /* encode new pointer */
    size = newsize;
    MEM_HTON(B1_PTR_ADDR(mem, index), &ptr, sizeof(ptr));
    MEM_HTON(B1_SIZE_ADDR(mem, index), &size, sizeof(size));

    /* copy new string in */
    memcpy(((char *) mem) + ptr + size, term, termlen);

    /* encode new entries record */
    entries++;
    MEM_HTON(B1_ENTRIES_ADDR(mem), &entries, sizeof(entries));

    if (idx) {
        *idx = index;
    }
    return ((char *) mem) + ptr;
}

int bucket1_remove_at(void *mem, unsigned int bucketsize, unsigned int index) {
    unsigned int i;
    uint16_t lastaddr, 
             entries,
             size,
             ptr,
             prevptr;

    /* read number of entries */
    MEM_NTOH(&entries, B1_ENTRIES_ADDR(mem), sizeof(entries));

    if (!entries || (index >= entries)) {
        return 0;
    }

    /* decode ptr */
    MEM_NTOH(&ptr, B1_PTR_ADDR(mem, index), sizeof(ptr));
    MEM_NTOH(&size, B1_SIZE_ADDR(mem, index), sizeof(ptr));

    /* decode next pointer, so we know where this one ends */
    if (index) {
        MEM_NTOH(&prevptr, B1_PTR_ADDR(mem, index - 1), sizeof(prevptr));
    } else {
        prevptr = bucketsize;
    }

    assert(prevptr >= ptr);

    /* read the last entry */
    MEM_NTOH(&lastaddr, B1_PTR_ADDR(mem, (entries - 1)), 
      sizeof(lastaddr));

    /* move strings/data up */
    memmove(((char *) mem) + lastaddr + (prevptr - ptr),
      ((char *) mem) + lastaddr, ptr - lastaddr);

    size = prevptr - ptr;

    /* update pointers */
    for (i = index; i + 1 < entries; i++) {
        MEM_NTOH(&ptr, B1_PTR_ADDR(mem, i + 1), sizeof(ptr));
        ptr += size;
        MEM_HTON(B1_PTR_ADDR(mem, i), &ptr, sizeof(ptr));
        memcpy(B1_SIZE_ADDR(mem, i), 
          B1_SIZE_ADDR(mem, i + 1), sizeof(size));
    }

    /* encode new entries record */
    entries--;
    MEM_HTON(B1_ENTRIES_ADDR(mem), &entries, sizeof(entries));

    return 1;
}

int bucket1_remove(void *mem, unsigned int bucketsize, const char *term, 
  unsigned int termlen) {
    unsigned int i,
                 index;
    uint16_t entries,
             ptr,
             size,
             prevptr;

    /* read number of entries */
    MEM_NTOH(&entries, B1_ENTRIES_ADDR(mem), sizeof(entries));

    if (!entries) {
        return 0;
    }

    index = bucket1_binsearch(mem, bucketsize, entries, term, termlen);

    /* verify that index points to requested term */

    /* decode ptr */
    MEM_NTOH(&ptr, B1_PTR_ADDR(mem, index), sizeof(ptr));
    MEM_NTOH(&size, B1_SIZE_ADDR(mem, index), sizeof(ptr));

    /* decode next pointer, so we know where this one ends */
    if (index) {
        MEM_NTOH(&prevptr, B1_PTR_ADDR(mem, index - 1), sizeof(prevptr));
    } else {
        prevptr = bucketsize;
    }

    assert(prevptr >= ptr);
    if (((unsigned int) prevptr - ptr - size) == termlen) {
        for (i = 0; i < termlen; i++) {
            if (term[i] != ((char *) mem)[ptr + size + i]) {
                return 0;
            }
        }

        return bucket1_remove_at(mem, bucketsize, index);
    } else {
        return 0;
    }
}

const char *bucket1_term_at(void *mem, unsigned int bucketsize, 
  unsigned int index, unsigned int *len, void **data, unsigned int *veclen) {
    uint16_t addr,
             prevaddr,  /* logically previous (physically next) address */
             size,
             entries;

    /* read number of entries */
    MEM_NTOH(&entries, mem, sizeof(entries));

    if (index < entries) {
        /* read the entry */
        MEM_NTOH(&addr, B1_PTR_ADDR(mem, index), sizeof(addr));
        MEM_NTOH(&size, B1_SIZE_ADDR(mem, index), sizeof(size));
        assert(((sizeof(addr) * entries) <= addr) 
          && (bucketsize > addr));

        if (index) {
            /* its not the first entry, read the previous entry's address */
            MEM_NTOH(&prevaddr, B1_PTR_ADDR(mem, index - 1), 
              sizeof(prevaddr));
        } else {
            /* its the first entry, boundary is the end of the bucket */
            prevaddr = bucketsize;
        }

        *len = prevaddr - addr - size;
        *veclen = size;
        *data = ((char *) mem) + addr;
        return ((char *) mem) + addr + size;
    } else {
        /* its past the last entry */
        return NULL;
    }
}

int bucket1_split(void *mem1, unsigned int bucketsize1, void *mem2, 
  unsigned int bucketsize2, unsigned int terms) {
    unsigned int i,
                 j;
    uint16_t lastaddr, 
             entries,
             size,
             ptr,
             prevptr;

    memset(mem2, 0, bucketsize2);

    /* read number of entries */
    MEM_NTOH(&entries, B1_ENTRIES_ADDR(mem1), sizeof(entries));

    if (terms >= entries) {
        return (terms == entries);
    }

    if (terms) {
        /* read split entry */
        MEM_NTOH(&ptr, B1_PTR_ADDR(mem1, terms - 1), sizeof(ptr));
    } else {
        ptr = bucketsize1;
    }

    /* read last entry */
    MEM_NTOH(&lastaddr, B1_PTR_ADDR(mem1, entries - 1), sizeof(lastaddr));

    /* copy strings and data from bucket 1 to bucket 2 */
    memcpy((char *) mem2 + bucketsize2 - (ptr - lastaddr), 
      (char *) mem1 + lastaddr, ptr - lastaddr);

    /* copy pointers to new bucket */
    prevptr = ptr;
    for (j = 0, i = terms; i < entries; i++, j++) {
        MEM_NTOH(&ptr, B1_PTR_ADDR(mem1, i), sizeof(ptr));
        ptr += bucketsize1 - prevptr;
        MEM_HTON(B1_PTR_ADDR(mem2, j), &ptr, sizeof(size));
        memcpy(B1_SIZE_ADDR(mem2, j), B1_SIZE_ADDR(mem1, i), sizeof(size));
    }

    /* adjust entries record in both buckets */
    size = terms;
    mem_hton(B1_ENTRIES_ADDR(mem1), &size, sizeof(size));
    entries -= size;
    mem_hton(B1_ENTRIES_ADDR(mem2), &entries, sizeof(entries));

    return 1;
}

int bucket1_set_term(void *mem, unsigned int bucketsize, unsigned int termno, 
  const char *newterm, unsigned int newtermlen, int *toobig) {
    unsigned int i,
                 termlen;
    uint16_t size,
             ptr, 
             prevptr,
             lastptr,
             entries;

    /* read number of entries */
    MEM_NTOH(&entries, B1_ENTRIES_ADDR(mem), sizeof(entries));

    if (termno >= entries) {
        return 0;
    }

    /* read entry, previous and last pointers */
    MEM_NTOH(&ptr, B1_PTR_ADDR(mem, termno), sizeof(ptr));
    if (termno + 1 < entries) {
        MEM_NTOH(&prevptr, B1_PTR_ADDR(mem, termno + 1), sizeof(prevptr));

        /* read the last entry */
        MEM_NTOH(&lastptr, B1_PTR_ADDR(mem, (entries - 1)), sizeof(lastptr));
    } else {
        prevptr = bucketsize;
        lastptr = ptr;
    }
    MEM_NTOH(&size, B1_SIZE_ADDR(mem, size), sizeof(size));

    termlen = prevptr - ptr - size;

    if (newtermlen > termlen) {
        /* string is growing */

        /* check that we have enough space to allocate entry */
        if (((newtermlen - termlen) > (lastptr - B1_PTR(entries)))) {

            /* not enough space to allocate the entry */
            if ((newtermlen + 2 * sizeof(uint16_t) + size)
              > bucketsize - sizeof(entries)) {
                /* ... even if the bucket was empty */
                *toobig = 1;
            }
            return 0;
        }

        /* move strings and data prior to the entry */
        memmove(((char *) mem) + lastptr - (newtermlen - termlen), 
          ((char *) mem) + lastptr, ptr - lastptr + size);

        /* update pointers prior to the entry */
        for (i = termno; i < entries; i++) {
            MEM_NTOH(&lastptr, B1_PTR_ADDR(mem, i), sizeof(lastptr));
            lastptr -= (newtermlen - termlen);
            MEM_HTON(B1_PTR_ADDR(mem, i), &lastptr, sizeof(lastptr));
        }
    } else if (newtermlen < termlen) {
        /* string is shrinking */

        /* move strings and data prior to the entry */
        memmove(((char *) mem) + lastptr + (termlen - newtermlen), 
          ((char *) mem) + lastptr, ptr - lastptr + size);

        /* update pointers prior to the entry */
        for (i = termno; i < entries; i++) {
            MEM_NTOH(&lastptr, B1_PTR_ADDR(mem, i), sizeof(lastptr));
            lastptr += (termlen - newtermlen);
            MEM_HTON(B1_PTR_ADDR(mem, i), &lastptr, sizeof(lastptr));
        }
    }

    /* copy new string in */
    memcpy(((char *) mem) + ptr + size, newterm, newtermlen);

    return 1;
}

int bucket1_resize(void *mem, unsigned int oldsize, unsigned int newsize) {
    unsigned int i;
    uint16_t ptr,
             lastptr,
             entries;

    /* read number of entries */
    MEM_NTOH(&entries, B1_ENTRIES_ADDR(mem), sizeof(entries));

    if (!entries) {
        return 1;
    }

    /* read last entry */
    MEM_NTOH(&lastptr, B1_PTR_ADDR(mem, (entries - 1)), sizeof(lastptr));

    if (newsize < oldsize) {
        /* shrinking */
        if ((oldsize - newsize) > (lastptr - B1_PTR(entries))) {
            return 0;
        }

        /* move data and strings */
        memmove(((char *) mem) + lastptr - (oldsize - newsize), 
          ((char *) mem) + lastptr, oldsize - lastptr);

        /* update pointer entries */
        for (i = 0; i < entries; i++) {
            MEM_NTOH(&ptr, B1_PTR_ADDR(mem, i), sizeof(ptr));
            ptr -= (oldsize - newsize);
            MEM_HTON(B1_PTR_ADDR(mem, i), &ptr, sizeof(ptr));
        }
    } else if (newsize > oldsize) {
        /* growing */

        /* move data and strings */
        memmove(((char *) mem) + lastptr + (newsize - oldsize), 
          ((char *) mem) + lastptr, oldsize - lastptr);

        /* update pointer entries */
        for (i = 0; i < entries; i++) {
            MEM_NTOH(&ptr, B1_PTR_ADDR(mem, i), sizeof(ptr));
            ptr += (newsize - oldsize);
            MEM_HTON(B1_PTR_ADDR(mem, i), &ptr, sizeof(ptr));
        }
    }

    return 1;
}

void *bucket1_append(void *mem, unsigned int bucketsize, const char *term,
  unsigned int termlen, unsigned int size, int *toobig) {
    uint16_t entries,
             lastaddr,
             ptr;

    /* read number of entries */
    MEM_NTOH(&entries, B1_ENTRIES_ADDR(mem), sizeof(entries));

    if (entries) {
        /* read the last entry */
        MEM_NTOH(&lastaddr, B1_PTR_ADDR(mem, (entries - 1)), 
          sizeof(lastaddr));
    } else {
        lastaddr = bucketsize;
    }

    /* check that we have enough free space */
    if ((size + termlen + 2 * (sizeof(uint16_t))) 
      > (lastaddr - B1_PTR(entries))) {
        /* test whether theres enough space to allocate the entry if the
         * bucket was empty */
        *toobig = (termlen + size + sizeof(uint16_t) * 2 
          > bucketsize - sizeof(entries));
        return NULL;
    }

    /* copy term in */
    memcpy(((char *) mem) + lastaddr - termlen, term, termlen);

    /* update entries record and pointer */
    ptr = lastaddr - termlen - size;
    mem_hton(B1_PTR_ADDR(mem, entries), &ptr, sizeof(ptr));
    ptr = size;
    mem_hton(B1_SIZE_ADDR(mem, entries), &ptr, sizeof(ptr));
    entries++;
    mem_hton(B1_ENTRIES_ADDR(mem), &entries, sizeof(entries));

    return ((char *) mem) + lastaddr - termlen - size;
}

/* functions for strategy 2 */

/* binary searches a bucket for a term, returning the largest thing that is less
 * than or equal to the given term (this property, which the usual binary search
 * must be modified to provide, is important because we can then use it for
 * insertion sorting as well as searching) */
static unsigned int bucket2_binsearch(void *mem, unsigned int bucketsize, 
  unsigned int entries, const char *term, unsigned int termlen) {
    unsigned int l,
                 r,
                 m,
                 i,
                 len,
                 tlen;
    uint16_t ptr,
             prevptr,
             size;

    assert(entries);

    l = 0;
    r = entries - 1;

    while (l < r) {
        m = BIT_DIV2((r + l + 1), 1);

        /* decode m ptr */
        MEM_NTOH(&ptr, B2_PTR_ADDR(mem, m), sizeof(ptr));
        MEM_NTOH(&size, B2_SIZE_ADDR(mem, m), sizeof(ptr));

        /* decode next pointer, so we know where this one ends */
        if (m) {
            MEM_NTOH(&prevptr, B2_PTR_ADDR(mem, m - 1), sizeof(prevptr));

            tlen = prevptr - ptr - size;
            len = (tlen > termlen) ? termlen : tlen;

            for (i = 0; i < len; i++) {
                if (term[i] < ((char *) mem)[i + ptr + size]) {
                    r = m - 1;
                    break;
                } else if (term[i] > ((char *) mem)[i + ptr + size]) {
                    l = m;
                    break;
                }
            }

            /* exact match of prefixes */
            if (i == len) {
                if (termlen == tlen) {
                    return m;
                } else if (termlen < tlen) {
                    r = m - 1;
                } else {
                    l = m;
                }
            }
        } else {
            /* m is first entry */
            prevptr = bucketsize;

            tlen = prevptr - ptr - size;
            len = (tlen > termlen) ? termlen : tlen;

            for (i = 0; i < len; i++) {
                if (term[i] < ((char *) mem)[i + ptr + size]) {
                    /* no exact match, act now to prevent underflow */
                    return 0;
                    break;
                } else if (term[i] > ((char *) mem)[i + ptr + size]) {
                    l = m;
                    break;
                }
            }

            /* exact match of prefixes */
            if (i == len) {
                if (termlen <= tlen) {
                    /* no match, act now to prevent underflow */
                    return 0;
                } else {
                    l = m;
                }
            }
        }
    }

    /* no exact match */
    return l;
}

void *bucket2_find(void *mem, unsigned int bucketsize, const char *term, 
  unsigned int termlen, unsigned int *veclen, unsigned int *idx) {
    unsigned int index,
                 i;
    uint16_t entries,
             size,
             ptr,
             prevptr;
             
    /* read number of entries */
    MEM_NTOH(&entries, B2_ENTRIES_ADDR(mem), sizeof(entries));

    /* handle 0 entries case */
    if (!entries) {
        return NULL;
    }

    index = bucket2_binsearch(mem, bucketsize, entries, term, termlen);

    /* decode ptr */
    MEM_NTOH(&ptr, B2_PTR_ADDR(mem, index), sizeof(ptr));
    MEM_NTOH(&size, B2_SIZE_ADDR(mem, index), sizeof(ptr));

    /* decode next pointer, so we know where this one ends */
    if (index) {
        MEM_NTOH(&prevptr, B2_PTR_ADDR(mem, index - 1), sizeof(prevptr));
    } else {
        prevptr = bucketsize;
    }

    assert(prevptr >= ptr);
    if (((unsigned int) prevptr - ptr) - size == termlen) {
        for (i = 0; i < termlen; i++) {
            if (term[i] != ((char *) mem)[i + ptr + size]) {
                return NULL;
            }
        }

        *veclen = size;
        if (idx) {
            *idx = index;
        }
        return ((char *) mem) + ptr;
    }

    return NULL;
}

void *bucket2_search(void *mem, unsigned int bucketsize, const char *term, 
  unsigned int termlen, unsigned int *veclen, unsigned int *idx) {
    unsigned int index;
    uint16_t entries,
             size,
             ptr,
             prevptr;
             
    /* read number of entries */
    MEM_NTOH(&entries, B2_ENTRIES_ADDR(mem), sizeof(entries));

    /* handle 0 entries case */
    if (!entries) {
        return NULL;
    }

    index = bucket2_binsearch(mem, bucketsize, entries, term, termlen);

    /* decode ptr */
    MEM_NTOH(&ptr, B2_PTR_ADDR(mem, index), sizeof(ptr));
    MEM_NTOH(&size, B2_SIZE_ADDR(mem, index), sizeof(ptr));

    /* decode next pointer, so we know where this one ends */
    if (index) {
        MEM_NTOH(&prevptr, B2_PTR_ADDR(mem, index - 1), sizeof(prevptr));
    } else {
        prevptr = bucketsize;
    }

    if (idx) {
        *idx = index;
    }
    *veclen = size;
    return ((char *) mem) + ptr;
}

void *bucket2_realloc_at(void *mem, unsigned int bucketsize, 
  unsigned int index, unsigned int newsize, int *toobig) {
    uint16_t size,
             entries,
             new_bucket_size,
             ptr;

    /* read number of entries */
    MEM_NTOH(&entries, B2_ENTRIES_ADDR(mem), sizeof(entries));

    /* handle 0 entries case */
    if (!entries || (index >= entries)) {
        *toobig = 0;
        return NULL;
    }

    new_bucket_size = bucketsize - sizeof(uint16_t);

    /* decode ptr */
    MEM_NTOH(&ptr, B2_PTR_ADDR(mem, index), sizeof(ptr));
    MEM_NTOH(&size, B2_SIZE_ADDR(mem, index), sizeof(ptr));

    if (size == newsize) {
        return ((char *) mem) + ptr;
    } else {
        return NULL;
    }
}

void *bucket2_realloc(void *mem, unsigned int bucketsize, const char *term,
  unsigned int termlen, unsigned int newsize, int *toobig) {
    unsigned int i,
                 index;
    uint16_t entries,
             size,
             ptr,
             prevptr;

    /* read number of entries */
    MEM_NTOH(&entries, B2_ENTRIES_ADDR(mem), sizeof(entries));

    /* handle 0 entries case */
    if (!entries) {
        *toobig = 0;
        return NULL;
    }

    index = bucket2_binsearch(mem, bucketsize, entries, term, termlen);

    /* verify that index points to requested term */

    /* decode ptr */
    MEM_NTOH(&ptr, B2_PTR_ADDR(mem, index), sizeof(ptr));
    MEM_NTOH(&size, B2_SIZE_ADDR(mem, index), sizeof(ptr));

    if (size != newsize) {
        return NULL;
    }

    /* decode next pointer, so we know where this one ends */
    if (index) {
        MEM_NTOH(&prevptr, B2_PTR_ADDR(mem, index - 1), sizeof(prevptr));
    } else {
        prevptr = bucketsize;
    }

    assert(prevptr >= ptr);
    if (((unsigned int) prevptr - ptr - size) == termlen) {
        for (i = 0; i < termlen; i++) {
            if (term[i] != ((char *) mem)[ptr + size + i]) {
                return NULL;
            }
        }

        return bucket2_realloc_at(mem, bucketsize, index, newsize, toobig);
    } else {
        return NULL;
    }
}

void *bucket2_alloc(void *mem, unsigned int bucketsize, const char *term, 
  unsigned int termlen, unsigned int newsize, int *toobig, 
  unsigned int *idx) {
    unsigned int index,
                 i,
                 len;
    uint16_t size,
             lastaddr,
             entries,
             ptr,
             prevptr;

    /* read number of entries */
    MEM_NTOH(&entries, B2_ENTRIES_ADDR(mem), sizeof(entries));
    MEM_NTOH(&size, B2_SIZE_ADDR(mem, 0), sizeof(size));

    if (entries && (size != newsize)) {
        return NULL;
    }

    if (entries) {
        /* read the last entry */
        MEM_NTOH(&lastaddr, B2_PTR_ADDR(mem, (entries - 1)), 
          sizeof(lastaddr));

        /* check that we have enough space to allocate entry */
        if ((size + termlen + 2 * (sizeof(uint16_t))) 
          > (lastaddr - B2_PTR(entries))) {
            /* test whether theres enough space to allocate the entry if the
             * bucket was empty */
            *toobig = (termlen + newsize + sizeof(uint16_t) * 2 
              > bucketsize - sizeof(entries));
            return NULL;
        }

        index = bucket2_binsearch(mem, bucketsize, entries, term, termlen);

        /* need to do a last comparison to figure out whether we want to insert
         * here or in the next position */
        MEM_NTOH(&ptr, B2_PTR_ADDR(mem, index), sizeof(ptr));
        MEM_NTOH(&size, B2_SIZE_ADDR(mem, index), sizeof(ptr));

        /* decode next pointer, so we know where this one ends */
        if (index) {
            MEM_NTOH(&prevptr, B2_PTR_ADDR(mem, index - 1), 
              sizeof(prevptr));
        } else {
            prevptr = bucketsize;
        }

        /* the way we do our binary search, the item may need to be inserted 
         * at the *next* location (assuming that there is no exact match) */

        assert(prevptr >= ptr);
        len = (unsigned int) (((unsigned int) prevptr - ptr - size) > termlen) 
            ? termlen : prevptr - ptr - size;

        for (i = 0; i < len; i++) {
            if (term[i] < ((char *) mem)[i + ptr + size]) {
                break;
            } else if (term[i] > ((char *) mem)[i + ptr + size]) {
                index++;
                prevptr = ptr;
                MEM_NTOH(&ptr, B2_PTR_ADDR(mem, index), sizeof(ptr));
                MEM_NTOH(&size, B2_SIZE_ADDR(mem, index), sizeof(ptr));
                break;
            }
        }

        /* exact match of prefixes */
        /* XXX: should check for exact match */
        if (i == len) {
            if (termlen > len) {
                index++;
                prevptr = ptr;
                MEM_NTOH(&ptr, B2_PTR_ADDR(mem, index), sizeof(ptr));
                MEM_NTOH(&size, B2_SIZE_ADDR(mem, index), sizeof(ptr));
            }
        }

        /* new allocation needs to be inserted at index */
        if (index < entries) {
            MEM_NTOH(&ptr, B2_PTR_ADDR(mem, index), sizeof(ptr));
            MEM_NTOH(&size, B2_SIZE_ADDR(mem, index), sizeof(ptr));

            /* decode next pointer, so we know where this one ends */
            if (index) {
                MEM_NTOH(&prevptr, B2_PTR_ADDR(mem, index - 1), 
                  sizeof(prevptr));
            } else {
                prevptr = bucketsize;
            }
 
            /* move strings and data down */
            memmove(((char *) mem) + lastaddr - termlen - newsize,
              ((char *) mem) + lastaddr,
              ptr - lastaddr + (prevptr - ptr));

            /* shift pointers up */
            for (i = entries; (i > index); i--) {
                MEM_NTOH(&ptr, B2_PTR_ADDR(mem, i - 1), sizeof(ptr));
                ptr -= termlen + newsize;
                MEM_HTON(B2_PTR_ADDR(mem, i), &ptr, sizeof(ptr));
                memcpy(B2_SIZE_ADDR(mem, i), B2_SIZE_ADDR(mem, i - 1), 
                  sizeof(size));
            }
            ptr = prevptr - termlen - newsize;
        } else {
            ptr = lastaddr - termlen - newsize;
        }
    } else {
        /* check that we have enough space to allocate entry */
        if (termlen + newsize + sizeof(uint16_t) * 2 
          > bucketsize - sizeof(entries)) {
            *toobig = 1;
            return NULL;
        }

        ptr = bucketsize - termlen - newsize;
        index = 0;
    }

    /* encode new pointer */
    size = newsize;
    MEM_HTON(B2_PTR_ADDR(mem, index), &ptr, sizeof(ptr));
    MEM_HTON(B2_SIZE_ADDR(mem, index), &size, sizeof(size));

    /* copy new string in */
    memcpy(((char *) mem) + ptr + size, term, termlen);

    /* encode new entries record */
    entries++;
    MEM_HTON(B2_ENTRIES_ADDR(mem), &entries, sizeof(entries));

    if (idx) {
        *idx = index;
    }
    return ((char *) mem) + ptr;
}

int bucket2_remove_at(void *mem, unsigned int bucketsize, unsigned int index) {
    unsigned int i;
    uint16_t lastaddr, 
             entries,
             size,
             ptr,
             prevptr;

    /* read number of entries */
    MEM_NTOH(&entries, B2_ENTRIES_ADDR(mem), sizeof(entries));

    if (!entries || (index >= entries)) {
        return 0;
    }

    /* decode ptr */
    MEM_NTOH(&ptr, B2_PTR_ADDR(mem, index), sizeof(ptr));

    /* decode next pointer, so we know where this one ends */
    if (index) {
        MEM_NTOH(&prevptr, B2_PTR_ADDR(mem, index - 1), sizeof(prevptr));
    } else {
        prevptr = bucketsize;
    }

    assert(prevptr >= ptr);

    /* read the last entry */
    MEM_NTOH(&lastaddr, B2_PTR_ADDR(mem, (entries - 1)), 
      sizeof(lastaddr));

    /* move strings/data up */
    memmove(((char *) mem) + lastaddr + (prevptr - ptr),
      ((char *) mem) + lastaddr, ptr - lastaddr);

    size = prevptr - ptr;

    /* update pointers */
    for (i = index; i + 1 < entries; i++) {
        MEM_NTOH(&ptr, B2_PTR_ADDR(mem, i + 1), sizeof(ptr));
        ptr += size;
        MEM_HTON(B2_PTR_ADDR(mem, i), &ptr, sizeof(ptr));
    }

    /* encode new entries record */
    entries--;
    MEM_HTON(B2_ENTRIES_ADDR(mem), &entries, sizeof(entries));

    return 1;
}

int bucket2_remove(void *mem, unsigned int bucketsize, const char *term, 
  unsigned int termlen) {
    unsigned int i,
                 index;
    uint16_t entries,
             ptr,
             size,
             prevptr;

    /* read number of entries */
    MEM_NTOH(&entries, B2_ENTRIES_ADDR(mem), sizeof(entries));

    if (!entries) {
        return 0;
    }

    index = bucket2_binsearch(mem, bucketsize, entries, term, termlen);

    /* verify that index points to requested term */

    /* decode ptr */
    MEM_NTOH(&ptr, B2_PTR_ADDR(mem, index), sizeof(ptr));
    MEM_NTOH(&size, B2_SIZE_ADDR(mem, index), sizeof(ptr));

    /* decode next pointer, so we know where this one ends */
    if (index) {
        MEM_NTOH(&prevptr, B2_PTR_ADDR(mem, index - 1), sizeof(prevptr));
    } else {
        prevptr = bucketsize;
    }

    assert(prevptr >= ptr);
    if (((unsigned int) prevptr - ptr - size) == termlen) {
        for (i = 0; i < termlen; i++) {
            if (term[i] != ((char *) mem)[ptr + size + i]) {
                return 0;
            }
        }

        return bucket2_remove_at(mem, bucketsize, index);
    } else {
        return 0;
    }
}

const char *bucket2_term_at(void *mem, unsigned int bucketsize, 
  unsigned int index, unsigned int *len, void **data, unsigned int *veclen) {
    uint16_t addr,
             prevaddr,  /* logically previous (physically next) address */
             size,
             entries;

    /* read number of entries */
    MEM_NTOH(&entries, mem, sizeof(entries));

    if (index < entries) {
        /* read the entry */
        MEM_NTOH(&addr, B2_PTR_ADDR(mem, index), sizeof(addr));
        MEM_NTOH(&size, B2_SIZE_ADDR(mem, index), sizeof(size));
        assert(((sizeof(addr) * entries) <= addr) 
          && (bucketsize > addr));

        if (index) {
            /* its not the first entry, read the previous entry's address */
            MEM_NTOH(&prevaddr, B2_PTR_ADDR(mem, index - 1), 
              sizeof(prevaddr));
        } else {
            /* its the first entry, boundary is the end of the bucket */
            prevaddr = bucketsize;
        }

        *len = prevaddr - addr - size;
        *veclen = size;
        *data = ((char *) mem) + addr;
        return ((char *) mem) + addr + size;
    } else {
        /* its past the last entry */
        return NULL;
    }
}

int bucket2_split(void *mem1, unsigned int bucketsize1, void *mem2, 
  unsigned int bucketsize2, unsigned int terms) {
    unsigned int i,
                 j;
    uint16_t lastaddr, 
             entries,
             size,
             ptr,
             prevptr;

    memset(mem2, 0, bucketsize2);

    /* read number of entries */
    MEM_NTOH(&entries, B2_ENTRIES_ADDR(mem1), sizeof(entries));

    if (terms >= entries) {
        return (terms == entries);
    }

    if (terms) {
        /* read split entry */
        MEM_NTOH(&ptr, B2_PTR_ADDR(mem1, terms - 1), sizeof(ptr));
    } else {
        ptr = bucketsize1;
    }

    /* read last entry */
    MEM_NTOH(&lastaddr, B2_PTR_ADDR(mem1, entries - 1), sizeof(lastaddr));

    /* copy strings and data from bucket 1 to bucket 2 */
    memcpy((char *) mem2 + bucketsize2 - (ptr - lastaddr), 
      (char *) mem1 + lastaddr, ptr - lastaddr);

    /* copy pointers to new bucket */
    prevptr = ptr;
    for (j = 0, i = terms; i < entries; i++, j++) {
        MEM_NTOH(&ptr, B2_PTR_ADDR(mem1, i), sizeof(ptr));
        ptr += bucketsize1 - prevptr;
        MEM_HTON(B2_PTR_ADDR(mem2, j), &ptr, sizeof(size));
    }
    memcpy(B2_SIZE_ADDR(mem2, j), B2_SIZE_ADDR(mem1, i), sizeof(size));

    /* adjust entries record in both buckets */
    size = terms;
    mem_hton(B2_ENTRIES_ADDR(mem1), &size, sizeof(size));
    entries -= size;
    mem_hton(B2_ENTRIES_ADDR(mem2), &entries, sizeof(entries));

    return 1;
}

int bucket2_set_term(void *mem, unsigned int bucketsize, unsigned int termno, 
  const char *newterm, unsigned int newtermlen, int *toobig) {
    unsigned int i,
                 termlen;
    uint16_t size,
             ptr, 
             prevptr,
             lastptr,
             entries;

    /* read number of entries */
    MEM_NTOH(&entries, B2_ENTRIES_ADDR(mem), sizeof(entries));

    if (termno >= entries) {
        return 0;
    }

    /* read entry, previous and last pointers */
    MEM_NTOH(&ptr, B2_PTR_ADDR(mem, termno), sizeof(ptr));
    if (termno + 1 < entries) {
        MEM_NTOH(&prevptr, B2_PTR_ADDR(mem, termno + 1), sizeof(prevptr));

        /* read the last entry */
        MEM_NTOH(&lastptr, B2_PTR_ADDR(mem, (entries - 1)), sizeof(lastptr));
    } else {
        prevptr = bucketsize;
        lastptr = ptr;
    }
    MEM_NTOH(&size, B2_SIZE_ADDR(mem, size), sizeof(size));

    termlen = prevptr - ptr - size;

    if (newtermlen > termlen) {
        /* string is growing */

        /* check that we have enough space to allocate entry */
        if (((newtermlen - termlen) > (lastptr - B2_PTR(entries)))) {

            /* not enough space to allocate the entry */
            if ((newtermlen + 2 * sizeof(uint16_t) + size)
              > bucketsize - sizeof(entries)) {
                /* ... even if the bucket was empty */
                *toobig = 1;
            }
            return 0;
        }

        /* move strings and data prior to the entry */
        memmove(((char *) mem) + lastptr - (newtermlen - termlen), 
          ((char *) mem) + lastptr, ptr - lastptr + size);

        /* update pointers prior to the entry */
        for (i = termno; i < entries; i++) {
            MEM_NTOH(&lastptr, B2_PTR_ADDR(mem, i), sizeof(lastptr));
            lastptr -= (newtermlen - termlen);
            MEM_HTON(B2_PTR_ADDR(mem, i), &lastptr, sizeof(lastptr));
        }
    } else if (newtermlen < termlen) {
        /* string is shrinking */

        /* move strings and data prior to the entry */
        memmove(((char *) mem) + lastptr + (termlen - newtermlen), 
          ((char *) mem) + lastptr, ptr - lastptr + size);

        /* update pointers prior to the entry */
        for (i = termno; i < entries; i++) {
            MEM_NTOH(&lastptr, B2_PTR_ADDR(mem, i), sizeof(lastptr));
            lastptr += (termlen - newtermlen);
            MEM_HTON(B2_PTR_ADDR(mem, i), &lastptr, sizeof(lastptr));
        }
    }

    /* copy new string in */
    memcpy(((char *) mem) + ptr + size, newterm, newtermlen);

    return 1;
}

int bucket2_resize(void *mem, unsigned int oldsize, unsigned int newsize) {
    unsigned int i;
    uint16_t ptr,
             lastptr,
             entries;

    /* read number of entries */
    MEM_NTOH(&entries, B2_ENTRIES_ADDR(mem), sizeof(entries));

    if (!entries) {
        return 1;
    }

    /* read last entry */
    MEM_NTOH(&lastptr, B2_PTR_ADDR(mem, (entries - 1)), sizeof(lastptr));

    if (newsize < oldsize) {
        /* shrinking */
        if ((oldsize - newsize) > (lastptr - B2_PTR(entries))) {
            return 0;
        }

        /* move data and strings */
        memmove(((char *) mem) + lastptr - (oldsize - newsize), 
          ((char *) mem) + lastptr, oldsize - lastptr);

        /* update pointer entries */
        for (i = 0; i < entries; i++) {
            MEM_NTOH(&ptr, B2_PTR_ADDR(mem, i), sizeof(ptr));
            ptr -= (oldsize - newsize);
            MEM_HTON(B2_PTR_ADDR(mem, i), &ptr, sizeof(ptr));
        }
    } else if (newsize > oldsize) {
        /* growing */

        /* move data and strings */
        memmove(((char *) mem) + lastptr + (newsize - oldsize), 
          ((char *) mem) + lastptr, oldsize - lastptr);

        /* update pointer entries */
        for (i = 0; i < entries; i++) {
            MEM_NTOH(&ptr, B2_PTR_ADDR(mem, i), sizeof(ptr));
            ptr += (newsize - oldsize);
            MEM_HTON(B2_PTR_ADDR(mem, i), &ptr, sizeof(ptr));
        }
    }

    return 1;
}

void *bucket2_append(void *mem, unsigned int bucketsize, const char *term,
  unsigned int termlen, unsigned int size, int *toobig) {
    uint16_t entries,
             lastaddr,
             ptr;

    /* read number of entries */
    MEM_NTOH(&entries, B2_ENTRIES_ADDR(mem), sizeof(entries));

    if (entries) {
        /* read the last entry */
        MEM_NTOH(&lastaddr, B2_PTR_ADDR(mem, (entries - 1)), 
          sizeof(lastaddr));
    } else {
        lastaddr = bucketsize;
    }

    /* check that we have enough free space */
    if ((size + termlen + 2 * (sizeof(uint16_t))) 
      > (lastaddr - B2_PTR(entries))) {
        /* test whether theres enough space to allocate the entry if the
         * bucket was empty */
        *toobig = (termlen + size + sizeof(uint16_t) * 2 
          > bucketsize - sizeof(entries));
        return NULL;
    }

    /* copy term in */
    memcpy(((char *) mem) + lastaddr - termlen, term, termlen);

    /* update entries record and pointer */
    ptr = lastaddr - termlen - size;
    mem_hton(B2_PTR_ADDR(mem, entries), &ptr, sizeof(ptr));
    ptr = size;
    mem_hton(B2_SIZE_ADDR(mem, entries), &ptr, sizeof(ptr));
    entries++;
    mem_hton(B2_ENTRIES_ADDR(mem), &entries, sizeof(entries));

    return ((char *) mem) + lastaddr - termlen - size;
}

#include <stdio.h>

int bucket_print(void *mem, unsigned int bucketsize, int strategy) {
    void *data;
    const char *term;
    unsigned int i,
                 tlen,
                 vlen;
    unsigned int state = 0;

    printf("%u entries\n", bucket_entries(mem, bucketsize, strategy));

    while ((term = bucket_next_term(mem, bucketsize, strategy, &state, &tlen, 
        &data, &vlen))) {

        putc('\'', stdout);
        for (i = 0; i < tlen; i++) {
            putc(term[i], stdout);
        }
        putc('\'', stdout);
        putc(' ', stdout);
        printf("%u %u (off %u)\n", tlen, vlen, 
          (unsigned int) (((char *) data) - (char *) mem));
    }
    putc('\n', stdout);
    return 1;
}

/* switching functions */

void *bucket_alloc(void *bucketmem, unsigned int bucketsize, int strategy,
  const char *term, unsigned int termlen, unsigned int newsize, 
  int *toobig, unsigned int *idx) {
    switch (strategy) {
    case 1:
        return bucket1_alloc(bucketmem, bucketsize, term, termlen, newsize, 
          toobig, idx);

    case 2:
        return bucket2_alloc(bucketmem, bucketsize, term, termlen, newsize, 
          toobig, idx);

    default:
        *toobig = 0;
        return NULL;
    };
}

void *bucket_find(void *bucketmem, unsigned int bucketsize,
  int strategy, const char *term, unsigned int termlen, 
  unsigned int *veclen, unsigned int *idx) {
    switch (strategy) {
    case 1:
        return bucket1_find(bucketmem, bucketsize, term, termlen, veclen, 
            idx);
    case 2:
        return bucket2_find(bucketmem, bucketsize, term, termlen, veclen, 
            idx);

    default:
        return NULL;
    };
}

void *bucket_search(void *bucketmem, unsigned int bucketsize, int strategy, 
  const char *term, unsigned int termlen, unsigned int *veclen, 
  unsigned int *idx) {
    switch (strategy) {
    case 1:
        return bucket1_search(bucketmem, bucketsize, term, termlen, veclen, 
            idx);

    case 2:
        return bucket2_search(bucketmem, bucketsize, term, termlen, veclen, 
            idx);

    default:
        return NULL;
    };
}

int bucket_remove(void *bucketmem, unsigned int bucketsize, int strategy,
  const char *term, unsigned int termlen) {
    switch (strategy) {
    case 1:
        return bucket1_remove(bucketmem, bucketsize, term, termlen);
        
    case 2:
        return bucket2_remove(bucketmem, bucketsize, term, termlen);

    default:
        return 0;
    };
}

int bucket_remove_at(void *bucketmem, unsigned int bucketsize, int strategy,
  unsigned int index) {
    switch (strategy) {
    case 1:
        return bucket1_remove_at(bucketmem, bucketsize, index);

    case 2:
        return bucket2_remove_at(bucketmem, bucketsize, index);

    default:
        return 0;
    };
}

void *bucket_realloc(void *bucketmem, unsigned int bucketsize, 
  int strategy, const char *term, unsigned int termlen, 
  unsigned int newlen, int *toobig) {
    switch (strategy) {
    case 1:
        return bucket1_realloc(bucketmem, bucketsize, term, termlen, 
          newlen, toobig);

    case 2:
        return bucket2_realloc(bucketmem, bucketsize, term, termlen, 
          newlen, toobig);

    default:
        *toobig = 0;
        return NULL;
    };
}

void *bucket_realloc_at(void *bucketmem, unsigned int bucketsize, 
  int strategy, unsigned int index, unsigned int newlen, int *toobig) {
    switch (strategy) {
    case 1:
        return bucket1_realloc_at(bucketmem, bucketsize, index, newlen, 
            toobig);

    case 2:
        return bucket2_realloc_at(bucketmem, bucketsize, index, newlen, 
            toobig);

    default:
        *toobig = 0;
        return NULL;
    };
}

const char *bucket_next_term(void *bucketmem, unsigned int bucketsize, 
  int strategy, unsigned int *state, unsigned int *len, void **data, 
  unsigned int *veclen) {
    assert(bucketsize);

    switch (strategy) {
    case 1:
        return bucket1_term_at(bucketmem, bucketsize, (*state)++, len, data, 
            veclen);

    case 2:
        return bucket2_term_at(bucketmem, bucketsize, (*state)++, len, data, 
            veclen);

    default:
        return NULL;
    };
}

const char *bucket_term_at(void *bucketmem, unsigned int bucketsize, 
  int strategy, unsigned int index, unsigned int *len, void **data, 
  unsigned int *veclen) {
    assert(bucketsize);

    switch (strategy) {
    case 1:
        return bucket1_term_at(bucketmem, bucketsize, index, len, data, 
            veclen);

    case 2:
        return bucket2_term_at(bucketmem, bucketsize, index, len, data, 
            veclen);

    default:
        return NULL;
    };
}

int bucket_split(void *bucketmem1, unsigned int bucketsize1, 
  void *bucketmem2, unsigned int bucketsize2, int strategy, 
  unsigned int split_terms) {
    switch (strategy) {
    case 1:
        return bucket1_split(bucketmem1, bucketsize1, bucketmem2, bucketsize2, 
          split_terms);

    case 2:
        return bucket2_split(bucketmem1, bucketsize1, bucketmem2, bucketsize2, 
          split_terms);

    default:
        return 0;
    }
}

int bucket_resize(void *bucketmem, unsigned int old_bucketsize, int strategy, 
  unsigned int new_bucketsize) {
    switch (strategy) {
    case 1:
        return bucket1_resize(bucketmem, old_bucketsize, new_bucketsize);

    case 2:
        return bucket2_resize(bucketmem, old_bucketsize, new_bucketsize);

    default:
        return 0;
    };
}

void *bucket_append(void *bucketmem, unsigned int bucketsize, int strategy,
  const char *term, unsigned int termlen, unsigned int size, 
  int *toobig) {
    switch (strategy) {
    case 1:
        return bucket1_append(bucketmem, bucketsize, term, termlen, size, 
          toobig);

    case 2:
        return bucket2_append(bucketmem, bucketsize, term, termlen, size, 
          toobig);

    default:
        *toobig = 0;
        return NULL;
    };
}

void bucket_print_split(void *bucketmem, unsigned int bucketsize, 
  int strategy, unsigned int terms, const char *term, unsigned int termlen, 
  unsigned int additional, int smaller) {
    const char *currterm;
    unsigned int state = 0;
    void *vec;
    unsigned int i,
                 j,
                 len,
                 veclen,
                 sum = 0;

    for (i = 0; i < terms; i++) {
        currterm = bucket_next_term(bucketmem, bucketsize, strategy, &state, 
            &len, &vec, &veclen);
        assert(currterm);
        for (j = 0; j < len; j++) {
            putc(currterm[j], stdout);
        }
        printf(" (%u) ", veclen + len);
        sum += len + veclen;
    }

    if (smaller) {
        printf("(additional) ");
        for (j = 0; j < termlen; j++) {
            putc(term[j], stdout);
        }
        printf(" (%u) ", additional);
        sum += additional;
    }

    printf("[%u] | ", sum);
    sum = 0;

    if (!smaller) {
        printf("(additional) ");
        for (j = 0; j < termlen; j++) {
            putc(term[j], stdout);
        }
        printf(" (%u) ", additional);
        sum += additional;
    }

    while ((currterm = bucket_next_term(bucketmem, bucketsize, strategy, &state,
        &len, &vec, &veclen))) {

        for (j = 0; j < len; j++) {
            putc(currterm[j], stdout);
        }
        printf(" (%u) ", veclen + len);
        sum += len + veclen;
    }

    printf(" [%u]\n", sum);
}

unsigned int bucket_find_split_entry(void *bucketmem, unsigned int bucketsize,
  int strategy, unsigned int range, const char *term, 
  unsigned int termlen, unsigned int additional, int *smaller) {
    const void *ret;                /* return value from bucket functions */
    void *dataptr;                  /* data location of vector in bucket */
    int tmpsmaller = 0,             /* value to be copied into smaller */
        consumed = 0,               /* whether the additional term has been 
                                     * consumed */ 
        cmp;                        /* result of string comparison */
    unsigned int shortest,          /* size of the shortest entry we've seen */
                 index,             /* split bucket term index */
                 disp,              /* displacement of shortest entry from a 
                                     * perfectly balanced split */
                 terms = 0,         /* number of terms seen thus far */
                 sum = 0,           /* sum of data thus far */
                 len,               /* length of string entry */
                 veclen,            /* length of data in entry */
                 data,              /* amount of data in bucket */
                 iter_state = 0;    /* state of iteration */

    /* determine where to split the leaf node 
     *
     * prefix b-tree magic is performed here, by selecting shortest string to 
     * push up within given range.  If two strings are equally short, the 
     * distribution of data between the buckets is considered, with more even
     * distribution favoured. */

    assert(bucket_sorted(strategy));  /* XXX: should handle unsorted buckets 
                                       * (by sorting) */

    data = bucket_utilised(bucketmem, bucketsize, strategy) 
      + bucket_string(bucketmem, bucketsize, strategy) + additional;
    data /= 2;
    /* our interval of interest is now data +/- range */

    ret = bucket_next_term(bucketmem, bucketsize, strategy, &iter_state, &len, 
        &dataptr, &veclen);
    assert(ret);

    /* iterate until we come up to term that enters range */
    while (ret 

      /* need to do comparison to additional term now so that we can stop on
       * term that crosses boundary into acceptable range */
      && ((cmp = str_nncmp(term, termlen, ret, len)), 1)

      /* if additional term is less than bucket term ... */
      && (((!consumed && (cmp < 0))
          /* ... check that its not going to cross into range ... */
          && (sum + additional < data - range) 
          /* ... and consume it */
          && ((sum += additional), (consumed = 1)))

        /* else if additional term is greater than bucket term or already
         * consumed ... */
        || ((consumed || (cmp > 0))
          /* ... check that bucket term isn't going to cross into range ... */
          && (sum + len + veclen < data - range) 
          && ((sum += len + veclen), ++terms)
          && (ret = bucket_next_term(bucketmem, bucketsize, strategy, 
              &iter_state, &len, &dataptr, &veclen)))

        /* else if they're equal ... */
        || ((!consumed && !cmp) 
          /* ... check that the sum isn't going to cross into range ... */
          && (sum + len + veclen + additional < data - range) 
          /* ... and consume them */
          && ((sum += len + veclen + additional), ++terms, (consumed = 1))
          && (ret = bucket_next_term(bucketmem, bucketsize, strategy, 
              &iter_state, &len, &dataptr, &veclen))))) {

        /* do nothing */
    }

    /* we're now up to term that may enter range, its the answer if it crosses
     * the entire range.  Either way we have to consume the next term here. */

    /* if additional term hasn't been consumed, and is less than bucket 
     * term ... */
    if (ret && ((cmp = str_nncmp(term, termlen, ret, len)) < 0) && !consumed) {
        /* ... and its going to cross entire range ... */
        if (sum + additional >= data + range) {
            /* its the splitting point, determine whether it balances better on 
             * left or right of split and return */
            *smaller = (sum + additional - data < data - sum);
            return terms;
        } else {
            /* its not the splitting point, or even a candidate, just consume 
             * it */
            sum += additional;
            consumed = 1;
            tmpsmaller = 1;
        }

    /* else if additional term is greater than bucket term or already 
     * consumed ... */
    } else if (ret && (consumed || (cmp > 0))) {
        /* ... and bucket term is going to cross entire range */
        if (sum + len + veclen >= data + range) {
            /* its the splitting point, determine whether it balances better on
             * left or right of split and return */
            *smaller = consumed;
            return terms + (sum + len + veclen - data < data - sum);
        } else {
            /* its not the splitting point, or even a candidate, consume it */
            sum += len + veclen;
            tmpsmaller = consumed;
            terms++;
            ret = bucket_next_term(bucketmem, bucketsize, strategy, &iter_state,
                &len, &dataptr, &veclen);
        }

    /* else if they're equal ... */
    } else if (ret && !consumed && !cmp) {
      /* ... check if they will cross entire range ... */
        if (sum + len + veclen + additional >= data + range) {
            /* combination is the splitting point, determine whether it 
             * balances better on left or right of split and return */
            *smaller = (sum + len + veclen + additional - data < data - sum);
            return terms + *smaller;
        } else {
            /* they aren't the splitting point, or even a candidate, consume
             * them */
            sum += len + veclen + additional;
            consumed = 1;
            tmpsmaller = 1;
            terms++;
            ret = bucket_next_term(bucketmem, bucketsize, strategy, &iter_state,
                &len, &dataptr, &veclen);
        }
    } 

    /* haven't found a splitting point thus far, but everything until we exit
     * the range is now a candidate */
    index = -1;
    disp = shortest = UINT_MAX;

    /* XXX: note that code below here hasn't really been debugged, since 
     * ranges haven't been enabled yet */

    /* iterate until we exit range */
    assert(sum >= data - range);
    while (ret && (sum < data + range)) {
        cmp = str_nncmp(term, termlen, ret, len);

        if (!consumed && (cmp < 0)) {
            /* consume additional term */

            /* if its the shortest entry, or equal shortest with smaller
             * displacement from a perfectly balanced bucket, then its our
             * current best candidate for splitting point */
            if ((len < shortest) 
              || ((len == shortest) 
                && (((sum < data) && (data - sum < disp) 
                  && (((sum + additional < data) 
                      && (data - sum - additional < disp))
                    || ((sum + additional > data)
                      && (sum + additional - data < disp))))
                  || ((sum > data) 
                    && (sum + additional - data < disp))))) {

                /* calculate maximum displacement of term from midpoint */
                if (sum < data) {
                    disp = data - sum;
                    if (sum + additional > data) {
                        if (sum + additional - data > disp) {
                            disp = sum + additional - data;
                        }
                    }
                } else {
                    disp = sum + additional - data;
                }
                index = terms;
                shortest = len;
                tmpsmaller = 0;
            }

            sum += additional;
            consumed = 1;
        } else {
            if (!consumed && !cmp) {
                /* consume both terms */
                if ((len < shortest) 
                  || ((len == shortest) 
                    && (((sum < data) && (data - sum < disp) 
                      && (((sum + len + veclen + additional < data) 
                          && (data - sum - len - veclen - additional < disp))
                        || ((sum + len + veclen + additional > data)
                          && (sum + len + veclen + additional - data < disp))))
                      || ((sum > data) 
                        && (sum + len + veclen + additional - data 
                          < disp))))) {

                    /* calculate maximum displacement of term from midpoint */
                    if (sum < data) {
                        disp = data - sum;
                        if (sum + len + veclen + additional > data) {
                            if (sum + len + veclen + additional - data > disp) {
                                disp = sum + len + veclen + additional - data;
                            }
                        }
                    } else {
                        disp = sum + len + veclen + additional - data;
                    }

                    index = terms + 1;
                    shortest = len;
                    tmpsmaller = 0;
                }

                sum += additional + len + veclen;
                consumed = 1;
            } else {
                /* consume term from bucket */
                if ((len < shortest) 
                  || ((len == shortest) 
                    && (((sum < data) && (data - sum < disp) 
                      && (((sum + len + veclen < data) 
                          && (data - sum - len - veclen < disp))
                        || ((sum + len + veclen > data)
                          && (sum + len + veclen - data < disp))))
                      || ((sum > data) 
                        && (sum + len + veclen - data < disp))))) {

                    /* calculate maximum displacement of term from midpoint */
                    if (sum < data) {
                        disp = data - sum;
                        if (sum + len + veclen > data) {
                            if (sum + len + veclen - data > disp) {
                                disp = sum + len + veclen - data;
                            }
                        }
                    } else {
                        disp = sum + len + veclen - data;
                    }
 
                    index = terms + 1;
                    shortest = len;
                    tmpsmaller = consumed;
                }

                sum += len + veclen;
            }
            /* get next term from bucket */
            terms++;
            ret = bucket_next_term(bucketmem, bucketsize, strategy, &iter_state,
                &len, &dataptr, &veclen);
        }
    }

    assert((index != -1) && (disp != UINT_MAX) && (shortest != UINT_MAX));
    *smaller = tmpsmaller;
    return index;
}

