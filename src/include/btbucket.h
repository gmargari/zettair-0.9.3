/* btbucket.h declares a set of utility functions for manipulating
 * blocks of raw memory that will serve as btree buckets.  The
 * bucket.h interface allows storage of (reasonably) arbitrary keys
 * and values within raw memory, and this interface wraps that
 * functionality with some accounting details that are necessary for a
 * btree (like sibling pointers for leaf buckets only and common prefixes for 
 * all buckets), while still containing an ordinary bucket.
 *
 * written nml 2003-02-17
 *
 */

#ifndef BTBUCKET_H
#define BTBUCKET_H

#ifdef __cplusplus
extern "C" {
#endif

/* initialise a new btree bucket, of size bucketsize, using bucketing
 * strategy strategy, and with a sibling pointers of sib_fileno,
 * sib_offset.  Terminating sibling pointers are set to point to the bucket 
 * itself. leaf indicates whether this bucket is a leaf bucket.  prefix and
 * prefix_size give the prefix for this bucket.  prefix_size is written with the
 * size of the prefix accepted by the bucket on return. */
void btbucket_new(void *btbucket, unsigned int bucketsize, 
  unsigned int sib_fileno, unsigned int sib_offset, int leaf, void *prefix, 
  unsigned int *prefix_size);

/* get the sibling pointer for this bucket (assuming that its a leaf node) */
void btbucket_sibling(void *btbucket, unsigned int bucketsize, 
  unsigned int *fileno, unsigned long int *offset);

/* set the sibling pointer for this bucket (assuming that its a leaf node) */
void btbucket_set_sibling(void *btbucket, unsigned int bucketsize, 
  unsigned int fileno, unsigned long int offset);

/* get the size of the bucket within this btree bucket */
unsigned int btbucket_size(void *btbucket, unsigned int bucketsize);

/* return a pointer to the start of the bucket within the btree bucket */
void *btbucket_bucket(void *btbucket);

/* return indication of whether the btree bucket is a leaf node */
int btbucket_leaf(void *btbucket, unsigned int bucketsize);

/* return the prefix and prefix size of the prefix for this btree bucket */
void *btbucket_prefix(void *btbucket, unsigned int bucketsize, 
  unsigned int *prefix_size);

/* set the prefix for this btree bucket to prefix, which is of length 
 * *prefix_size.  Note that the bucket may not be able to accomodate all of
 * *prefix_size, in which case it will be shortened and *prefix_size set
 * appropriately.  The bucket inside the btbucket has to be resized to
 * accomodate the change, so its possible for this method to fail, (if the
 * bucket is too full to be resized downward) in which case it returns 0. */
int btbucket_set_prefix(void *btbucket, unsigned int bucketsize, 
  void *prefix, unsigned int *prefix_size);

/* size of a btbucket internal node entry */
unsigned int btbucket_entry_size();

/* decodes a btbucket internal node entry (entry must be of size
 * btbucket_entry_size) */
void btbucket_entry(void *entry, unsigned int *fileno, 
  unsigned long int *offset);

/* encodes a btbucket internal node entry (entry must be of size
 * btbucket_entry_size) */
void btbucket_set_entry(void *entry, unsigned int fileno, 
  unsigned long int offset);

/* return the number of entries that should be split to the lexically smaller 
 * bucket (btbucket, bucketsize, strategy), given 
 * that we're inserting/reallocating an entry (term, termlen, additional) in
 * the bucket at the same time.  Additional should be the length of the vector
 * if the split is in response to an insert, otherwise it should be the
 * additional space requested by a reallocation operation.  The shortest entry 
 * within range bytes of the perfect split is returned, or the nearest entry 
 * if this isn't possible.  Note that the additional term *must* be
 * considered to obtain a workable split in all cases.  the value of left
 * after execution indicates whether additional term should be placed in left
 * or right bucket.  This routine is exposed for testing and because it may be
 * useful for other btrees based on the same bucketing schemes. */
unsigned int btbucket_find_split_entry(void *btbucket, unsigned int bucketsize,
  int strategy, unsigned int range, const char *term, 
  unsigned int termlen, unsigned int additional, int *left);

/* determine what the common prefix for strings in the range [one, two) is, by
 * returning its length and last character (written into lastchar).  Note that
 * the prefix has to be the prefix of both the strings, except for the last
 * character, so you can infer what the prefix is by its length and last
 * character.  one is presumed to be lexographically smaller than two by this
 * function. */
unsigned int btbucket_common_prefix(const char *one, 
  unsigned int onelen, const char *two, unsigned int twolen, 
  char *lastchar);

#ifdef __cplusplus
}
#endif

#endif

