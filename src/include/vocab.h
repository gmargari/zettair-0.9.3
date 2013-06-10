/* vocab.h contains the definition of the structure stored in the
 * vocabulary for each of the terms.  It replaces (though largely keeping the
 * same information) the old hashtable representation.  The vocabulary
 * will actually be stored compressed in a b-tree, this module also
 * offers functions to de/compress vocabulary entries
 *
 * written nml 2003-02-09
 *
 */

#ifndef VOCAB_H
#define VOCAB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>

/* note that _decode and _encode assume that the following enums can fit in a
 * certain number of bits, so you'll have to change them if you add entries */

/* different locations that a vector can be stored */
enum vocab_locations {
    VOCAB_LOCATION_VOCAB = 0,          /* inline, after the entry */
    VOCAB_LOCATION_FILE = 1            /* standalone, in the heap files */
};

/* cardinality of attributes (arbitrary info) bits (note that they can both be
 * on at one time) */
enum vocab_attributes {
    VOCAB_ATTRIBUTES_NONE = 0,         /* no attributes */
    VOCAB_ATTRIBUTES_PERLIST = 1,      /* attributes 1:1 with list */
    VOCAB_ATTRIBUTES_PEROCC = (1 << 1) /* attributes 1:1 with occurrances 
                                        * (1:N with list) */
};

/* different types of vectors that we deal with */
enum vocab_vtype {
    VOCAB_VTYPE_DOC = 0,               /* standard document order,
                                        * f_t: <d, f_dt> */
    VOCAB_VTYPE_DOCWP = 1,             /* document ordered, with word 
                                        * positions,
                                        * f_t: <d, f_dt, (offset)> */
    VOCAB_VTYPE_IMPACT = 2             /* impact ordered 
                                        * f_t: <blocksize, impact (d, f_dt)> */

    /* other possibilities are: access ordered, access ordered with word 
     * positions, frequency ordered, page rank ordered, 
     * page rank ordered with word positions, a list of documents to delete, 
     * some kind of meta-vector to contain vocab vector if it becomes too large.
     * (note that orderings can only (sensibly) have word positions if 
     * they're a global ordering (same for all terms), which only document, 
     * access and page rank are) */
};

/* structure representing an individual vector */
struct vocab_vector {
    enum vocab_attributes attr;
    unsigned int attribute;

    enum vocab_vtype type;

    /* these entries are common to all vector types */
    unsigned long int size;    /* size of stored vector */

    union {
        struct {
            unsigned long int docs;    /* number of documents term occurs in */
            unsigned long int occurs;  /* total number of times term occurrs */
            unsigned long int last;    /* last docno in vector */
        } doc;

        struct {
            unsigned long int docs;    /* number of documents term occurs in */
            unsigned long int occurs;  /* total number of times term occurrs */
            unsigned long int last;    /* last docno in vector */
        } docwp;

        struct {
            unsigned long int docs;    /* number of documents term occurs in */
            unsigned long int occurs;  /* total number of times term occurrs */
            unsigned long int last;    /* last docno in vector */
        } impact;
    } header;

    enum vocab_locations location;     /* location */
    union {
        /* VOCAB entries are the last section of bytes in each entry */
        struct {
            void *vec;                 /* pointer to vector in given memory */
        } vocab;

        /* struct for entries in location FILE */
        struct {
            unsigned int capacity;     /* how much space is there */
            unsigned int fileno;       /* number of file its in */
            unsigned long int offset;  /* offset it's at */
        } file;
    } loc;
};

/* return values that below functions can return */
enum vocab_ret {
    VOCAB_ENOSPC = -ENOSPC,            /* vector didn't contain a full vocab 
                                        * entry */
    VOCAB_EOVERFLOW = -EOVERFLOW,      /* overflow while reading a value */
    VOCAB_EINVAL = -EINVAL,            /* invalid vocab entry */
    VOCAB_OK = 0,                      /* success */
    VOCAB_END = 1                      /* no further entries to read */
};

struct vec;

/* decodes a vocab vector from a (contiguous) series of bytes.  Returns _END if
 * no further vectors follow, _ERR if the bytes don't make sense and _OK if a
 * vector was successfully read.  Note that decode skips over location VOCAB
 * bytes (at end) without reading them. */
enum vocab_ret vocab_decode(struct vocab_vector *vocab, struct vec *v);

/* encodes a vocab vector into a (contiguous) series of bytes.  Returns _ERR 
 * there isn't enough space and _OK if the operation was successful.  No other
 * errors should be possible.  Note that location VOCAB bytes (at end) are 
 * skipped over without writing. (this function should probably encode them into
 * the vector given the vector pointer in the vocab structure, but that assumes
 * that the vocab vector is contiguous in memory). */
enum vocab_ret vocab_encode(struct vocab_vector *vocab, struct vec *v);

/* returns the length in bytes of a vocab vector */
unsigned int vocab_len(struct vocab_vector *vocab);

/* returns the number of docs from a vocab vector */
unsigned long int vocab_docs(struct vocab_vector *vocab);

/* returns the number of occurences from a vocab vector */
unsigned long int vocab_occurs(struct vocab_vector *vocab);

/* returns the last docnum from a vocab vector */
unsigned long int vocab_last(struct vocab_vector *vocab);

#ifdef __cplusplus
}
#endif

#endif

