/* _chash.h contains the implementation details of the chash data
 * structure
 *
 * written nml 2003-05-29
 *
 */

#ifndef PRIVATE_CHASH_H
#define PRIVATE_CHASH_H

#ifdef __cplusplus
extern "C" {
#endif

/* get the current number of elements in the table */
#define CHASH_SIZE(chash) ((const unsigned int) (chash)->elements)

struct chash_link {
    union {
        void *d_ptr;
        unsigned long int d_luint;
        double d_dbl;
        float d_flt;
        /* add other types as needed */
    } data;                             /* data associated with key */

    union {
        void *k_ptr;
        unsigned long int k_luint;
        struct {
            unsigned int len;           /* length of string */
            unsigned int ptr;           /* pointer (offset from start) to 
                                         * string in strings array */
        } k_str;
        /* add other types as needed */
    } key;                              /* lookup key */

    struct chash_link *next;            /* next entry in the table */
    unsigned int hash;                  /* hash value (pre-mod) of this 
                                         * entry */
};

/* the type of the hash elements */
enum chash_type {
    CHASH_TYPE_UNKNOWN,
    CHASH_TYPE_LUINT,
    CHASH_TYPE_PTR,
    CHASH_TYPE_STR,
    CHASH_TYPE_FLT,
    CHASH_TYPE_DBL
};

struct chash {
    unsigned int elements;              /* number of elements currently in 
                                         * table */
    struct chash_link **table;          /* table of ptrs to links */
    unsigned int bits;                  /* table is (2 ** bits) big */

    union {
        unsigned int (*h_ptr)(const void *key);
        /* h_luint won't be used, as we hash int's differently, but its needed
         * for compilation */
        unsigned int (*h_luint)(unsigned long int key);
        unsigned int (*h_dbl)(double key);
        unsigned int (*h_str)(const char *str, unsigned int len);
    } hashfn;                           /* function to hash the data */

    /* not used for primitive types */
    int (*cmpfn)(const void *key1, const void *key2);

    float resize_load;                  /* load to double size at */
    unsigned int resize_point;          /* count to double size at */
    struct objalloc *alloc;             /* link allocator */

    /* structure to store strings stored in the hashtable (that aren't small 
     * enough to be completely stored within the prefix/suffix ints) */
    struct {
        char *strings;                  /* strings area */
        unsigned int used;              /* number of free bytes in strings */
        unsigned int size;              /* size of strings array in bytes */
        unsigned int unpacked;          /* number of bytes we can reclaim by 
                                         * repacking */
    } strings;

    enum chash_type key_type;
    enum chash_type data_type;

    unsigned int timestamp;             /* allows checks that hash doesn't 
                                         * get modified during iteration */
};

#ifdef __cplusplus
}
#endif

#endif

