/* summarise.h declares methods for creating a textual summary of a document
 * based on query terms
 *
 * written jyiannis 2004-05-18
 *
 */

#ifndef SUMMARISE_H
#define SUMMARISE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "index.h"

enum summarise_ret {
    SUMMARISE_OK = 0,       /* success */

    SUMMARISE_ERR = -1,     /* unexpected error */
    SUMMARISE_EIO = -2,     /* IO error */
    SUMMARISE_ENOMEM = -3   /* couldn't obtain sufficient memory */
};

struct query;
struct summarise;

/* initialise a text summary object, to summarise documents from the given 
 * index */
struct summarise *summarise_new(struct index *idx);

/* delete a text summary object */
void summarise_delete(struct summarise *sum);

/* structure to represent the results of summarisation */
struct summary {
    char *summary;              /* buffer for summary */
    unsigned int summary_len;   /* length of buffer for summary */
    char *title;                /* buffer for document title */
    unsigned int title_len;     /* length of buffer for document title */
};

/* create a textual summary of document number docno, biased toward the given
 * query.  The summary and title are written into result, with the type of
 * summary dictated by type (plain gives a plaintext summary, capitalise has
 * query words capitalised, html has query words bolded and html-significant
 * characters html-escaped). */
enum summarise_ret summarise(struct summarise *sum, unsigned long int docno,
  const struct query *query, enum index_summary_type type,
  struct summary *result);

#ifdef __cplusplus
}
#endif

#endif

