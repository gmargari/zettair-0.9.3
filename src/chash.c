/* chash.c implements the interface declared in chash.h.  The implementation
 * caters for different types by using a primitive sort of templating using
 * macros.  Nasty, but it gets the job done.  One performance improvement that 
 * i've made is to specialise the macro's for strings.  A prefix consisting of
 * the first word full of characters is formed, which allows fast comparison.
 * Since we only need inequality, not relative positional (< or >) information
 * for a hashtable, the characters are stored in the native endianness of the
 * platform.  This allows us to return pointers directly to the prefix as a
 * string and it will work for the first four characters.  We also allocate
 * another word for either a suffix or pointer into a strings array 
 * immediately following, so the combination can be used to store strings 
 * of size <= 2 words directly in each node.  Longer strings we store (entirely,
 * even though the prefix contains some of it, so we can return pointers to the
 * whole string) in a special strings area.  The strings area is simply an 
 * array which we keep a tail pointer into, and an indication whether it is 
 * packed.  We allocate strings off the end of the area, and remove them by 
 * deleting the reference and indicating that the area is no longer packed.  
 * If further space is needed we repack the area, by iterating over the 
 * hashtable, copying strings into packed format again, before expanding.
 *
 * written nml 2003-05-29
 *
 */

#include "firstinclude.h"

#include "chash.h"
#include "_chash.h"

#include "def.h"
#include "bit.h"
#include "str.h"
#include "objalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <limits.h>

/* amount requested from malloc for links */
#define BULK_ALLOC 1024

/* minimum number of bytes that we'll normally (we make exceptions if we run
 * out of memory) repack to reclaim */
#define PACK_HYSTERESIS (BULK_ALLOC / 3)

struct chash_iter {
    struct chash *hash;            /* hashtable we're iterating over */
    struct chash_link *curr;       /* current link */
    struct chash_link first;       /* (dummy) first link, for convenience */
    unsigned int currpos;          /* current position in hashtable */
};

/* internal function to grow and rehash the hashtable */
static int chash_expand(struct chash *hash) {
    unsigned int size,
                 newsize,
                 i;
    void *ptr;
    struct chash_link *link,
                      *list,
                      **linkptr;

    if (hash->bits < (BIT_FROM_BYTE(sizeof(unsigned int)))) {
        size = BIT_POW2(hash->bits);
        newsize = BIT_POW2(hash->bits + 1);
        if ((ptr 
          = realloc(hash->table, sizeof(struct chash_link*) * newsize))) {
            hash->table = ptr;
            BIT_ARRAY_NULL(&hash->table[size], size);

            /* now we need to rehash all of the items in the hashtable */
            for (i = 0; i < size; i++) {
                linkptr = &hash->table[i];
                while ((link = *linkptr)) {
                    /* note: we need to move it if the bit we just
                     * started considering is on */
                    if (BIT_GET(link->hash, hash->bits)) {
                        /* it belongs in the new slot, move it */
                        *linkptr = link->next;
                        link->next = hash->table[size + i];
                        hash->table[size + i] = link;
                    } else {
                        /* it belongs in the old slot, leave it */
                        linkptr = &link->next;
                    }
                }
            }

            /* reverse all of the new lists, since they're in exactly
             * the wrong order (ordered by move-to-front) */
            for (/* note: i == size */; i < newsize; i++) {
                if ((list = hash->table[i]) && (list->next)) {
                    /* reverse the list */
                    hash->table[i] = NULL;
                    do {
                        link = list->next;
                        list->next = hash->table[i];
                        hash->table[i] = list;
                        list = link;
                    } while (list);
                }
            }

            hash->bits++;
            hash->resize_point 
              = (unsigned int) (BIT_POW2(hash->bits) * hash->resize_load);

            /* watch for overflow */
            if ((hash->resize_load > 1.0) 
              && (hash->resize_point < BIT_POW2(hash->bits))) {
                hash->resize_point = UINT_MAX;
            }
            return 1;
        }
    }

    return 0;
}

/* internal function to repack the strings area when it becomes fragmented */
static enum chash_ret chash_strings_repack(struct chash *hash) {
    char *copy = malloc(hash->strings.used);
    unsigned int i,
                 size = BIT_POW2(hash->bits);
    struct chash_link *link;

    assert(hash->strings.unpacked);

    if (copy) {
        memcpy(copy, hash->strings.strings, hash->strings.used);
        hash->strings.used = 0;

        /* iterate through the hash table, copying strings into strings area in
         * packed, ordered fashion */

        for (i = 0; i < size; i++) {
            link = hash->table[i];
            while (link) {
                memcpy(&hash->strings.strings[hash->strings.used],
                  &copy[link->key.k_str.ptr], link->key.k_str.len);
                link->key.k_str.ptr = hash->strings.used;
                hash->strings.used += link->key.k_str.len;
                link = link->next;
            }
        }

        free(copy);
        hash->strings.unpacked = 0;
        return CHASH_OK;
    } else {
        return CHASH_ENOMEM;
    }
}

/* internal function to expand the strings area when necessary */
static enum chash_ret chash_strings_expand(struct chash *hash) {
    void *ptr = realloc(hash->strings.strings, 2 * hash->strings.size);

    if (ptr) {
        hash->strings.strings = ptr;
        hash->strings.size *= 2;
        return CHASH_OK;
    } else {
        return CHASH_ENOMEM;
    }
}

/* macro to perform all common initialisation of the hashtable */
#define CHASH_INIT(startbits, resize_load, hash, space, keytype, cmp, str)    \
    do {                                                                      \
        if ((space = malloc(sizeof(*space)))                                  \
          && (space->table = NULL, space->strings.strings = NULL,             \
              space->strings.size = space->strings.used = 0,                  \
              space->strings.unpacked = 0, 1)                                 \
          && (space->table                                                    \
            = malloc(sizeof(struct chash_link*) * BIT_POW2(startbits)))       \
          && (space->alloc = objalloc_new(sizeof(struct chash_link), 0,       \
              DEAR_DEBUG, BULK_ALLOC, NULL))                                  \
          /* initialise strings area */                                       \
          && (!str || ((space->strings.size = BULK_ALLOC),                    \
              (space->strings.strings = malloc(BULK_ALLOC))))) {              \
            space->bits = startbits;                                          \
            space->resize_load = resize_load;                                 \
            space->cmpfn = cmp;                                               \
            space->data_type = CHASH_TYPE_UNKNOWN;                            \
            space->hashfn.h_##keytype = hash;                                 \
            space->elements = 0;                                              \
            space->resize_point                                               \
              = (unsigned int) (BIT_POW2(startbits) * resize_load);           \
            BIT_ARRAY_NULL(space->table, BIT_POW2(startbits));                \
        } else {                                                              \
            if (space) {                                                      \
                if (space->table) {                                           \
                    free(space->table);                                       \
                }                                                             \
                free(space);                                                  \
            }                                                                 \
            return NULL;                                                      \
        }                                                                     \
    } while (0);

struct chash *chash_ptr_new(unsigned int startbits, float resize_load, 
  unsigned int (*hash)(const void *key), 
  int (*cmp)(const void *key1, const void *key2)) {
    struct chash *table;

    CHASH_INIT(startbits, resize_load, hash, table, ptr, cmp, 0);
    table->key_type = CHASH_TYPE_PTR;
    return table;
}

struct chash *chash_luint_new(unsigned int startbits, float resize_load) {
    struct chash *table;
    CHASH_INIT(startbits, resize_load, NULL, table, luint, NULL, 0);
    table->key_type = CHASH_TYPE_LUINT;
    return table;
}

struct chash *chash_str_new(unsigned int startbits, float resize_load, 
  unsigned int (*hash)(const char *key, unsigned int len)) {
    struct chash *table;
    CHASH_INIT(startbits, resize_load, hash, table, str, NULL, 1);
    table->key_type = CHASH_TYPE_STR;
    return table;
}

void chash_delete(struct chash *hash) {
    objalloc_clear(hash->alloc);
    objalloc_delete(hash->alloc);
    if (hash->strings.strings) {
        free(hash->strings.strings);
    }
    free(hash->table);
    hash->table = NULL;     /* makes use after deletion easy to catch */
    free(hash);
    return;
}

unsigned int chash_size(const struct chash *hash) {
    return CHASH_SIZE(hash);
}

unsigned int chash_reserve(struct chash *hash, unsigned int reserve) {
    return objalloc_reserve(hash->alloc, reserve);
}

struct chash_iter *chash_iter_new(struct chash *hash) {
    struct chash_iter *iter;

    if (sizeof(*iter) <= sizeof(struct chash_link)) {
        if (!(iter = objalloc_malloc(hash->alloc, sizeof(*iter)))) {
            return NULL;
        }
    } else if (!(iter = malloc(sizeof(*iter)))) {
        return NULL;
    }

    iter->hash = hash;
    iter->curr = &iter->first;
    iter->first.next = NULL;
    iter->currpos = 0;

    return iter;
}

void chash_iter_delete(struct chash_iter *iter) {
    if (sizeof(*iter) <= sizeof(struct chash_link)) {
        objalloc_free(iter->hash->alloc, iter);
    } else {
        free(iter);
    }
}

void chash_clear(struct chash *hash) {
    objalloc_clear(hash->alloc);
    BIT_ARRAY_NULL(hash->table, BIT_POW2(hash->bits));
    hash->elements = 0;

    return;
}

/* insert functions (which are almost exactly the same) */

#define CHASH_INSERT(hash, ky, keytype, dt, datatype, dtptr)                  \
    do {                                                                      \
        struct chash_link *newlink;                                           \
                                                                              \
        /* note: no search for duplicates */                                  \
                                                                              \
        /* obtain a new link, and insert it at the front of the chain */      \
        if ((newlink = objalloc_malloc(hash->alloc, sizeof(*newlink)))) {     \
            /* insert new link */                                             \
            newlink->hash = hashval;                                          \
            newlink->key.k_##keytype = ky;                                    \
            newlink->data.d_##datatype = dt;                                  \
            *dtptr = &newlink->data.d_##datatype;                             \
            newlink->next = hash->table[modhash];                             \
            hash->table[modhash] = newlink;                                   \
            hash->elements++;                                                 \
                                                                              \
            /* check if we need to expand the table */                        \
            if (hash->elements > hash->resize_point) {                        \
                if (!chash_expand(hash)) {                                    \
                    /* failed: up the resize point so we don't expand soon */ \
                    hash->resize_point <<= 1;                                 \
                }                                                             \
            }                                                                 \
                                                                              \
            return CHASH_OK;                                                  \
        } else {                                                              \
            return CHASH_ENOMEM;                                              \
        }                                                                     \
    } while (0)

enum chash_ret chash_ptr_ptr_insert(struct chash *hash, const void *ckey, 
  void *data) {
    void *key = (void *) ckey;   /* get around const issue */
    void **dummy;
    unsigned int hashval = hash->hashfn.h_ptr(key),
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_PTR);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_INSERT(hash, key, ptr, data, ptr, &dummy);
}

enum chash_ret chash_ptr_luint_insert(struct chash *hash, const void *ckey, 
  unsigned long int data) {
    void *key = (void *) ckey;   /* get around const issue */
    unsigned long int *dummy;
    unsigned int hashval = hash->hashfn.h_ptr(key),
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_PTR);
    assert((hash->data_type == CHASH_TYPE_LUINT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_LUINT;

    CHASH_INSERT(hash, key, ptr, data, luint, &dummy);
}

enum chash_ret chash_luint_ptr_insert(struct chash *hash, 
  unsigned long int key, void *data) {
    void **dummy;
    unsigned int hashval = key,   /* integral types are their own keys */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_INSERT(hash, key, luint, data, ptr, &dummy);
}

enum chash_ret chash_luint_luint_insert(struct chash *hash, 
  unsigned long int key, unsigned long int data) {
    unsigned long int *dummy;
    unsigned int hashval = key,   /* integral types are their own keys */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_LUINT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_LUINT;

    CHASH_INSERT(hash, key, luint, data, luint, &dummy);
}

enum chash_ret chash_luint_dbl_insert(struct chash *hash, 
  unsigned long int key, double data) {
    double *dummy;
    unsigned int hashval = key,   /* integral types are their own keys */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_DBL) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_DBL;

    CHASH_INSERT(hash, key, luint, data, dbl, &dummy);
}

enum chash_ret chash_luint_flt_insert(struct chash *hash, 
  unsigned long int key, float data) {
    float *dummy;
    unsigned int hashval = key,   /* integral types are their own keys */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_FLT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_FLT;

    CHASH_INSERT(hash, key, luint, data, flt, &dummy);
}

/* specialisation for string type */

#define CHASH_STR_INSERT(hash, ky, kylen, dt, datatype, dtptr)                \
    do {                                                                      \
        struct chash_link *newlink;                                           \
                                                                              \
        /* note: no search for duplicates */                                  \
                                                                              \
        /* obtain a new link, and insert it at the front of the chain */      \
        if ((newlink = objalloc_malloc(hash->alloc, sizeof(*newlink)))) {     \
            /* insert new link */                                             \
            newlink->hash = hashval;                                          \
            newlink->key.k_str.len = kylen;                                   \
                                                                              \
            /* ensure that there's enough space in the strings area */        \
            while (hash->strings.size - hash->strings.used < kylen) {         \
                if ((hash->strings.unpacked >= PACK_HYSTERESIS)               \
                  && (hash->strings.unpacked >= kylen)) {                     \
                    if (chash_strings_repack(hash) != CHASH_OK) {             \
                        /* failed to repack */                                \
                        objalloc_free(hash->alloc, newlink);                  \
                        return CHASH_ENOMEM;                                  \
                    }                                                         \
                } else {                                                      \
                    if (chash_strings_expand(hash) != CHASH_OK) {             \
                        /* failed to expand, see if repacking will let us     \
                         * fit this string in */                              \
                        if ((hash->strings.unpacked < kylen)                  \
                          || (chash_strings_repack(hash) != CHASH_OK)) {      \
                            /* failed to repack, or repacking wouldn't        \
                             * free enough space anyway */                    \
                            objalloc_free(hash->alloc, newlink);              \
                            return CHASH_ENOMEM;                              \
                        }                                                     \
                    }                                                         \
                }                                                             \
            }                                                                 \
                                                                              \
            /* we have enough space to code the string */                     \
            newlink->key.k_str.ptr = hash->strings.used;                      \
            memcpy(&hash->strings.strings[hash->strings.used], ky,            \
              kylen);                                                         \
            hash->strings.used += kylen;                                      \
                                                                              \
            newlink->data.d_##datatype = dt;                                  \
            *dtptr = &newlink->data.d_##datatype;                             \
            newlink->next = hash->table[modhash];                             \
            hash->table[modhash] = newlink;                                   \
            hash->elements++;                                                 \
                                                                              \
            /* check if we need to expand the table */                        \
            if (hash->elements > hash->resize_point) {                        \
                if (!chash_expand(hash)) {                                    \
                    /* failed: up the resize point so we don't expand soon */ \
                    hash->resize_point <<= 1;                                 \
                }                                                             \
            }                                                                 \
                                                                              \
            return CHASH_OK;                                                  \
        } else {                                                              \
            return CHASH_ENOMEM;                                              \
        }                                                                     \
    } while (0)

enum chash_ret chash_nstr_ptr_insert(struct chash *hash, 
  const char *key, unsigned int keylen, void *data) {
    void *dummy;
    unsigned int hashval = hash->hashfn.h_str(key, keylen),
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_STR);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_STR_INSERT(hash, key, keylen, data, ptr, &dummy);
}

/* remove functions (which are almost exactly the same) */

#define CHASH_REMOVE(hash, ky, keytype, keyisptr, dt, datatype)               \
    do {                                                                      \
        struct chash_link **linkptr = &hash->table[modhash],                  \
                          *link = *linkptr;                                   \
                                                                              \
        while (link) {                                                        \
            if ((keyisptr && ((hashval == link->hash)                         \
                && (!hash->cmpfn((void*) ky, link->key.k_ptr))))              \
              /* compare direct if key isn't a pointer type */                \
              || (!keyisptr && (link->key.k_##keytype == ky))) {              \
                /* found it, remove */                                        \
                *linkptr = link->next;                                        \
                *dt = link->data.d_##datatype;                                \
                objalloc_free(hash->alloc, link);                             \
                hash->elements--;                                             \
                return CHASH_OK;                                              \
            }                                                                 \
            linkptr = &link->next;                                            \
            link = *linkptr;                                                  \
        }                                                                     \
    } while (0)

enum chash_ret chash_ptr_ptr_remove(struct chash *hash, const void *key, 
  void **data) {
    unsigned int hashval = hash->hashfn.h_ptr(key),
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_PTR);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_REMOVE(hash, key, ptr, 1, data, ptr);
    /* failed to remove */
    return CHASH_ENOENT;
}

enum chash_ret chash_ptr_luint_remove(struct chash *hash, const void *key, 
  unsigned long int *data) {
    unsigned int hashval = hash->hashfn.h_ptr(key),
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_PTR);
    assert((hash->data_type == CHASH_TYPE_LUINT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_LUINT;

    CHASH_REMOVE(hash, key, ptr, 1, data, luint);
    /* failed to remove */
    return CHASH_ENOENT;
}

enum chash_ret chash_luint_ptr_remove(struct chash *hash, 
  unsigned long int key, void **data) {
    unsigned int hashval = key,  /* integral types are their own hash values */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_REMOVE(hash, key, luint, 0, data, ptr); 
    /* failed to remove */
    return CHASH_ENOENT;
}

enum chash_ret chash_luint_luint_remove(struct chash *hash, 
  unsigned long int key, unsigned long int *data) {
    unsigned int hashval = key,  /* integral types are their own hash values */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_LUINT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_LUINT;

    CHASH_REMOVE(hash, key, luint, 0, data, luint); 
    /* failed to remove */
    return CHASH_ENOENT;
}

enum chash_ret chash_luint_dbl_remove(struct chash *hash, 
  unsigned long int key, double *data) {
    unsigned int hashval = key,  /* integral types are their own hash values */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_DBL) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_DBL;

    CHASH_REMOVE(hash, key, luint, 0, data, dbl); 
    /* failed to remove */
    return CHASH_ENOENT;
}

enum chash_ret chash_luint_flt_remove(struct chash *hash, 
  unsigned long int key, float *data) {
    unsigned int hashval = key,  /* integral types are their own hash values */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_FLT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_FLT;

    CHASH_REMOVE(hash, key, luint, 0, data, flt); 
    /* failed to remove */
    return CHASH_ENOENT;
}

/* specialisation of remove for string type */

#define CHASH_STR_REMOVE(hash, ky, kylen, dt, datatype)                       \
    do {                                                                      \
        struct chash_link **linkptr = &hash->table[modhash],                  \
                          *link = *linkptr;                                   \
                                                                              \
        while (link) {                                                        \
            if ((kylen == link->key.k_str.len)                                \
              && (hashval == link->hash)                                      \
              && (!str_ncmp(ky, &hash->strings.strings[link->key.k_str.ptr],  \
                  kylen))) {                                                  \
                /* found it, remove */                                        \
                                                                              \
                assert(hash->strings.used >= kylen);                          \
                /* string is in strings area */                               \
                if (link->key.k_str.ptr == hash->strings.used - kylen) {      \
                    /* its the last string in the area, don't need to         \
                     * fragment */                                            \
                    hash->strings.used -= kylen;                              \
                } else {                                                      \
                    hash->strings.unpacked += kylen;                          \
                }                                                             \
                *dt = link->data.d_##datatype;                                \
                *linkptr = link->next;                                        \
                objalloc_free(hash->alloc, link);                             \
                hash->elements--;                                             \
                return CHASH_OK;                                              \
            }                                                                 \
            linkptr = &link->next;                                            \
            link = *linkptr;                                                  \
        }                                                                     \
    } while (0)

enum chash_ret chash_nstr_ptr_remove(struct chash *hash, 
  const char *key, unsigned int keylen, void **data) {
    unsigned int hashval = hash->hashfn.h_str(key, keylen),
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_STR);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_STR_REMOVE(hash, key, keylen, data, ptr); 
    /* failed to remove */
    return CHASH_ENOENT;
}

/* find functions (which are almost exactly the same) */

#define CHASH_FIND(hash, ky, keytype, keyisptr, dt, datatype)                 \
    do {                                                                      \
        struct chash_link **linkptr = &hash->table[modhash],                  \
                          *link = *linkptr;                                   \
                                                                              \
        while (link) {                                                        \
            if ((keyisptr && ((hashval == link->hash)                         \
                && (!hash->cmpfn((void*) ky, link->key.k_ptr))))              \
              /* compare direct if key isn't a pointer type */                \
              || (!keyisptr && (link->key.k_##keytype == ky))) {              \
                /* found it, move to front */                                 \
                if (link != hash->table[modhash]) {                           \
                    *linkptr = link->next;                                    \
                    link->next = hash->table[modhash];                        \
                    hash->table[modhash] = link;                              \
                }                                                             \
                *dt = &link->data.d_##datatype;                               \
                return CHASH_OK;                                              \
            }                                                                 \
            linkptr = &link->next;                                            \
            link = *linkptr;                                                  \
        }                                                                     \
    } while (0)

enum chash_ret chash_ptr_ptr_find(struct chash *hash, const void *key, 
  void ***data) {
    unsigned int hashval = hash->hashfn.h_ptr(key),
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_PTR);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_FIND(hash, key, ptr, 1, data, ptr);
    /* failed to find it */
    return CHASH_ENOENT;
}

enum chash_ret chash_ptr_luint_find(struct chash *hash, const void *key, 
  unsigned long int **data) {
    unsigned int hashval = hash->hashfn.h_ptr(key),
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_PTR);
    assert((hash->data_type == CHASH_TYPE_LUINT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_LUINT;

    CHASH_FIND(hash, key, ptr, 1, data, luint);
    /* failed to find it */
    return CHASH_ENOENT;
}

enum chash_ret chash_luint_ptr_find(struct chash *hash, unsigned long int key, 
  void ***data) {
    unsigned int hashval = key,  /* integral types are their own hash values */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_FIND(hash, key, luint, 0, data, ptr);
    /* failed to find it */
    return CHASH_ENOENT;
}

enum chash_ret chash_luint_luint_find(struct chash *hash, unsigned long int key,
  unsigned long int **data) {
    unsigned int hashval = key,  /* integral types are their own hash values */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_LUINT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_LUINT;

    CHASH_FIND(hash, key, luint, 0, data, luint);
    /* failed to find it */
    return CHASH_ENOENT;
}

enum chash_ret chash_luint_dbl_find(struct chash *hash, unsigned long int key, 
  double **data) {
    unsigned int hashval = key,  /* integral types are their own hash values */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_DBL) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_DBL;

    CHASH_FIND(hash, key, luint, 0, data, dbl);
    /* failed to find it */
    return CHASH_ENOENT;
}

enum chash_ret chash_luint_flt_find(struct chash *hash, unsigned long int key, 
  float **data) {
    unsigned int hashval = key,  /* integral types are their own hash values */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_FLT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_FLT;

    CHASH_FIND(hash, key, luint, 0, data, flt);
    /* failed to find it */
    return CHASH_ENOENT;
}

/* specialisation for str type */

#define CHASH_STR_FIND(hash, ky, kylen, dt, datatype)                         \
    do {                                                                      \
        struct chash_link **linkptr = &hash->table[modhash],                  \
                          *link = *linkptr;                                   \
                                                                              \
        while (link) {                                                        \
            if ((kylen == link->key.k_str.len)                                \
              && (hashval == link->hash)                                      \
              && (!str_ncmp(ky, &hash->strings.strings[link->key.k_str.ptr],  \
                  kylen))) {                                                  \
                /* found it, move to front */                                 \
                if (link != hash->table[modhash]) {                           \
                    *linkptr = link->next;                                    \
                    link->next = hash->table[modhash];                        \
                    hash->table[modhash] = link;                              \
                }                                                             \
                *dt = &link->data.d_##datatype;                               \
                return CHASH_OK;                                              \
            }                                                                 \
            linkptr = &link->next;                                            \
            link = *linkptr;                                                  \
        }                                                                     \
    } while (0)

enum chash_ret chash_nstr_ptr_find(struct chash *hash, const char *key, 
  unsigned int keylen, void ***data) {
    unsigned int hashval = hash->hashfn.h_str(key, keylen),
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_STR);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_STR_FIND(hash, key, keylen, data, ptr);
    /* failed to find it */
    return CHASH_ENOENT;
}

/* find_insert function (which are almost exactly the same) */

enum chash_ret chash_ptr_ptr_find_insert(struct chash *hash, const void *key, 
  void ***fnd_data, void *ins_data, int *find) {
    unsigned int hashval = hash->hashfn.h_ptr(key),
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_PTR);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    *find = 1;
    CHASH_FIND(hash, key, ptr, 1, fnd_data, ptr);
    *find = 0;
    CHASH_INSERT(hash, (void *) key, ptr, ins_data, ptr, fnd_data);
}

enum chash_ret chash_luint_dbl_find_insert(struct chash *hash, 
  unsigned long int key, double **fnd_data, double ins_data, int *find) {
    unsigned int hashval = key,  /* integral types are their own hash values */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_DBL) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_DBL;

    *find = 1;
    CHASH_FIND(hash, key, luint, 0, fnd_data, dbl);
    *find = 0;
    CHASH_INSERT(hash, key, luint, ins_data, dbl, fnd_data);
}

enum chash_ret chash_luint_flt_find_insert(struct chash *hash, 
  unsigned long int key, float **fnd_data, float ins_data, int *find) {
    unsigned int hashval = key,  /* integral types are their own hash values */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_FLT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_FLT;

    *find = 1;
    CHASH_FIND(hash, key, luint, 0, fnd_data, flt);
    *find = 0;
    CHASH_INSERT(hash, key, luint, ins_data, flt, fnd_data);
}

enum chash_ret chash_ptr_luint_find_insert(struct chash *hash, 
  const void *key, unsigned long int **fnd_data, unsigned long int ins_data, 
  int *find) {
    unsigned int hashval = hash->hashfn.h_ptr(key),
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_PTR);
    assert((hash->data_type == CHASH_TYPE_LUINT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_LUINT;

    *find = 1;
    CHASH_FIND(hash, key, ptr, 1, fnd_data, luint);
    *find = 0;
    CHASH_INSERT(hash, (void *) key, ptr, ins_data, luint, fnd_data);
}

enum chash_ret chash_luint_ptr_find_insert(struct chash *hash, 
  unsigned long int key, void ***fnd_data, void *ins_data, int *find) {
    unsigned int hashval = key,  /* integral types are their own hash values */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    *find = 1;
    CHASH_FIND(hash, key, luint, 0, fnd_data, ptr);
    *find = 0;
    CHASH_INSERT(hash, key, luint, ins_data, ptr, fnd_data);
}

enum chash_ret chash_luint_luint_find_insert(struct chash *hash, 
  unsigned long int key, unsigned long int **fnd_data, 
  unsigned long int ins_data, int *find) {
    unsigned int hashval = key,  /* integral types are their own hash values */
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_LUINT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_LUINT;

    *find = 1;
    CHASH_FIND(hash, key, luint, 0, fnd_data, luint);
    *find = 0;
    CHASH_INSERT(hash, key, luint, ins_data, luint, fnd_data);
}

enum chash_ret chash_nstr_ptr_find_insert(struct chash *hash, 
  const char *key, unsigned int keylen, void ***fnd_data, void *ins_data, 
  int *find) {
    unsigned int hashval = hash->hashfn.h_str(key, keylen),
                 modhash = BIT_MOD2(hashval, hash->bits);

    assert(hash->key_type == CHASH_TYPE_STR);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    *find = 1;
    CHASH_STR_FIND(hash, key, keylen, fnd_data, ptr);
    *find = 0;
    CHASH_STR_INSERT(hash, key, keylen, ins_data, ptr, fnd_data);
}

/* foreach function (which are almost exactly the same) */

#define CHASH_FOREACH(hash, fn, keytype, datatype, userd)                     \
    do {                                                                      \
        unsigned int i,                                                       \
                     size = BIT_POW2(hash->bits);                             \
        struct chash_link *link;                                              \
                                                                              \
        for (i = 0; i < size; i++) {                                          \
            link = hash->table[i];                                            \
            while (link) {                                                    \
                fn(userd, link->key.k_##keytype, &link->data.d_##datatype);   \
                link = link->next;                                            \
            }                                                                 \
        }                                                                     \
                                                                              \
        return CHASH_OK;                                                      \
    } while (0)

enum chash_ret chash_ptr_ptr_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, const void *key, void **data)) {
    assert(hash->key_type == CHASH_TYPE_PTR);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_FOREACH(hash, fn, ptr, ptr, userdata);
}

enum chash_ret chash_ptr_luint_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, const void *key, unsigned long int *data)) {
    assert(hash->key_type == CHASH_TYPE_PTR);
    assert((hash->data_type == CHASH_TYPE_LUINT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_LUINT;

    CHASH_FOREACH(hash, fn, ptr, luint, userdata);
}

enum chash_ret chash_luint_ptr_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, unsigned long int key, void **data)) {
    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_FOREACH(hash, fn, luint, ptr, userdata);
}

enum chash_ret chash_luint_luint_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, unsigned long int key, unsigned long int *data)) {
    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_LUINT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_LUINT;

    CHASH_FOREACH(hash, fn, luint, luint, userdata);
}

enum chash_ret chash_luint_dbl_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, unsigned long int key, double *data)) {
    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_DBL) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_DBL;

    CHASH_FOREACH(hash, fn, luint, dbl, userdata);
}

enum chash_ret chash_luint_flt_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, unsigned long int key, float *data)) {
    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_FLT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_FLT;

    CHASH_FOREACH(hash, fn, luint, flt, userdata);
}

/* specialisation for str type */

#define CHASH_STR_FOREACH(hash, fn, datatype, userd)                          \
    do {                                                                      \
        unsigned int i,                                                       \
                     size = BIT_POW2(hash->bits);                             \
        struct chash_link *link;                                              \
                                                                              \
        for (i = 0; i < size; i++) {                                          \
            link = hash->table[i];                                            \
                                                                              \
            while (link) {                                                    \
                fn(userd, &hash->strings.strings[link->key.k_str.ptr],        \
                  link->key.k_str.len, &link->data.d_##datatype);             \
                link = link->next;                                            \
            }                                                                 \
        }                                                                     \
                                                                              \
        return CHASH_OK;                                                      \
    } while (0)

enum chash_ret chash_nstr_ptr_foreach(struct chash *hash, void *userdata,
  void (*fn)(void *userdata, const char *key, unsigned int len, void **data)) {
    assert(hash->key_type == CHASH_TYPE_STR);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_STR_FOREACH(hash, fn, ptr, userdata);
}

/* iteration functions */

#define CHASH_ITER_NEXT(iter, key, keytype, data, datatype)                   \
    do {                                                                      \
        unsigned int tablesize = BIT_POW2((iter)->hash->bits);                \
        struct chash_link *curr = (iter)->curr->next;                         \
                                                                              \
        while (!curr) {                                                       \
            if ((iter)->currpos < tablesize) {                                \
                curr = (iter)->hash->table[(iter)->currpos++];                \
            } else {                                                          \
                return CHASH_ITER_FINISH;                                     \
            }                                                                 \
        }                                                                     \
                                                                              \
        (iter)->curr = curr;                                                  \
        *(key) = curr->key.k_##keytype;                                       \
        *(data) = &curr->data.d_##datatype;                                   \
        return CHASH_OK;                                                      \
    } while (0)

enum chash_ret chash_iter_ptr_ptr_next(struct chash_iter *iter, 
  const void **key, void ***data) {
    struct chash *hash = iter->hash;
    assert(hash->key_type == CHASH_TYPE_PTR);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_ITER_NEXT(iter, key, ptr, data, ptr);
}

enum chash_ret chash_iter_ptr_luint_next(struct chash_iter *iter, 
  const void **key, unsigned long int **data) {
    struct chash *hash = iter->hash;
    assert(hash->key_type == CHASH_TYPE_PTR);
    assert((hash->data_type == CHASH_TYPE_LUINT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_LUINT;

    CHASH_ITER_NEXT(iter, key, ptr, data, luint);
}

enum chash_ret chash_iter_luint_ptr_next(struct chash_iter *iter, 
  unsigned long int *key, void ***data) {
    struct chash *hash = iter->hash;
    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_ITER_NEXT(iter, key, luint, data, ptr);
}

enum chash_ret chash_iter_luint_luint_next(struct chash_iter *iter, 
  unsigned long int *key, unsigned long int **data) {
    struct chash *hash = iter->hash;
    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_LUINT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_LUINT;

    CHASH_ITER_NEXT(iter, key, luint, data, luint);
}

enum chash_ret chash_iter_luint_dbl_next(struct chash_iter *iter, 
  unsigned long int *key, double **data) {
    struct chash *hash = iter->hash;
    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_DBL) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_DBL;

    CHASH_ITER_NEXT(iter, key, luint, data, dbl); 
}

enum chash_ret chash_iter_luint_flt_next(struct chash_iter *iter, 
  unsigned long int *key, float **data) {
    struct chash *hash = iter->hash;
    assert(hash->key_type == CHASH_TYPE_LUINT);
    assert((hash->data_type == CHASH_TYPE_FLT) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_FLT;

    CHASH_ITER_NEXT(iter, key, luint, data, flt); 
}

/* specialisation for str type */

#define CHASH_STR_ITER_NEXT(iter, key, keylen, data, datatype)                \
    do {                                                                      \
        unsigned int tablesize = BIT_POW2((iter)->hash->bits);                \
        struct chash_link *curr = (iter)->curr->next;                         \
                                                                              \
        while (!curr) {                                                       \
            if ((iter)->currpos < tablesize) {                                \
                curr = (iter)->hash->table[(iter)->currpos++];                \
            } else {                                                          \
                return CHASH_ITER_FINISH;                                     \
            }                                                                 \
        }                                                                     \
                                                                              \
        (iter)->curr = curr;                                                  \
        *(data) = &curr->data.d_ptr;                                          \
        *keylen = curr->key.k_str.len;                                        \
        *(key) = &iter->hash->strings.strings[curr->key.k_str.ptr];           \
        return CHASH_OK;                                                      \
    } while (0)

enum chash_ret chash_iter_nstr_ptr_next(struct chash_iter *iter, 
  const char **key, unsigned int *keylen, void ***data) {
    struct chash *hash = iter->hash;
    assert(hash->key_type == CHASH_TYPE_STR);
    assert((hash->data_type == CHASH_TYPE_PTR) 
      || (hash->data_type == CHASH_TYPE_UNKNOWN));
    hash->data_type = CHASH_TYPE_PTR;

    CHASH_STR_ITER_NEXT(iter, key, keylen, data, ptr);
}

/* str to nstr converters */

enum chash_ret chash_str_ptr_insert(struct chash *hash, 
  const char *key, void *data) {
    return chash_nstr_ptr_insert(hash, key, str_len(key), data);
}

enum chash_ret chash_str_ptr_remove(struct chash *hash, 
  const char *key, void **data) {
    return chash_nstr_ptr_remove(hash, key, str_len(key), data);
}

enum chash_ret chash_str_ptr_find(struct chash *hash, const char *key, 
  void ***data) {
    return chash_nstr_ptr_find(hash, key, str_len(key), data);
}

enum chash_ret chash_str_ptr_find_insert(struct chash *hash, 
  const char *key, void ***fnd_data, void *ins_data, int *find) {
    return chash_nstr_ptr_find_insert(hash, key, str_len(key), 
        fnd_data, ins_data, find);
}

#ifdef CHASH_TEST
#include <stdio.h>

unsigned int inthash(unsigned int key) {
    return key;
}

void luint_ptr_free(void *opaque, unsigned long int key, void **data) {
    free(*data);
}

int test_str() {
    struct chash *hash;
    const char *and = "and";
    const char *or = "or";
    const char *the = "the";
    const char *not = "not";
    const char *a = "a";
    const char *medium = "medium";
    const char *longstr = "blasdfjas;dljb;alsjkdf;lasjfdbasl;djfoeww;mlkvasdo"
      "fwe;lawjrf;lsjl;fjasl;dfja;slkdjf;laejri;oajj;lbvjasdo;fijeo;fjal;sn;o"
      "gvaeoirfal;skenfl;aksjoijre;ajflkans;ldgjaoiejr;lasjfdlknas;ovijasoier"
      ";laskenf;lkasnfvoiajdsf;ojawe;lrkn;wlerkjwpoieur9p2u349823958y2o;3lkjn"
      "52;o5iu29-8y52h52;n35;2lkn5o2pi35p92875928h5o2n5235h-928735-92835po2n3"
      "5k2hj35p9278359072p5oiuh2;o5n23o5iu2-98375-2983h5o25n3p[209u5-928735-9"
      "2h5[3o2kn5o2i3j5-092723-87295ioh23;k5jn2309572035972o3i5hn2;35p2o";
    const char *longstr2 = "xxxsdfjas;dljb;alsjkdf;lasjfdbasl;djfoeww;mlkvasdo"
      "fwe;lawjrf;lsjl;fjasl;dfja;slkdjf;laejri;oajj;lbvjasdo;fijeo;fjal;sn;o"
      "gvaeoirfal;skenfl;aksjoijre;ajflkans;ldgjaoiejr;lasjfdlknas;ovijasoier"
      ";laskenf;lkasnfvoiajdsf;ojawe;lrkn;wlerkjwpoieur9p2u349823958y2o;3lkjn"
      "52;o5iu29-8y52h52;n35;2lkn5o2pi35p92875928h5o2n5235h-928735-92835po2n3"
      "5k2hj35p9278359072p5oiuh2;o5n23o5iu2-98375-2983h5o25n3p[209u5-928735-9"
      "2h5[3o2kn5o2i3j5-092723-87295ioh23;k5jn2309572035972o3i5hn2;35p2olsdk8";
    enum chash_ret ret;
    void *found;
    void **ifound;

    if ((hash = chash_str_new(0, 2.0, str_nhash))) {
        /* insert some elements */
        ret = chash_str_ptr_insert(hash, and, (char *) and);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, the, (char *) the);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, or, (char *) or);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, not, (char *) not);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, a, (char *) a);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, medium, (char *) medium);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, longstr, (char *) longstr);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, longstr2, (char *) longstr2);
        assert(ret == CHASH_OK);
        assert(chash_size(hash) == 8);

        /* find the elements */
        ret = chash_str_ptr_find(hash, and, &ifound);
        assert(ret == CHASH_OK && *ifound == (void *) and);
        ret = chash_str_ptr_find(hash, or, &ifound);
        assert(ret == CHASH_OK && *ifound == (void *) or);
        ret = chash_str_ptr_find(hash, not, &ifound);
        assert(ret == CHASH_OK && *ifound == (void *) not);
        ret = chash_str_ptr_find(hash, the, &ifound);
        assert(ret == CHASH_OK && *ifound == (void *) the);
        ret = chash_str_ptr_find(hash, a, &ifound);
        assert(ret == CHASH_OK && *ifound == (void *) a);
        ret = chash_str_ptr_find(hash, medium, &ifound);
        assert(ret == CHASH_OK && *ifound == (void *) medium);
        ret = chash_str_ptr_find(hash, longstr, &ifound);
        assert(ret == CHASH_OK && *ifound == (void *) longstr);
        ret = chash_str_ptr_find(hash, longstr2, &ifound);
        assert(ret == CHASH_OK && *ifound == (void *) longstr2);

        /* remove the elements */
        ret = chash_str_ptr_remove(hash, and, &found);
        assert(ret == CHASH_OK && found == (void *) and);
        ret = chash_str_ptr_remove(hash, or, &found);
        assert(ret == CHASH_OK && found == (void *) or);
        ret = chash_str_ptr_remove(hash, not, &found);
        assert(ret == CHASH_OK && found == (void *) not);
        ret = chash_str_ptr_remove(hash, the, &found);
        assert(ret == CHASH_OK && found == (void *) the);
        ret = chash_str_ptr_remove(hash, a, &found);
        assert(ret == CHASH_OK && found == (void *) a);
        ret = chash_str_ptr_remove(hash, medium, &found);
        assert(ret == CHASH_OK && found == (void *) medium);
        ret = chash_str_ptr_remove(hash, longstr, &found);
        assert(ret == CHASH_OK && found == (void *) longstr);
        ret = chash_str_ptr_remove(hash, longstr2, &found);
        assert(ret == CHASH_OK && found == (void *) longstr2);
        assert(chash_size(hash) == 0);

        /* insert them back */
        ret = chash_str_ptr_insert(hash, and, (char *) and);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, the, (char *) the);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, or, (char *) or);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, not, (char *) not);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, a, (char *) a);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, medium, (char *) medium);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, longstr, (char *) longstr);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, longstr2, (char *) longstr2);
        assert(ret == CHASH_OK);

        /* remove the elements */
        ret = chash_str_ptr_remove(hash, and, &found);
        assert(ret == CHASH_OK && found == (void *) and);
        ret = chash_str_ptr_remove(hash, or, &found);
        assert(ret == CHASH_OK && found == (void *) or);
        ret = chash_str_ptr_remove(hash, not, &found);
        assert(ret == CHASH_OK && found == (void *) not);
        ret = chash_str_ptr_remove(hash, the, &found);
        assert(ret == CHASH_OK && found == (void *) the);
        ret = chash_str_ptr_remove(hash, a, &found);
        assert(ret == CHASH_OK && found == (void *) a);
        ret = chash_str_ptr_remove(hash, medium, &found);
        assert(ret == CHASH_OK && found == (void *) medium);
        ret = chash_str_ptr_remove(hash, longstr, &found);
        assert(ret == CHASH_OK && found == (void *) longstr);
        ret = chash_str_ptr_remove(hash, longstr2, &found);
        assert(ret == CHASH_OK && found == (void *) longstr2);
        assert(chash_size(hash) == 0);

        /* fail finding elements */
        ret = chash_str_ptr_find(hash, and, &ifound);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_find(hash, or, &ifound);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_find(hash, not, &ifound);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_find(hash, the, &ifound);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_find(hash, a, &ifound);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_find(hash, medium, &ifound);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_find(hash, longstr, &ifound);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_find(hash, longstr2, &ifound);
        assert(ret == CHASH_ENOENT);

        /* fail removing elements */
        ret = chash_str_ptr_remove(hash, and, &found);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_remove(hash, or, &found);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_remove(hash, not, &found);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_remove(hash, the, &found);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_remove(hash, a, &found);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_remove(hash, medium, &found);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_remove(hash, longstr, &found);
        assert(ret == CHASH_ENOENT);
        ret = chash_str_ptr_remove(hash, longstr2, &found);
        assert(ret == CHASH_ENOENT);

        /* insert them back */
        ret = chash_str_ptr_insert(hash, and, (char *) and);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, the, (char *) the);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, or, (char *) or);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, not, (char *) not);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, a, (char *) a);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, medium, (char *) medium);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, longstr, (char *) longstr);
        assert(ret == CHASH_OK);
        ret = chash_str_ptr_insert(hash, longstr2, (char *) longstr2);
        assert(ret == CHASH_OK);

        /* clear elements */
        chash_clear(hash);
        assert(CHASH_SIZE(hash) == 0);

        chash_delete(hash);
        return 1;
    } else {
        fprintf(stderr, "hashtable failed init\n");
        return 0;
    }
}

int main() {
    struct chash *hash;
    unsigned int i,
                 size = 10000;
    int ret;
    void *vptr,
         **vptrptr;
    unsigned int *ptr;

    if (!test_str()) {
        fprintf(stderr, "string hashtable failed\n");
        return EXIT_FAILURE;
    }

    if ((hash = chash_luint_new(0, 2.0))) {
        /* insert some elements */
        for (i = 0; i < size; i++) {
            assert(CHASH_SIZE(hash) == i);
            ptr = malloc(sizeof(*ptr));
            assert(ptr);
            *ptr = i;
            ret = chash_luint_ptr_insert(hash, i, ptr);
            assert(ret == CHASH_OK);
            assert(CHASH_SIZE(hash) == i + 1);
        }

        /* find the elements */
        for (i = 0; i < size; i++) {
            ret = chash_luint_ptr_find(hash, i, &vptrptr);
            assert(ret == CHASH_OK);
            assert((ptr = *vptrptr) && *ptr == i);
        }

        /* remove the elements */
        for (i = 0; i < size / 2; i++) {
            ret = chash_luint_ptr_remove(hash, i, &vptr);
            assert(ret == CHASH_OK);
            assert((ptr = vptr) && *ptr == i);
            free(ptr);
        }

        /* insert them back */
        for (i = 0; i < size / 2; i++) {
            assert(CHASH_SIZE(hash) == size / 2 + i);
            ptr = malloc(sizeof(*ptr));
            assert(ptr);
            *ptr = i;
            ret = chash_luint_ptr_insert(hash, i, ptr);
            assert(ret == CHASH_OK);
            assert(CHASH_SIZE(hash) == size / 2 + i + 1);
        }

        /* remove the elements */
        for (i = 0; i < size / 2; i++) {
            ret = chash_luint_ptr_remove(hash, i, &vptr);
            assert(ret == CHASH_OK);
            assert((ptr = vptr) && *ptr == i);
            free(ptr);
        }

        /* fail finding elements */
        for (i = 0; i < size / 2; i++) {
            ret = chash_luint_ptr_find(hash, i, &vptrptr);
            assert(ret == CHASH_ENOENT);
        }

        /* find remaining */
        for (; i < size; i++) {
            ret = chash_luint_ptr_find(hash, i, &vptrptr);
            assert(ret == CHASH_OK);
            assert((ptr = *vptrptr) && *ptr == i);
        }

        /* fail removing elements */
        for (i = 0; i < size / 2; i++) {
            ret = chash_luint_ptr_remove(hash, i, &vptr);
            assert(ret == CHASH_ENOENT);
        }

        /* clear elements */
        chash_luint_ptr_foreach(hash, NULL, luint_ptr_free);
        chash_clear(hash);
        assert(CHASH_SIZE(hash) == 0);

        /* fail to find the elements */
        for (i = 0; i < size; i++) {
            ret = chash_luint_ptr_find(hash, i, &vptrptr);
            assert(ret == CHASH_ENOENT);
        }

        chash_delete(hash);
    } else {
        fprintf(stderr, "hashtable failed init\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
#endif

