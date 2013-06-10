/* _docmap.h provides access to the internals of docmap for those who need 
 * to use them.
 *
 * This provides direct access to the measures and location arrays
 * for efficiency.  
 *
 * written wew 2004-10-06
 *
 */

#ifndef PRIVATE_DOCMAP_H
#define PRIVATE_DOCMAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "docmap.h"
#include "vec.h"
#include "zstdint.h"

/* macros/fns for fast access to loaded entries */
#define DOCMAP_GET_WORDS(docmap, docno) docmap->cache.words[docno] 
#define DOCMAP_GET_DISTINCT_WORDS(docmap, docno) docmap->cache.dwords[docno] 
#define DOCMAP_GET_WEIGHT(docmap, docno) docmap->cache.weight[docno] 
unsigned int docmap_get_bytes_cached(struct docmap *dm, unsigned int docno);

struct fdset;

/* represents a single entry within the docmap */
struct docmap_entry {
    off_t offset;                     /* offset within repository file */
    unsigned int docno;               /* doc number */
    unsigned int fileno;              /* repository file number */
    unsigned int dwords;              /* number of distinct words in doc */
    unsigned int words;               /* number of words in doc */
    float weight;                     /* the cosine weight of the doc */
    unsigned int bytes;               /* size of doc in bytes */
    enum mime_types mtype;            /* mime type of document */
    enum docmap_flag flags;

    char *trecno;                     /* TREC id number for doc */
    unsigned int trecno_len;          /* length of current TREC id */
    unsigned int trecno_size;         /* size of trecno buffer */
};

/* represents a buffer with current decoding position */
struct docmap_buffer {
    unsigned int page;                /* page number occupying buffer */
    char *buf;                        /* buffer */
    unsigned int buflen;              /* length of buffer (in pages) */
    unsigned int bufsize;             /* capacity of buffer (in pages) */
    int dirty;                        /* whether buffer requires writing */
};

/* structure representing a read/write cursor */
struct docmap_cursor {
    unsigned long int first_docno;    /* first docno in pos page */
    unsigned long int last_docno;     /* first docno not in pos page */
    struct vec pos;                   /* (de)encoding position */
    struct docmap_entry entry;        /* entry previously (de)encoded */
    unsigned int past;                /* number of entries past */
    uint32_t entries;                 /* number of entries in pos page */
    struct docmap_buffer *buf;        /* buffer we're operating on */
    unsigned int page;                /* page we're currently in */
};

/* structure to represent a cache buf */
struct docmap_cbuf {
    char *buf;
    unsigned int len;
    unsigned int size;
};

/* describes a type exception */
struct docmap_type_ex {
    unsigned int docno;
    enum mime_types mtype;
};

struct docmap {
    struct fdset *fdset;
    struct reposset *rset;
    int fd_type;
    unsigned int pagesize;            /* size of each page */
    unsigned long int entries;        /* number of entries in docmap */
    unsigned long int max_filesize;   /* maximum filesize in bytes */
    unsigned int file_pages;          /* maximum number of pages in a file */

    struct docmap_buffer readbuf;     /* read buffer */
    struct docmap_buffer appendbuf;   /* append buffer */
    char *buf;                        /* allocated buffer */

    struct docmap_cursor read;        /* read cursor */
    struct docmap_cursor write;       /* write cursor */

    unsigned long int *map;           /* array of the first docno in each 
                                       * page */
    unsigned int map_size;            /* capacity of page array */
    unsigned int map_len;             /* number of pages in docmap */

    int dirty;                        /* whether the docmap has changed since 
                                       * being loaded */

    struct {
        enum docmap_cache cache;      /* whether we're caching anything in 
                                       * main memory */
        unsigned int len;             /* number of entries cached */
        unsigned int size;            /* size of cache arrays */
        unsigned int *words;          /* length-in-words cache */
        unsigned int *dwords;         /* length-in-distinct-words cache */
        float *weight;                /* cosine weight cache */

        unsigned int *trecno_off;     /* trec docnos, as offsets 
                                       * into trecno array */
        struct docmap_cbuf trecno;    /* buffer for front-coded trecnos */

        unsigned int *loc_off;        /* length-in-bytes cache, as offsets 
                                       * into loc array */
        struct docmap_cbuf loc;       /* buffer for encoded locations */

        struct docmap_type_ex *typeex;/* type exceptions (not X_TREC) */
        unsigned int typeex_len;      /* utilised length of typeex array */
        unsigned int typeex_size;     /* capacity of typeex array */
    } cache;

    /* aggregate quantities */
    struct {
        double avg_words;
        double sum_words;
        double avg_dwords;
        double sum_dwords;
        double avg_bytes;
        double sum_bytes;
        double avg_weight;
        double sum_weight;           /* note that sum_weight isn't updated 
                                      * except when weights are being cached, 
                                      * due to cost of calculating square 
                                      * roots */

        double sum_trecno;           /* total length of trecno entries */
    } agg;
};

#ifdef __cplusplus
}
#endif

#endif 

