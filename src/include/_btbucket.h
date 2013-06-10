/* _btbucket.h declares the internal details of a btree bucket and
 * some macros for fast operation
 *
 * written nml 2003-02-17
 *
 */

#ifndef PRIVATE_BTBUCKET__H
#define PRIVATE_BTBUCKET__H

#ifdef __cplusplus
extern "C" {
#endif

#include "zstdint.h"

struct btbucket {
    union {
        char mem[1];           /* pointer to allow access to the raw btree 
                                * bucket memory using a struct hack.  Extra 
                                * memory will be allocated to allow this array 
                                * to be of size pagesize. */

        /* struct representing logical information inside mem array.  
         *
         *   - tailsize:   indicates the length of the information trailing the 
         *                 bucket in mem.  Also includes overhead bytes at the 
         *                 front of the page and prefixsize.
         *
         *   - prefixsize: The bottom 7 bits of the prefixsize indicate how long
         *                 a prefix that this btree bucket has. The top bit 
         *                 indicates whether the bucket is an internal 
         *                 node (0) or a leaf (1).  Note that this limits the
         *                 prefixsize value to 128.  This entry also ensures
         *                 that the alignment of the bucket array is exactly 
         *                 two bytes off of four byte alignment.
         *
         *   - bucket:     struct hack array that gives access to bucket memory.
         *                 Note that the bucket memory will start at mem[2] 
         *                 which ensures that the bucket memory alignment is 2 
         *                 bytes off of 4 byte alignment */
        struct {
            uint8_t tailsize;
            uint8_t prefixsize;
            char bucket[1];
        } s;

        unsigned int dummy;    /* dummy variable, used to ensure that mem is 
                                * aligned to integer boundary */
    } u;

    /* the prefix and then the sibling pointers are placed at the end of the 
     * bucket in prefix, fileno, offset order.  It would be nice to represent 
     * them in the struct somehow, but their lack of alignment prevents that. */
};

/* macro to return the size of a btree bucket */
#define BTBUCKET_SIZE(bucket, bucketsize)                                     \
    ((bucketsize)                                                             \
      - (BIT_LMASK(7) & ((struct btbucket *) (bucket))->u.s.tailsize))
         
/* macro to return the offset to the bucket within a btree bucket */
#define BTBUCKET_BUCKET(bbucket)                                              \
    (((struct btbucket *) bbucket)->u.s.bucket)

/* macro to determine whether the btree bucket is a leaf node */
#define BTBUCKET_LEAF(bucket, size)                                           \
    BIT_GET(((struct btbucket *) bucket)->u.s.prefixsize, 7)

/* macro to return a pointer to the prefix of the bucket */
#define BTBUCKET_PREFIX(bbucket, size, prefixlen_ptr)                         \
    ((*prefixlen_ptr                                                          \
      = BIT_SET(((struct btbucket *) bbucket)->u.s.prefixsize, 7, 0)),        \
        &((struct btbucket *) bbucket)->u.s.bucket[BTBUCKET_SIZE((bbucket),   \
          (size))])

/* size of a btbucket internal node entry */
#define BTBUCKET_ENTRY_SIZE() (sizeof(unsigned int) + sizeof(unsigned long int))

/* decodes a btbucket internal node entry (entry must be of size
 * btbucket_entry_size) */
#define BTBUCKET_ENTRY(entryptr, filenoptr, offsetptr)                        \
    MEM_NTOH((filenoptr), (entryptr), sizeof(unsigned int));                  \
    MEM_NTOH((offsetptr), ((char *) entryptr) + sizeof(unsigned int),         \
      sizeof(unsigned long int));

/* encodes a btbucket internal node entry (entry must be of size
 * btbucket_entry_size) */
#define BTBUCKET_SET_ENTRY(entryptr, fileno, offset)                          \
    MEM_NTOH((entryptr), &(fileno), sizeof(unsigned int));                    \
    MEM_NTOH(((char *) entryptr) + sizeof(unsigned int), &(offset),           \
      sizeof(unsigned long int));

#ifdef __cplusplus
}
#endif

#endif

