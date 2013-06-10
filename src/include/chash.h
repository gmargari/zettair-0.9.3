/* chash.h declares interfaces for a generic chained hashtable, that
 * features pipeling, (storing of hash values) dynamic expansion and 
 * move-to-front searching.  The hashtable has been designed to be as 
 * extensible and fast as possible, at the expense of neatness.  It
 * does not handle duplicates, so don't insert them.
 *
 * written nml 2003-05-29
 *
 */

#ifndef CHASH_H
#define CHASH_H

#ifdef __cplusplus
extern "C" {
#endif

enum chash_ret {
    CHASH_OK = 0,              /* operation succeeded */
    CHASH_ITER_FINISH = 1,     /* no more items to iterate over */

    CHASH_ENOMEM = -1,         /* operation failed due to lack of memory */
    CHASH_ENOENT = -2          /* operation failed as the specified item 
                                * wasn't found */
};

struct chash;
struct chash_iter;

/* construct a new table that contains pointer keys in the space given.  
 * (2 ** startbits) is the initial size of the table, and it will double 
 * whenever resize_load is exceeded. hash is the hash function and cmp is the 
 * comparison function. */
struct chash *chash_ptr_new(unsigned int startbits, float resize_load, 
  unsigned int (*hash)(const void *key), 
  int (*cmp)(const void *key1, const void *key2));

/* construct a new table that contains long integer keys in the space given.  
 * (2 ** startbits) is the initial size of the table, and it will double 
 * whenever resize_load is exceeded. the value of the key mod the tablesize 
 * will be taken as the hash value */
struct chash *chash_luint_new(unsigned int startbits, float resize_load);

/* construct a new table that contains string keys in the space given.
 * Strings are copied into the table upon insertion (no need to keep strings 
 * lying around).  (2 ** startbits) is the initial size of the table, and it 
 * will double whenever resize_load is exceeded.  hash is the string hash 
 * function */
struct chash *chash_str_new(unsigned int startbits, float resize_load, 
  unsigned int (*hash)(const char *key, unsigned int len));

/* delete a table */
void chash_delete(struct chash *hash);

/* remove all elements from the table */
void chash_clear(struct chash *hash);

/* create a new iterator over the hashtable */
struct chash_iter *chash_iter_new(struct chash *hash);

void chash_iter_delete(struct chash_iter *iter);

/* get the number of elements in the table */
unsigned int chash_size(const struct chash *hash);

/* ensure that at least enough space is available to allocate reserve objects
 * without failure from lack of memory.  Value returned is the number of
 * reserved items, which should be higher than reserve if the call 
 * succeeded. */
unsigned int chash_reserve(struct chash *hash, unsigned int reserve);

/* documentation for template functions:
 *
 * chash_[keytype]_[datatype]_insert:
 *   accepts a key of keytype and data of datatype and inserts them into the
 *   hashtable.  Don't insert duplicates.  Returns CHASH_OK on success.
 * 
 * chash_[keytype]_[datatype]_remove:
 *   removes first instance found matching key, writing the data associated
 *   with key into *data on success.  Returns CHASH_OK on success.
 *
 * chash_[keytype]_[datatype]_find:
 *   find first instance matching given key, writing a pointer to the data
 *   associated with the key into *data (so you can change the data in-situ -
 *   although you should beware cases where this changes the key!) on success
 *
 * chash_[keytype]_[datatype]_find_insert:
 *   find entry associated with key, or insert a new entry using ins_data if it
 *   can't be found.  On all successful cases, *fnd_data is written with a
 *   pointer to the found/inserted data.  On success, *find will be 1 if found,
 *   0 if inserted.  Returns CHASH_OK on success.
 *
 * chash_[keytype]_[datatype]_foreach:
 *   execute a given fn on each element of the hashtable.  Returns CHASH_OK on
 *   success, currently can't fail.
 *
 * chash_iter_[keytype]_[datatype]_next:
 *   obtain the next value from an iterator, writing the key value into *key
 *   and the data value into *data on success.  Returns CHASH_OK on success and
 *   CHASH_ITER_FINISH once iteration is finished.
 *
 */

/* ptr to ptr functions */

enum chash_ret chash_ptr_ptr_insert(struct chash *hash, const void *key, 
  void *data);
enum chash_ret chash_ptr_ptr_remove(struct chash *hash, const void *key, 
  void **data);
enum chash_ret chash_ptr_ptr_find(struct chash *hash, const void *key, 
  void ***data);
enum chash_ret chash_ptr_ptr_find_insert(struct chash *hash, const void *key, 
  void ***fnd_data, void *ins_data, int *find);
enum chash_ret chash_ptr_ptr_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, const void *key, void **data));
enum chash_ret chash_iter_ptr_ptr_next(struct chash_iter *iter, 
  const void **key, void ***data);

/* ptr to luint functions */

enum chash_ret chash_ptr_luint_insert(struct chash *hash, const void *key, 
  unsigned long int data);
enum chash_ret chash_ptr_luint_remove(struct chash *hash, const void *key, 
  unsigned long int *data);
enum chash_ret chash_ptr_luint_find(struct chash *hash, const void *key, 
  unsigned long int **data);
enum chash_ret chash_ptr_luint_find_insert(struct chash *hash, 
  const void *key, unsigned long int **fnd_data, unsigned long int ins_data, 
  int *find);
enum chash_ret chash_ptr_luint_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, const void *key, unsigned long int *data));
enum chash_ret chash_iter_ptr_luint_next(struct chash_iter *iter, 
  const void **key, unsigned long int **data);

/* uint to ptr functions */

enum chash_ret chash_luint_ptr_insert(struct chash *hash, 
  unsigned long int key, void *data);
enum chash_ret chash_luint_ptr_remove(struct chash *hash, 
  unsigned long int key, void **data);
enum chash_ret chash_luint_ptr_find(struct chash *hash, unsigned long int key, 
  void ***data);
enum chash_ret chash_luint_ptr_find_insert(struct chash *hash, 
  unsigned long int key, void ***fnd_data, void *ins_data, int *find);
enum chash_ret chash_luint_ptr_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, unsigned long int key, void **data));
enum chash_ret chash_iter_luint_ptr_next(struct chash_iter *iter, 
  unsigned long int *key, void ***data);

/* uint to uint functions */

enum chash_ret chash_luint_luint_insert(struct chash *hash, 
  unsigned long int key, unsigned long int data);
enum chash_ret chash_luint_luint_remove(struct chash *hash, 
  unsigned long int key, unsigned long int *data);
enum chash_ret chash_luint_luint_find(struct chash *hash, unsigned long int key,
  unsigned long int **data);
enum chash_ret chash_luint_luint_find_insert(struct chash *hash, 
  unsigned long int key, unsigned long int **fnd_data, 
  unsigned long int ins_data, int *find);
enum chash_ret chash_luint_luint_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, unsigned long int key, unsigned long int *data));
enum chash_ret chash_iter_luint_luint_next(struct chash_iter *iter, 
  unsigned long int *key, unsigned long int **data);

/* uint to dbl functions */

enum chash_ret chash_luint_dbl_insert(struct chash *hash, 
  unsigned long int key, double data);
enum chash_ret chash_luint_dbl_remove(struct chash *hash, 
  unsigned long int key, double *data);
enum chash_ret chash_luint_dbl_find(struct chash *hash, unsigned long int key, 
  double **data);
enum chash_ret chash_luint_dbl_find_insert(struct chash *hash, 
  unsigned long int key, double **fnd_data, double ins_data, int *find);
enum chash_ret chash_luint_dbl_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, unsigned long int key, double *data));
enum chash_ret chash_iter_luint_dbl_next(struct chash_iter *iter, 
  unsigned long int *key, double **data);

/* uint to flt functions */

enum chash_ret chash_luint_flt_insert(struct chash *hash, 
  unsigned long int key, float data);
enum chash_ret chash_luint_flt_remove(struct chash *hash, 
  unsigned long int key, float *data);
enum chash_ret chash_luint_flt_find(struct chash *hash, unsigned long int key, 
  float **data);
enum chash_ret chash_luint_flt_find_insert(struct chash *hash, 
  unsigned long int key, float **fnd_data, float ins_data, int *find);
enum chash_ret chash_luint_flt_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, unsigned long int key, float *data));
enum chash_ret chash_iter_luint_flt_next(struct chash_iter *iter, 
  unsigned long int *key, float **data);

/* str to ptr functions */

enum chash_ret chash_str_ptr_insert(struct chash *hash, 
  const char *key, void *data);
enum chash_ret chash_str_ptr_remove(struct chash *hash, 
  const char *key, void **data);
enum chash_ret chash_str_ptr_find(struct chash *hash, const char *key, 
  void ***data);
enum chash_ret chash_str_ptr_find_insert(struct chash *hash, 
  const char *key, void ***fnd_data, void *ins_data, int *find);
enum chash_ret chash_nstr_ptr_insert(struct chash *hash, 
  const char *key, unsigned int keylen, void *data);
enum chash_ret chash_nstr_ptr_remove(struct chash *hash, 
  const char *key, unsigned int keylen, void **data);
enum chash_ret chash_nstr_ptr_find(struct chash *hash, const char *key, 
  unsigned int keylen, void ***data);
enum chash_ret chash_nstr_ptr_find_insert(struct chash *hash, 
  const char *key, unsigned int keylen, void ***fnd_data, void *ins_data, 
  int *find);
enum chash_ret chash_nstr_ptr_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, const char *key, unsigned int keylen, 
      void **data));
enum chash_ret chash_iter_nstr_ptr_next(struct chash_iter *iter, 
  const char **key, unsigned int *keylen, void ***data);

#ifdef __cplusplus
}
#endif

#endif

