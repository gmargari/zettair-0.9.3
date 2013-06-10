/* def.h defines constants to control the operation of the search
 * engine
 *
 * originally by Hugh Williams
 *
 */

#ifndef DEF_H
#define DEF_H

#include "index.h"   /* needed for constants from interface */

/* general options */

/* whether to produce a core file when things start to go wrong (useful for
 * debugging, obviously not for production systems) */
#ifndef CRASH
#define CRASH 1
#endif

/* whether to perform expensive debugging checks (define to 0 to turn off) */
#ifdef NDEBUG
#define DEAR_DEBUG 0
#else
#ifndef DEAR_DEBUG
#define DEAR_DEBUG 1
#endif
#endif

/* how far to look ahead to verify SGML tags and comments */
#define LOOKAHEAD 999

/* whether to case fold or not */
#define CASE_FOLDING 

/* maximum length of a word */
#define TERMLEN_MAX 1024
#define TERMLEN_DEFAULT 49

/* number of bytes in a punchcard */
#define PUNCHCARD_SIZE (80 * 12)

/* maximum plausible number of computers in a cluster */
#define IBM_CLUSTER_SIZE 5

/* maximum amount of memory anyone could possible need */
#define GATES_MEM_MAX (640 * 1024)

/* filename extensions */
#define INDSUF "index"         /* index file extension name */
#define REPOSSUF "repos"       /* repository file extension name */
#define VECTORSUF "v"          /* vector file extension name */
#define DOCMAPSUF "map"        /* map file extension name */
#define VOCABSUF "vocab"       /* vocabulary file extension name */
#define PARAMSUF "param"       /* parameters file extension name */

/* prompt for query interface */
#define PROMPT "> "

/* General constants */
#define QUERYBUF	2000      /* Size of Query Buffer */
#define QUERY_WORDS 20        /* default maximum number of words read from 
                               * query */

/* whether to spit out timings of build */
#undef TIME_BUILD

/* pyramid_width controls number of temporary files during build */
#define PYRAMID_WIDTH 50

/* default memory constants */
#define TABLESIZE (1024 * 1024)            /* size of postings hashtable */
#define DUMP_BUFFER (1024 * 1024)          /* how much to buffer for dumps */
#define PARSE_BUFFER (1024 * 1024)         /* how much to use for parsing */
#define MERGE_BUFFER (1024 * 1024)         /* how much to use for merging */
#define ACCUMULATOR_LIMIT (20000)          /* how many document accumulators to 
                                              allow */
#define MEMORY_DEFAULT (10 * 1024 * 1024)  /* how much memory to consume by 
                                            * default */

/* memory constants for 'big-and-fast' operation */
#define BIG_TABLESIZE (8 * 1024 * 1024)
#define BIG_PARSE_BUFFER (10 * 1024 * 1024)
#define BIG_MEMORY_DEFAULT (200 * 1024 * 1024)

/* how big term vectors are initially */
#define INITVECLEN 8

/* minimum ratio of the accumulator limit to perform thresholding for.  Note
 * that search may not work correctly unless this value is significantly 
 * smaller than 0.5 */
#define SEARCH_SAMPLES_MIN 0.05  /* 5% */

#endif

