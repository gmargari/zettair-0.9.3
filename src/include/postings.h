/* postings.h declares a structure that accumulates encoded terms and
 * vectors (a postings list) and then dumps them to a file for merging
 *
 * if you didn't understand that you need to read up on search
 * engines, inverted (postings) lists 
 *
 * this postings list generates postings in a form that is
 * compatible with merger.[ch]
 *
 * based on code written by Hugh Williams
 *
 * written nml 2003-03-9
 *
 */

#ifndef POSTINGS_H
#define POSTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* users of postings should note that to add to the postings, you must
 * now use the sequence (adddoc(), addword()*, update())*, dump() to
 * get sensible postings out */

struct postings;
struct vec;
struct stop;

/* structure to allow returning of statistics from update.  More per
 * document stats can easily be added and calculated */
struct postings_docstats {
    float weight;                      /* vector space length of document */
    unsigned int terms;                /* number of terms in document */
    unsigned int distinct;             /* number of distinct terms in document*/
};

/* constructor, creates a new (empty) postings list.  tablesize is
 * the size of the hashtable to create.  stem and opaque define the stemming to
 * be used, where both can be NULL (for no stemming).  stop is the build-time
 * stoplist to be used, or NULL for none. */
struct postings* postings_new(unsigned int tablesize, 
  void (*stem)(void *opaque, char *term), void *opaque, struct stop *list);

/* remove a postings structure */
void postings_delete(struct postings *post);

/* add a document to the postings list.  All subsequent words added
 * will be added to this document.  If addword is called prior to this
 * function, it will add to document 0 */
void postings_adddoc(struct postings *post, unsigned long int docno);

/* a reference to a word in a document, identified by term, wordno and docno
 * respectively.  Returns true on success and 0 on failure. vocab is a pointer
 * to the current vocabulary, can be NULL, and is used to look up the last docno
 * encoded for this word.  Note that the term will be stemmed in this function
 * if needed. */
int postings_addword(struct postings *post, char *term, 
  unsigned long int wordno);

/* whether postings list needs an update */
int postings_needs_update(struct postings *post);

/* add the current document to the accumulated postings.  Note that
 * this action must be taken prior to a new adddoc or dump. */
int postings_update(struct postings *post, struct postings_docstats *stats);

/* dump the postings to a file, returning true on success and 0 on
 * failure */
int postings_dump(struct postings *post, void *buf, unsigned int bufsize, 
  int fd);

/* remove all postings from the postings list */
void postings_clear(struct postings *post);

/* return the size of the postings held in memory */
unsigned int postings_size(struct postings *post);

/* return the size of the total postings structure in memory */
unsigned int postings_memsize(struct postings *post);

/* returns the last error encountered by the postings list, or 0 */
int postings_err(struct postings *post);

/* returns the number of terms that have accumulated postings */
unsigned int postings_distinct_terms(struct postings *post);

/* returns the number of total terms that have been recorded */
unsigned int postings_total_terms(struct postings *post);

/* returns the number of documents in the postings */
unsigned int postings_documents(struct postings *post);

/* returns a vector for a given word.  Remember to stem (if required) term
 * before passing it to this function. */
struct vec *postings_vector(struct postings *post, char *term);

#ifdef __cplusplus
}
#endif

#endif

