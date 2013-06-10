/* docmument map interface.  The docmap holds all per-document data, 
 * including document sizes, locations, word/byte counts and identifiers.
 *
 * written wew 2004-09-24
 *
 */

#ifndef DOCMAP_H
#define DOCMAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "mime.h"

struct freemap;
struct fdset;

enum docmap_ret {
    DOCMAP_OK = 0,
    DOCMAP_MEM_ERROR = -1,      /* out of memory */
    DOCMAP_IO_ERROR = -2,       /* I/O error */
    DOCMAP_BUFSIZE_ERROR = -3,  /* provided buffer too small */
    DOCMAP_FMT_ERROR = -4,      /* internal format error */
    DOCMAP_ARG_ERROR = -5       /* provided argument was incorrect */
};

enum docmap_flag {
    DOCMAP_NO_FLAGS = 0,
    DOCMAP_COMPRESSED = (1 << 0)  /* doc src stored compressed in repos */
};

enum docmap_cache {
    DOCMAP_CACHE_NOTHING = 0,
    DOCMAP_CACHE_LOCATION = (1 << 1),
    DOCMAP_CACHE_WORDS = (1 << 2),
    DOCMAP_CACHE_DISTINCT_WORDS = (1 << 3),
    DOCMAP_CACHE_WEIGHT = (1 << 4),
    DOCMAP_CACHE_TRECNO = (1 << 5)
};

struct docmap;

/*
 *  Create a new, empty docmap.
 *
 *  Returns the new docmap or NULL on error.  The status code is set in
 *  RET.  PAGES is the number of docmap pages to hold in memory.  MAX_FILESIZE
 *  governs how often the docmap starts a new file (in bytes).  CACHE 
 *  determines  what quantities are held in-memory by the docmap 
 *  (can be changed later using docmap_cache())
 *
 *  Status values:
 *
 *  DOCMAP_OK         - new docmap created ok
 *  DOCMAP_MEM_ERROR  - out of memory.
 */
struct docmap *docmap_new(struct fdset *fdset, 
  int fd_type, unsigned int pagesize, unsigned int pages, 
  unsigned long int max_filesize, enum docmap_cache cache, 
  enum docmap_ret *ret);

/*
 *  Load a docmap.
 *
 *  Arguments are identical to those for docmap_new() 
 *  Returns the loaded docmap or NULL on error.  The status code is set in
 *  RET.
 *
 *  Status values:
 *
 *  DOCMAP_OK          - new docmap loaded ok
 *  DOCMAP_MEM_ERROR   - out of memory
 *  DOCMAP_IO_ERROR    - error reading off disk
 *  DOCMAP_FMT_ERROR   - error in the on-disk format
 */
struct docmap *docmap_load(struct fdset *fdset, 
  int fd_type, unsigned int pagesize, unsigned int pages, 
  unsigned long int max_filesize, enum docmap_cache cache, 
  enum docmap_ret *ret);

/*
 *  Save the docmap to disk.
 *
 *  Return values:
 *
 *  DOCMAP_OK        - save successfully
 *  DOCMAP_MEM_ERROR - error allocating internal memory
 *  DOCMAP_IO_ERROR  - error writing to disk
 */
enum docmap_ret docmap_save(struct docmap *docmap);

/*
 *  Delete a docmap.
 */
void docmap_delete(struct docmap *docmap);

/*
 *  Add a document to the docmap.
 *
 *  DOC_INFO specifies the document to add.  The docmap will set 
 *  DOCNO to the new document number; the passed-in value
 *  is ignored.  TRECNO_LEN specifies the length of the TRECNO entry
 *  (NUL-termination is ignored)
 *
 *  Return values:
 *
 *  DOCMAP_OK         - new document added
 *  DOCMAP_MEM_ERROR  - out of memory
 *  DOCMAP_IO_ERROR   - I/O error loading or flushing buffers 
 *  DOCMAP_FMT_ERROR  - format error with loaded buffer.
 */
enum docmap_ret docmap_add(struct docmap *docmap, 
  unsigned int sourcefile, off_t offset, 
  unsigned int bytes, enum docmap_flag flags, 
  unsigned int words, unsigned int distinct_words,
  float weight, const char *trecno, unsigned trecno_len, 
  enum mime_types type, unsigned long int *docno);

/*
 *  Get the TREC document number for a document.
 *
 *  DOCNO is the document number to retrieve.  It must be less than
 *  the current number of documents.
 *
 *  TRECNO_BUF must be an allocated buffer of length TRECNO_BUF_LEN.  The
 *  length of the DOCNO, even if it is truncated by insufficient space, is
 *  set in TRECNO_LEN.
 *
 *  Return values:
 *
 *  DOCMAP_OK            - auxiliary retrieved ok
 *  DOCMAP_MEM_ERROR     - error allocating memory internally
 *  DOCMAP_FMT_ERROR     - internal format error
 *  DOCMAP_IO_ERROR      - error reading info from disk
 */
enum docmap_ret docmap_get_trecno(struct docmap *docmap, 
  unsigned long int docno, char *trecno_buf, unsigned trecno_buf_len, 
  unsigned *trecno_len);

/*
 *  Get the location information for a document.
 *
 *  DOCNO must be less than the number of documents.
 *  The required information is written out into SOURCEFILE, OFFSET,
 *  BYTES, and TYPE.
 *
 *  Return values:
 *
 *  DOCMAP_OK           - location information retrieved ok.
 *  DOCMAP_MEM_ERROR    - error allocating memory internally
 *  DOCMAP_FMT_ERROR    - error in on-disk format
 *  DOCMAP_IO_ERROR     - error reading info from disk
 */
enum docmap_ret docmap_get_location(struct docmap *docmap,
  unsigned long int docno, unsigned int *sourcefile, 
  off_t *offset, unsigned int *bytes, enum mime_types *type,
  enum docmap_flag *flags);

/*
 *  Get the number of bytes for a document.
 *
 *  DOCNO must be less than the number of documents.
 *  The required information is written out into BYTES.
 *
 *  Return values:
 *
 *  DOCMAP_OK        - byte information retrieved ok.
 *  DOCMAP_MEM_ERROR    - error allocating memory internally
 *  DOCMAP_FMT_ERROR    - error in on-disk format
 *  DOCMAP_IO_ERROR     - error reading info from disk
 */
enum docmap_ret docmap_get_bytes(struct docmap *docmap,
  unsigned long int docno, unsigned int *bytes);

/*
 *  Get the number of words for a document.
 *
 *  DOCNO must be less than the number of documents.
 *  The required information is written out into BYTES.
 *
 *  Return values:
 *
 *  DOCMAP_OK         - word information retrieved ok.
 *  DOCMAP_MEM_ERROR  - error allocating memory internally
 *  DOCMAP_FMT_ERROR  - error in on-disk format
 *  DOCMAP_IO_ERROR   - error reading info from disk
 */
enum docmap_ret docmap_get_words(struct docmap *docmap,
  unsigned long int docno, unsigned int *words);

/*
 *  Get the number of distinct words for a document.
 *
 *  DOCNO must be less than the number of documents.
 *  The required information is written out into BYTES.
 *
 *  Return values:
 *
 *  DOCMAP_OK         - distinct word information retrieved ok.
 *  DOCMAP_MEM_ERROR  - error allocating memory internally
 *  DOCMAP_FMT_ERROR  - error in on-disk format
 *  DOCMAP_IO_ERROR   - error reading info from disk
 */
enum docmap_ret docmap_get_distinct_words(struct docmap *docmap,
  unsigned long int docno, unsigned int *distinct_words);

/*
 *  Get the weight of a document.
 *
 *  DOCNO must be less than the number of documents.
 *  The required information is written out into BYTES.
 *
 *  Return values:
 *
 *  DOCMAP_OK         - weight information retrieved ok.
 *  DOCMAP_MEM_ERROR  - error allocating memory internally
 *  DOCMAP_FMT_ERROR  - error in on-disk format
 *  DOCMAP_IO_ERROR   - error reading info from disk
 */
enum docmap_ret docmap_get_weight(struct docmap *docmap,
  unsigned long int docno, double *weight);

/*
 *  Get the average number of bytes per document.
 *
 *  The required information is written out into AVG_BYTES.
 *
 *  Return values:
 *
 *  DOCMAP_OK         - information retrieved ok.
 *  DOCMAP_MEM_ERROR  - error allocating memory internally
 *  DOCMAP_FMT_ERROR  - error in on-disk format
 *  DOCMAP_IO_ERROR   - error reading info from disk
 */
enum docmap_ret docmap_avg_bytes(struct docmap *docmap, double *avg_bytes);

/*
 *  Get the total number of bytes for all documents.
 *
 *  The required information is written out into TOTAL_BYTES.
 *
 *  Return values:
 *
 *  DOCMAP_OK         - information retrieved ok.
 *  DOCMAP_MEM_ERROR  - error allocating memory internally
 *  DOCMAP_FMT_ERROR  - error in on-disk format
 *  DOCMAP_IO_ERROR   - error reading info from disk
 */
enum docmap_ret docmap_total_bytes(struct docmap *docmap, double *total_bytes);

/*
 *  Get the average weight of each document.  Note that, unlike the other
 *  average functions, docmap_avg_weight requires that the weights be held in
 *  cache by the docmap, else DOCMAP_ARG_ERROR is returned.
 *
 *  The required information is written out into AVG_WEIGHT.
 *
 *  Return values:
 *
 *  DOCMAP_OK         - information retrieved ok.
 *  DOCMAP_MEM_ERROR  - error allocating memory internally
 *  DOCMAP_FMT_ERROR  - error in on-disk format
 *  DOCMAP_IO_ERROR   - error reading info from disk
 *  DOCMAP_ARG_ERROR  - weights not held in cache
 */
enum docmap_ret docmap_avg_weight(struct docmap *docmap, double *avg_weight);

/*
 *  Get the average number of words per document.
 *
 *  The required information is written out into AVG_WORDS.
 *
 *  Return values:
 *
 *  DOCMAP_OK         - information retrieved ok.
 *  DOCMAP_MEM_ERROR  - error allocating memory internally
 *  DOCMAP_FMT_ERROR  - error in on-disk format
 *  DOCMAP_IO_ERROR   - error reading info from disk
 */
enum docmap_ret docmap_avg_words(struct docmap *docmap, double *avg_words);

/*
 *  Get the average number of distinct words per document.
 *
 *  The required information is written out into AVG_DISTINCT_WORDS.
 *
 *  Return values:
 *
 *  DOCMAP_OK         - information retrieved ok.
 *  DOCMAP_MEM_ERROR  - error allocating memory internally
 *  DOCMAP_FMT_ERROR  - error in on-disk format
 *  DOCMAP_IO_ERROR   - error reading info from disk
 */
enum docmap_ret docmap_avg_distinct_words(struct docmap *docmap,
  double *avg_distinct_words);

/* Get the number of documents in the docmap */
unsigned long int docmap_entries(struct docmap *docmap);

/* Get a string described a docmap error code. */
const char *docmap_strerror(enum docmap_ret nd_ret);

/* find out what values are currently cached by the docmap */
enum docmap_cache docmap_get_cache(struct docmap *docmap);

/* set the values for the docmap to cache (this may cause the docmap to read 
 * all values from disk) */
enum docmap_ret docmap_cache(struct docmap *docmap, 
  enum docmap_cache cache);

#ifdef __cplusplus
}
#endif

#endif

