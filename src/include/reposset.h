/* reposset.h declares an interface for a collection of information
 * about the collection indexed.  This information includes the range
 * of document numbers assigned to documents inside the repositories,
 * the compression (or lack thereof) applied to each repository and so
 * forth.
 *
 * written nml 2006-04-21
 *
 */

#ifndef REPOSSET
#define REPOSSET

#ifdef __cplusplus
extern "C" {
#endif

#include "mime.h"

enum reposset_ret {
    REPOSSET_OK = 0,              /* operation succeeded */
    REPOSSET_ERR = -1,            /* unexpected error */
    REPOSSET_ENOMEM = -2,         /* out-of-memory error */
    REPOSSET_ENOENT = -3,         /* no such entry */
    REPOSSET_EINVAL = -4          /* invalid argument (translation: programmer 
                                   * error) */
};

struct reposset;

struct reposset *reposset_new();
void reposset_delete(struct reposset *rset);

/* add a new repository.  reposno will contain the number assigned to this
 * repository. */
enum reposset_ret reposset_append(struct reposset *rset, 
  unsigned int start_docno, unsigned int *reposno);

/* append a docno or set of docnos onto the last
 * repository in the set */
enum reposset_ret reposset_append_docno(struct reposset *rset, 
  unsigned int start_docno, unsigned int docs);

/* add a checkpoint to a repository.  A checkpoint is a point in a
 * *compressed* file (don't do this to files that aren't compressed)
 * from where decompression can start.  Note that compression_type must
 * currently either be GZIP or BZIP2. */
enum reposset_ret reposset_add_checkpoint(struct reposset *rset, 
  unsigned int reposno, enum mime_types compression_type,
  unsigned long int point);

/* translate a docno to a reposno */
enum reposset_ret reposset_reposno(struct reposset *rset, unsigned int docno, 
  unsigned int *reposno);

/* returns the number of repositories in the set */
unsigned int reposset_entries(struct reposset *rset);

/* clear a reposset of all records */
void reposset_clear(struct reposset *db);

#ifdef __cplusplus
}
#endif

#endif

