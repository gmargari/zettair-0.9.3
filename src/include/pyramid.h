/* pyramid.h declares an object which progressively merges a series of
 * postings files together, until it performs a final merge.  This object used
 * to be called merger.[ch], but i decided that pyramid described it better
 * (implements a merging pyramid, whereas other code does the actual merging)
 *
 * based upon code originally by Hugh Williams (merge.[hc])
 *
 * written nml 2003-03-3
 *
 */

#ifndef PYRAMID_H
#define PYRAMID_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PYRAMID_OK = 0
};

struct pyramid;
struct fdset;
struct storagep;
struct freemap;

/* create a new merger pyramid, which performs a pyramid merge with width
 * maxfiles. Files are accessed through the
 * fdset, with fdset type number tmp_type for temporary files and type number
 * final_type for final index files. */
struct pyramid *pyramid_new(struct fdset *fds, unsigned int tmp_type,
  unsigned int final_type, unsigned int vocab_type, unsigned int maxfiles, 
  struct storagep *storagep, struct freemap *map, struct freemap *vocab_map);

/* delete the pyramid object, deleting any temporary files created */
void pyramid_delete(struct pyramid *pyramid);

/* perform final merge, returning the number of files created, distinct and
 * total terms merged and the btree root page file number and offset in *files,
 * *dterms, *terms_high, *terms_low, *root_fileno and *root_offset respectively 
 * on success (returns PYRAMID_OK on success, -errno on failure)  After calling 
 * this method all further add_file calls will fail. */
int pyramid_merge(struct pyramid *pyramid, unsigned int *files, 
  unsigned int *vocabs, unsigned long int *dterms, unsigned int *terms_high, 
  unsigned int *terms_low, unsigned int *root_fileno, 
  unsigned long int *root_offset, void *buf, unsigned int bufsize);

/* get an fd for the next file in the pyramid.  Returns -errno on failure. */
int pyramid_pin_next(struct pyramid *pyramid);

/* return an fd for the next file in the pyramid.  Returns -errno on failure */
int pyramid_unpin_next(struct pyramid *pyramid, int fd);

/* add another file to the merging process.  It is assumed that files
 * are added in order of creation.  If a merge occurs it will use buffer buf of
 * size bufsize to perform the merge.  Returns PYRAMID_OK on success and -errno
 * on failure.  allow_merge specifies whether a merge is allowed to occur 
 * (so you can prevent partial merges for the final file added and so on). */
int pyramid_add_file(struct pyramid *pyramid, int allow_merge, void *buf, 
  unsigned int bufsize);

#ifdef __cplusplus
}
#endif

#endif

