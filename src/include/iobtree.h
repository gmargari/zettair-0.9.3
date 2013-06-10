/* ioiobtree.h declares an interface to a btree datastructure that
 * performs IO internally to the btree using POSIX read/write calls.  This btree
 * allows both variable length keys and variable length values.
 *
 * actually, the btree is a b+ tree, and i haven't implemented
 * implement utilisation guarantees with deleting, since its not particularly
 * interesting to me.  See "The Ubiquitous B-tree" by Comer for a survey
 * of btree techniques.  Datastructures and algorithms textbooks should also
 * have descriptions of them.
 *
 * the current version doesn't handle the extended character set within words
 * properly while splitting buckets :o(
 *
 * written nml 2004-01-06
 *
 */

#ifndef IOBTREE_H
#define IOBTREE_H

#ifdef __cplusplus
extern "C" {
#endif

struct iobtree;
struct freemap;
struct fdset;

/* create a new btree, with page size pagesize (note that this will constrain
 * the size of the objects you insert into it to be less than a quarter of the
 * pagesize), with bucket strategies leaf_strategy and node_strategy for leaves
 * and nodes respectively.  freemap provides space management, with access to
 * the files through fdset (using fdset_type as the type). */
struct iobtree *iobtree_new(unsigned int pagesize, int leaf_strategy, 
  int node_strategy, struct freemap *freemap, struct fdset *fds, 
  unsigned int fdset_type);

/* load a previously written btree.  All parameters except root_fileno and
 * root_offset are the same as for iobtree_new, and must be the same as when 
 * the btree was created.  root_fileno and root_offset specify where the root 
 * of the btree is on disk.  On load, the entire btree will be read once so 
 * that it can be removed from the freemap if the freemap is not NULL */
struct iobtree *iobtree_load(unsigned int pagesize, int leaf_strategy,
  int node_strategy, struct freemap *freemap, struct fdset *fds, 
  unsigned int fdset_type, unsigned int root_fileno, 
  unsigned long int root_offset);

/* load_quick does the same thing as load except it avoids reading the btree 
 * by not modifying the btree (you have to do this yourself) and by having the
 * caller supply the number of entries in the btree */
struct iobtree *iobtree_load_quick(unsigned int pagesize, int leaf_strategy,
  int node_strategy, struct freemap *freemap, struct fdset *fds, 
  unsigned int fdset_type, unsigned int root_fileno, 
  unsigned long int root_offset, unsigned long int entries);

/* delete a btree object.  You probably want to flush it to disk first 
 * though */
void iobtree_delete(struct iobtree *iobtree);

/* flush a btree object to disk.  Returns true on success. */
int iobtree_flush(struct iobtree *iobtree);

/* allocate some space of size size to key term, termlen.  Returns a pointer to
 * the space if successful.  If unsuccessful, sets *toobig to true if the
 * requested space is too big to ever fit into the tree.  Keys should be
 * distinct, the result of inserting a duplicate key is undefined.  NOTE: 
 * Returned pointer is good until the next call to iobtree, don't use it after 
 * that. */
void *iobtree_alloc(struct iobtree *iobtree, const char *term, 
  unsigned int termlen, unsigned int size, int *toobig);

/* change the amount of space allocated to key term, termlen to newsize (which
 * can be larger or smaller than original allocation).  Returns a pointer to 
 * the new space on success, writes *toobig to let you know if the request 
 * was too big on failure.  The old contents associated with the key is 
 * preserved as far as is possible (will be truncated for shrinking calls, 
 * otherwise will be preserved).  This function will return NULL if the key 
 * cannot be found.  NOTE: returned pointer is good until the next call to 
 * iobtree */
void *iobtree_realloc(struct iobtree *iobtree, const char *term, 
  unsigned int termlen, unsigned int newsize, int *toobig);

/* remove a key term, termlen and entry associated with it from the btree.
 * Returns true on success. */
int iobtree_remove(struct iobtree *iobtree, const char *term, 
  unsigned int termlen);

/* find the entry associated with key term, termlen in the btree.  Returns a
 * pointer to it and writes its length into veclen on success.  write indicates
 * whether you are going to change the returned entry, so that the btree can
 * flush it to disk if necessary.  NOTE: returned pointer is good until the 
 * next call to iobtree, don't use it after that. */
void *iobtree_find(struct iobtree *iobtree, const char *term, 
  unsigned int termlen, int write, unsigned int *veclen);

/* append a new term, of length termlen, to the end of the btree.  
 * Successive application of this call can be used to bulk-load a btree.  Note 
 * that it is the caller's responsibility to ensure that the new term is 
 * lexically the greatest currently in the btree.  Returns a pointer to the 
 * data area allocated by this call, which is of length veclen, or NULL on
 * failure. */
void *iobtree_append(struct iobtree *iobtree, const char *term,
  unsigned int termlen, unsigned int veclen, int *toobig);

/* iterate over all of the entries in the btree.  Returns each a pointer to 
 * each term, writing its length, a data pointer and the data length into 
 * *termlen, *data and *len respectively.  Pointers are only good until next 
 * call to iobtree.  state must be an array of 3 unsigned ints, which you must
 * initialise to 0 prior to the first call and then leave alone.  Will return
 * NULL once no more terms are available. */
const char *iobtree_next_term(struct iobtree *iobtree, 
  unsigned int *state, unsigned int *termlen, 
  void **data, unsigned int *len);

/* new interface stuff */

enum iobtree_ret {
    IOBTREE_ERR = -1,
    IOBTREE_EIO = -2,
    IOBTREE_ENOENT = -3,
    IOBTREE_ENOMEM = -4,
    IOBTREE_EINTR = -5,
    IOBTREE_EAGAIN = -6,

    IOBTREE_OK = 0,
    IOBTREE_ITER_FINISH = 1
};

/* read a portion of an entry from the btree.  Up to buflen bytes are read 
 * from offset within the btree entry into buf.  The total size of the entry 
 * is written into *veclen on success.  The number of bytes read is written 
 * into *numread on success */
enum iobtree_ret iobtree_read(struct iobtree *iobtree, const char *term, 
  unsigned int termlen, unsigned int *veclen, unsigned int offset, void *buf, 
  unsigned int buflen, unsigned int *numread);

/* write part of an entry into the btree.  If the entry doesn't exist it 
 * will be inserted with size offset + buflen.  If the entry exists it will 
 * be widened if necessary to hold offset + buflen bytes, but will be 
 * shortened to the larger of offset + buflen bytes and maxveclen bytes */
enum iobtree_ret iobtree_write(struct iobtree *iobtree, const char *term, 
  unsigned int termlen, unsigned int maxveclen, unsigned int offset, 
  const void *buf, unsigned int buflen);

/* iteration functions */

struct iobtree_iter;

/* create a new iterator over the btree, starting at term (with length 
 * termlen), which can be NULL, 0 if iteration from the start is desired.  Note
 * that you don't have to call iobtree_iter_next immediately after this, 
 * the iterator is initialised to the first requested term. */
struct iobtree_iter *iobtree_iter_new(struct iobtree *iobtree, 
  const char *term, unsigned int termlen);

/* delete an iterator */
void iobtree_iter_delete(struct iobtree_iter *iter);

/* read the key that the iteration is currently up to.  a maximum of termbuflen
 * bytes of key are written into termbuf on successful return, with the length
 * of the term written into termlen.  Note that keys can't be any greater than
 * 1/4 of a page if they are resident in the btree. */
enum iobtree_ret iobtree_iter_curr(struct iobtree_iter *iter,
  char *termbuf, unsigned int termbuflen, unsigned int *termlen, 
  unsigned int *datalen);

/* move forward in the iteration to another term.  If termlen is 0, then the
 * iteration moves to the next term, otherwise iteration moves to the smallest
 * term not smaller than the given seek term.  once a term is found, a maximum
 * of termbuflen bytes of it are written into termbuf, with the length of the
 * term written into *termlen, similar to iobtree_iter_curr. */
enum iobtree_ret iobtree_iter_next(struct iobtree_iter *iter, 
  char *termbuf, unsigned int termbuflen, unsigned int *termlen,
  const char *seekterm, unsigned int seektermlen);

/* blech, iterator equivalent of old interface _alloc, for convenience */
void *iobtree_iter_alloc(struct iobtree_iter *iter,
  const char *term, unsigned int termlen, unsigned int veclen, int *toobig);

/* blech, iterator equivalent of old interface _realloc, for convenience */
void *iobtree_iter_realloc(struct iobtree_iter *iter, unsigned int newsize, 
  int *toobig);

/* utility functions */

/* return the number of entries in the btree */
unsigned int iobtree_size(struct iobtree *iobtree);

/* return the number of bytes of overhead (used space not holding data) in the
 * btree */
unsigned long int iobtree_overhead(struct iobtree *iobtree);

/* return the total amount of space occupied by the btree in bytes */
unsigned long int iobtree_space(struct iobtree *iobtree);

/* return the number of bytes of utilised space in the btree (holding keys or
 * entries) */
unsigned long int iobtree_utilised(struct iobtree *iobtree);

/* writes the root file number and offset into params */
void iobtree_root(struct iobtree *iobtree, unsigned int *fileno, 
  unsigned long int *offset);

/* return the number of pages in the btree.  Split into leaves and nodes are
 * written into leaves and nodes if not NULL. */
unsigned int iobtree_pages(struct iobtree *iobtree, unsigned int *leaves, 
  unsigned int *nodes);

/* return the size of the pages in the btree */
unsigned int iobtree_pagesize(struct iobtree *iobtree);

/* returns the current number of levels in the btree */
unsigned int iobtree_levels(struct iobtree *iobtree);

#ifdef __cplusplus
}
#endif

#endif

