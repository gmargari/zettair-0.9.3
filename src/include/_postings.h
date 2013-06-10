/* _postings.h contains the private declarations of the postings
 * module, which need to be exposed to some other modules for
 * efficiency reasons 
 *
 * written nml 2003-04-30
 *
 */

#ifndef PRIVATE_POSTINGS_H
#define PRIVATE_POSTINGS_H

#include "vec.h"

struct postings_node {
    char *term;                       /* what this term is */
    struct vec vec;                   /* encoded postings for this term */
    unsigned long int last_offset;    /* last offset encoded */
    unsigned int offsets;             /* number of offsets for current docno */
    unsigned long int last_docno;     /* last docno encoded */
    unsigned int docs;                /* number of documents in current list */
    unsigned int occurs;              /* total number of occurances */
    char *vecmem;                     /* start of vec */
    char *last_count;                 /* where in the vec the last count was 
                                       * encoded */
    struct postings_node *next;       /* next element in hash linked list */
    struct postings_node *update;     /* next element in update linked list */
};

struct postings {
    struct postings_node **hash;      /* implicit hashtable of nodes */
    unsigned int tblsize;             /* number of slots in hash */
    unsigned int tblbits;             /* log2(tblsize) */
    unsigned int dterms;              /* number of items (distinct terms) */
    unsigned int terms;               /* total number of terms in postings */
    unsigned int size;                /* current size of postings */
    unsigned int docs;                /* current number of documents */
    struct poolalloc *string_mem;     /* allocator for strings */
    struct objalloc *node_mem;        /* allocator for nodes */
    struct postings_node *update;     /* first node in list to update */
    unsigned long int docno;          /* the current document number */
    int update_required;              /* whether there are unupdated postings */
    int err;                          /* last error that occurred or 0 */
    struct stop *stop;                /* build-time stoplist */

    /* stemming function and opaque data for it */
    void *stem_opaque;
    void (*stem)(void *opaque, char *term);
};

/* compare two posting_node ** lexographically by term (for qsort) */
int post_cmp(const void *vone, const void *vtwo);

#endif

