/* makeindex.h declares an interface for procedures to create postings from
 * input documents 
 *
 * originally written Hugh Williams
 *
 * updated nml 2003-03-27
 *
 */

#ifndef MAKEINDEX_H
#define MAKEINDEX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "postings.h"
#include "mime.h"

struct postings;
struct mlparse;
struct stem_cache;
struct psettings;

enum makeindex_ret {
    MAKEINDEX_ERR = -1,                     /* unexpected error */
    MAKEINDEX_OK = 0,                       /* request successful */
    MAKEINDEX_ENDDOC = 1,                   /* received the end of a document */
    MAKEINDEX_EOF = 2,                      /* got the end of the file */

    MAKEINDEX_INPUT = 3                     /* more input required */
    /* MAKEINDEX_OUTPUT = 4 */              /* need to flush postings (currently
                                             * not used)*/
};

/* FIXME: document what has to be initialised by user */
struct makeindex {
    const char *next_in;                    /* input buffer */
    unsigned int avail_in;                  /* size of input buffer */

    unsigned long int docs;                 /* number of documents parsed */

    struct postings *post;                  /* output postings */
    struct postings_docstats stats;         /* document statistics */

    struct makeindex_state *state;          /* opaque state */
};

/* create a new makeindex structure, using configuration settings, with
 * maximum term length termlen.  Returns MAKEINDEX_OK on success, MAKEINDEX_ERR
 * on failure */
enum makeindex_ret makeindex_new(struct makeindex *mi, 
  struct psettings *settings, unsigned int termlen, enum mime_types type);

/* reset the internal state of the makeindex structure */
enum makeindex_ret makeindex_renew(struct makeindex *mi, enum mime_types type);

/* remove the makeindex structure */
void makeindex_delete(struct makeindex *mi);

/* indicate to the makeindex struct that eof has been reached */
void makeindex_eof(struct makeindex *mi);

/* return a count of the number of bytes buffered by the parser within 
 * makeindex */
unsigned int makeindex_buffered(const struct makeindex *mi);

/* return a pointer to the docno received for the last document */
const char *makeindex_docno(const struct makeindex *mi);

/* return the MIME type for the last document */
enum mime_types makeindex_type(const struct makeindex *mi);

/* set internal docno to docno.  Returns MAKEINDEX_OK on success */
enum makeindex_ret makeindex_set_docno(struct makeindex *mi, const char *docno);
enum makeindex_ret makeindex_append_docno(struct makeindex *mi, 
  const char *docno);

/* This function takes text input via the next_in/avail_in members, and 
 * converts it into postings contained within the
 * post member.  After each document has ended, MAKEINDEX_ENDDOC will be
 * returned.  When more input is required, MAKEINDEX_INPUT is returned.  After
 * all input has been exhausted, MAKEINDEX_EOF is returned.  The makeindex
 * module will try to recover from errors, but if anything goes badly wrong,
 * MAKEINDEX_ERR will be returned. */
enum makeindex_ret makeindex(struct makeindex *mi);

#ifdef __cplusplus
}
#endif

#endif

