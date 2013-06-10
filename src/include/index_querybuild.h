/* index_querybuild.h declares a query structure and a function to
 * construct it from a query string
 *
 * written nml 2003-06-02 (moved from index_search.c 2003-09-23)
 *
 */

#ifndef INDEX_QUERYBUILD_H
#define INDEX_QUERYBUILD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "vocab.h"

struct index;

/* struct to hold information about a word being queried for */
struct term {
    struct term *next;                    /* linked list of terms */
    struct vocab_vector vocab;            /* vocab term */
    char *term;                           /* term text */
    char *vecmem;                         /* pointer to vector for this word */
};

/* types of conjunction (in priority order) */
enum conjunct_type {
    CONJUNCT_TYPE_EXCLUDE = 0,            /* single word that needs to be 
                                           * excluded from results */
    CONJUNCT_TYPE_PHRASE = 1,             /* phrase */
    CONJUNCT_TYPE_AND = 2,                /* AND */
    CONJUNCT_TYPE_WORD = 3                /* its just a single word */
};

/* struct to hold a linked list of conjuct terms for querying */
struct conjunct {
    struct term term;                     /* first term */
    unsigned int terms;                   /* number of terms */
    unsigned int f_t;                     /* total estimated freq of conjunct */
    unsigned int F_t;                     /* total estimated occurances of 
                                           * conjunct */
    unsigned int f_qt;                    /* its frequency in the query */
    char *vecmem;                         /* base pointer for vector memory */
    unsigned int vecsize;                 /* size of vector memory */
    enum conjunct_type type;              /* type of the conjuct, see enum */
    unsigned int sloppiness;              /* sloppiness of phrase */
    unsigned int cutoff;                  /* must find phrase in this number 
                                           * of words (0 means infinite) */
};

/* struct to hold a parsed query */
struct query {
    unsigned int terms;                   /* number of terms in query */
    struct conjunct *term;                /* array of terms in query */
};

/* function to construct a query structure from a given string
 * (querystr) of length len.  At most maxwords will be read from the query. */ 
unsigned int index_querybuild(struct index *idx, struct query *query, 
  const char *querystr, unsigned int len, unsigned int maxwords, 
  unsigned int maxtermlen, int impact);

#ifdef __cplusplus
}
#endif

#endif

