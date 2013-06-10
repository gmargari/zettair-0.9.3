/* bucket.h declares an interface for a operations that store variable length
 * lists with variable length keys into buckets
 *
 * written nml 2003-10-06
 *
 */

#ifndef BUCKET_H
#define BUCKET_H

#ifdef __cplusplus
extern "C" {
#endif

struct bucket;

int bucket_new(void *bucketmem, unsigned int bucketsize, int strategy);

/* allocate space for term term within bucket b.  index is the integer index
 * within the bucket that was assigned, and may be NULL */
void *bucket_alloc(void *bucketmem, unsigned int bucketsize, int strategy,
  const char *term, unsigned int termlen, unsigned int newsize, 
  int *toobig, unsigned int *index);

/* return the in-memory address that an inverted list identified by
 * bucketno,termno occupies in memory.  NULL is returned on failure.  index is
 * the integer index of the term within the bucket and may be NULL. */
void *bucket_find(void *bucketmem, unsigned int bucketsize,
  int strategy, const char *term, unsigned int termlen, 
  unsigned int *veclen, unsigned int *index);

/* search is just like find, except that it returns the lexographically largest
 * term that is less than the search target, instead of an exact match. index
 * is the integer index of the term within the bucket and may be NULL. */
void *bucket_search(void *bucketmem, unsigned int bucketsize, int strategy, 
  const char *term, unsigned int termlen, unsigned int *veclen, 
  unsigned int *index);

/* remove the specified term from the bucket */
int bucket_remove(void *bucketmem, unsigned int bucketsize, int strategy,
  const char *term, unsigned int termlen);
int bucket_remove_at(void *bucketmem, unsigned int bucketsize, int strategy,
  unsigned int index);

/* grow (or shrink) the list associated with the term to newlen.  On
 * success, the address of the list in-memory is returned.  NULL is returned on
 * failure, which indicates that there isn't enough room in the bucket for the
 * additional length.  On failure, if toobig is set, it indicates that the term
 * and vector are too long to fit into an empty bucket. */
void *bucket_realloc(void *bucketmem, unsigned int bucketsize, 
  int strategy, const char *term, unsigned int termlen, 
  unsigned int newsize, int *toobig);
void *bucket_realloc_at(void *bucketmem, unsigned int bucketsize, 
  int strategy, unsigned int index, unsigned int newsize, int *toobig);

/* retrieve the next term in the bucket.  Iterate over the terms in the bucket
 * by setting *state to 0, then calling bucket_next_term until it 
 * returns NULL */
/* XXX: note that this call will have to be changed if buckets stop storing the
 * term contiguously */
const char *bucket_next_term(void *bucketmem, unsigned int bucketsize, 
  int strategy, unsigned int *state, unsigned int *termlen, 
  void **data, unsigned int *len);
const char *bucket_term_at(void *bucketmem, unsigned int bucketsize,
  int strategy, unsigned int index, unsigned int *termlen, void **data, 
  unsigned int *len);

/* split the entries in bucket 1 lexographically so that the terms
 * lexographically least number of terms are left in bucket 1.  Bucket 2 gets
 * the remainder, and is presumed to be uninitialised.  Return value indicates 
 * success or failure. */
int bucket_split(void *bucketmem1, unsigned int bucketsize1, 
  void *bucketmem2, unsigned int bucketsize2, int strategy, 
  unsigned int terms);

/* resize the working area of the bucket.  Return value indicates success or
 * failure. */
int bucket_resize(void *bucketmem, unsigned int old_bucketsize, int strategy, 
  unsigned int new_bucketsize);

/* return number of entries in bucket */
unsigned int bucket_entries(void *bucketmem, unsigned int bucketsize, 
  int strategy);

/* return number of bytes utilised in bucket */
unsigned int bucket_utilised(void *bucketmem, unsigned int bucketsize, 
  int strategy);

/* return number of bytes of string in bucket */
unsigned int bucket_string(void *bucketmem, unsigned int bucketsize, 
  int strategy);

/* return number of bytes of overhead in bucket */
unsigned int bucket_overhead(void *bucketmem, unsigned int bucketsize, 
  int strategy);

/* return number of free bytes in bucket */
unsigned int bucket_unused(void *bucketmem, unsigned int bucketsize, 
  int strategy);

/* whether this bucket scheme sorts its entries */
int bucket_sorted(int strategy);

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
 * or right bucket. */
unsigned int bucket_find_split_entry(void *bucketmem, unsigned int bucketsize,
  int strategy, unsigned int range, const char *term, 
  unsigned int termlen, unsigned int additional, int *smaller);

/* append a term to a bucket, providing the quickest way to get a term into a
 * bucket.  No attempt is made to sort the bucket, so you'll have to append them
 * in sorted order if the bucket format requires it */
void *bucket_append(void *bucketmem, unsigned int bucketsize, int strategy,
  const char *term, unsigned int termlen, unsigned int size, 
  int *toobig);

/* XXX: things we may need in the future: */

/* merge two lexographically sorted buckets (where all entries in bucket1 are
 * less than all entries in bucket2) into one bucket (bucket1) */
int bucket_merge(void *bucketmem1, unsigned int bucketsize1, void *bucketmem2, 
  unsigned int bucketsize2, int strategy);

/* set the term of entry termno to newterm, without attempting to maintain
 * sorting order.  This is very much an 'i know what i'm doing call', its up to
 * you to ensure that the entry number is correct and that the ordering (if the
 * strategy is ordered) is not affected by this call.  Return value indicates
 * success. */
/* FIXME: relationship b/w newsize and newtermlen indicates whether to
 * completely change term, to remove chars from front of term or add chars to
 * front of term. */
/* XXX: maybe something that does this to all entries at once? */
int bucket_set_term(void *bucketmem, unsigned int bucketsize, int strategy, 
  unsigned int termno, unsigned int newsize, const char *newterm, 
  unsigned int newtermlen, int *toobig);

#ifdef __cplusplus
}
#endif

#endif

