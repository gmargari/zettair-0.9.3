/* reposset.c implements an object that manages the set of
 * repositories, as declared in reposset.h.
 *
 * FIXME: There is a problem with the strategy of saving/loading through the
 * docmap, which is that there is no way of determining where checkpoints go 
 * via the information in the docmap (???).
 *
 * written nml 2006-04-26
 *
 */

#include "firstinclude.h"

#include "reposset.h"
#include "_reposset.h"

#include "def.h"
#include "binsearch.h"
#include "mime.h"
#include "vec.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define INIT_ARRAY_SIZE 4

struct reposset {
    unsigned int entries;              /* number of repositories in set */

    struct reposset_record *rec;       /* sorted array of records */
    unsigned int rec_size;             /* size of records array */
    unsigned int rec_len;              /* number of records currently in rec */

    struct reposset_check *check;      /* sorted array of checkpoints */
    unsigned int check_size;           /* size of checkpoint array */
    unsigned int check_len;            /* entries currently in check */
};

static int check_cmp(const void *vone, const void *vtwo) {
    const struct reposset_check *one = vone,
                                *two = vtwo;

    if (one->reposno < two->reposno) {
        return -1;
    } else if (one->reposno > two->reposno) {
        return 1;
    } else if (one->offset < two->offset) {
        return -1;
    } else if (one->offset > two->offset) {
        return 1;
    } else {
        return 0;
    }
}

static int rec_docno_cmp(const void *vone, const void *vtwo) {
    const struct reposset_record *one = vone,
                                 *two = vtwo;
    unsigned int one_end = one->docno + one->quantity,
                 two_end = two->docno + two->quantity;

    /* have to detect overlaps between the two ranges so that searching works
     * properly.  bugger :o( */
    if (one->docno < two->docno) {
        if (one_end > two->docno) {
            return 0;
        } else {
            return -1;
        }
    } else if (one->docno > two->docno) {
        if (two_end > one->docno) {
            return 0;
        } else {
            return 1;
        }
    } else {
        return 0;
    }
}

struct reposset *reposset_new() {
    struct reposset *rset = malloc(sizeof(*rset));

    if (rset && (rset->rec = malloc(sizeof(*rset->rec) * INIT_ARRAY_SIZE))
      && (rset->check = malloc(sizeof(*rset->check) * INIT_ARRAY_SIZE))) {

        rset->rec_size = rset->check_size = INIT_ARRAY_SIZE;
        reposset_clear(rset);
    } else if (rset) {
        if (rset->rec) {
            free(rset->rec);
        }
        if (rset->check) {
            free(rset->check);
        }
        free(rset);
        rset = NULL;
    }

    return rset;
}

void reposset_print(struct reposset *rset, FILE *out) {
    unsigned int i;

    for (i = 0; i < rset->rec_len; i++) {
        if (rset->rec[i].rectype == REPOSSET_MANY_FILES) {
            printf("files %u - %u contain docnos %u - %u\n", 
              rset->rec[i].reposno,
              rset->rec[i].reposno + rset->rec[i].quantity - 1,
              rset->rec[i].docno,
              rset->rec[i].docno + rset->rec[i].quantity - 1);
        } else {
            printf("file %u contains docnos %u - %u\n", 
              rset->rec[i].reposno,
              rset->rec[i].docno,
              rset->rec[i].docno + rset->rec[i].quantity - 1);
        }
    }
    if (rset->rec_len) {
        printf("\n");
    }

    for (i = 0; i < rset->check_len; i++) {
        printf("checkpoint %u %lu: %s\n", rset->check[i].reposno,
          rset->check[i].offset, mime_string(rset->check[i].comp));
    }
}

void reposset_delete(struct reposset *rset) {
    assert(rset->rec);
    assert(rset->check);
    free(rset->rec);
    free(rset->check);
    free(rset);
}

enum reposset_ret reposset_append(struct reposset *rset, 
  unsigned int start_docno, unsigned int *reposno) {
    if (rset->entries 
      && rset->rec[rset->rec_len - 1].rectype == REPOSSET_MANY_FILES
      && rset->rec[rset->rec_len - 1].docno 
        + rset->rec[rset->rec_len - 1].quantity == start_docno) {

        /* just append this record onto the last many-files record */
        rset->rec[rset->rec_len - 1].quantity++;
    } else {
        /* have to add a new record */
        if (rset->rec_len >= rset->rec_size) {
            void *ptr 
              = realloc(rset->rec, sizeof(*rset->rec) * rset->rec_size * 2);

            if (ptr) {
                rset->rec = ptr;
                rset->rec_size *= 2;
                assert(rset->rec_len < rset->rec_size);
            } else {
                return REPOSSET_ENOMEM;
            }
        }

        rset->rec[rset->rec_len].rectype = REPOSSET_MANY_FILES;
        rset->rec[rset->rec_len].docno = start_docno;
        rset->rec[rset->rec_len].reposno = rset->entries;
        rset->rec[rset->rec_len].quantity = 1;
        rset->rec_len++;
    }

    *reposno = rset->entries++;
    return REPOSSET_OK;
}

enum reposset_ret reposset_append_docno(struct reposset *rset, 
  unsigned int docno, unsigned int docs) {
    assert(docs);
    if (rset->entries 
      && rset->rec[rset->rec_len - 1].docno 
        + rset->rec[rset->rec_len - 1].quantity - 1 == docno) {
        /* first docno in the file, ignore */
    } else if (rset->entries
      && rset->rec[rset->rec_len - 1].docno 
        + rset->rec[rset->rec_len - 1].quantity == docno
      && (rset->rec[rset->rec_len - 1].rectype == REPOSSET_SINGLE_FILE
        || (rset->rec[rset->rec_len - 1].rectype == REPOSSET_MANY_FILES
          && (rset->rec[rset->rec_len - 1].quantity == 1)))) {

        /* just add the docnos onto the existing record */
        rset->rec[rset->rec_len - 1].rectype = REPOSSET_SINGLE_FILE;
        rset->rec[rset->rec_len - 1].quantity += docs;
    } else if (rset->entries
      && rset->rec[rset->rec_len - 1].rectype == REPOSSET_MANY_FILES
      && rset->rec[rset->rec_len - 1].docno 
        + rset->rec[rset->rec_len - 1].quantity == docno) {

        /* have to add a new record */
        if (rset->rec_len >= rset->rec_size) {
            void *ptr 
              = realloc(rset->rec, sizeof(*rset->rec) * rset->rec_size * 2);

            if (ptr) {
                rset->rec = ptr;
                rset->rec_size *= 2;
                assert(rset->rec_len < rset->rec_size);
            } else {
                return REPOSSET_ENOMEM;
            }
        }

        /* adjust previous entry */
        rset->rec[rset->rec_len - 1].quantity--;

        /* insert new entry */
        rset->rec[rset->rec_len].rectype = REPOSSET_SINGLE_FILE;
        rset->rec[rset->rec_len].docno = docno - 1;
        rset->rec[rset->rec_len].reposno = rset->entries - 1;
        rset->rec[rset->rec_len].quantity = docs + 1;
        rset->rec_len++;
    } else {
        assert("can't get here" && 0);
        return REPOSSET_EINVAL;
    }

    return REPOSSET_OK;
}

enum reposset_ret reposset_add_checkpoint(struct reposset *rset, 
  unsigned int reposno, enum mime_types comp, unsigned long int point) {
    /* XXX: technically, reposno should be below the number of
     * entries, but we'll allow checkpoints to be entered for the
     * coming repository as well, for convenience sake.  Also, we
     * should probably maintain the sorting of the array explicitly,
     * but we assume that they come in sorted order. */
    assert(comp == MIME_TYPE_APPLICATION_X_GZIP 
      || comp == MIME_TYPE_APPLICATION_X_BZIP2);

    assert(!rset->check_len 
      || rset->check[rset->check_len - 1].reposno < reposno);

    if (rset->check_len >= rset->check_size) {
        void *ptr = realloc(rset->check, 
            sizeof(*rset->check) * rset->check_size * 2);

        if (ptr) {
            rset->check = ptr;
            rset->check_size *= 2;
            assert(rset->check_len < rset->check_size);
        } else {
            return REPOSSET_ENOMEM;
        }
    }

    rset->check[rset->check_len].reposno = reposno;
    rset->check[rset->check_len].offset = point;
    rset->check[rset->check_len].comp = comp;
    assert(!rset->check_len || check_cmp(&rset->check[rset->check_len - 1], 
      &rset->check[rset->check_len]) < 0);  /* ensure ordering is correct */
    rset->check_len++;
    return REPOSSET_OK;
}

unsigned int reposset_reposno_rec(struct reposset_record *rec, 
  unsigned int docno) {
    if (rec->rectype == REPOSSET_SINGLE_FILE) {
        return rec->reposno;
    } else {
        return rec->reposno + docno - rec->docno;
    }
}

enum reposset_ret reposset_reposno(struct reposset *rset, unsigned int docno, 
  unsigned int *reposno) {
    struct reposset_record *find,
                           target;

    target.docno = docno;
    target.quantity = 1;

    /* find the correct page in the map */
    find = binsearch(&target, rset->rec, rset->rec_len, sizeof(target), 
        rec_docno_cmp);

    if (find < rset->rec + rset->rec_len && find->docno <= docno 
      && find->docno + find->quantity > docno) {
        *reposno = reposset_reposno_rec(find, docno);
        return REPOSSET_OK;
    } else {
        return REPOSSET_ENOENT;
    }
}

unsigned int reposset_entries(struct reposset *rset) {
    return rset->entries;
}

struct reposset_record *reposset_record_last(struct reposset *rset) {
    if (rset->rec_len) {
        return &rset->rec[rset->rec_len - 1];
    } else {
        return NULL;
    }
}

struct reposset_record *reposset_record(struct reposset *rset, 
  unsigned int docno) {
    struct reposset_record *find,
                           target;

    target.docno = docno;
    target.quantity = 1;

    /* find the correct page in the map */
    find = binsearch(&target, rset->rec, rset->rec_len, sizeof(target), 
        rec_docno_cmp);

    if (find < rset->rec + rset->rec_len && find->docno <= docno 
      && find->docno + find->quantity > docno) {
        return find;
    } else {
        return NULL;
    }
}

enum reposset_ret reposset_set_record(struct reposset *rset, 
  struct reposset_record *rec) {
    unsigned int max = rec->reposno + 1;

    /* XXX: again, can't be bothered actively sorting entries here,
     * ensure that they come sorted */
    assert(!rset->entries 
      || rec_docno_cmp(&rset->rec[rset->rec_len - 1], rec) < 0);
    assert(!rset->entries 
      || rset->rec[rset->rec_len - 1].reposno < rec->reposno);

    /* have to add a new record */
    if (rset->rec_len >= rset->rec_size) {
        void *ptr 
          = realloc(rset->rec, sizeof(*rset->rec) * rset->rec_size * 2);

        if (ptr) {
            rset->rec = ptr;
            rset->rec_size *= 2;
            assert(rset->rec_len < rset->rec_size);
        } else {
            return REPOSSET_ENOMEM;
        }
    }

    /* update entries */
    if (rec->rectype == REPOSSET_MANY_FILES) {
        max += rec->quantity;
    }
    if (max > rset->entries) {
        rset->entries = max;
    }

    rset->rec[rset->rec_len++] = *rec;
    return REPOSSET_OK;
}

void reposset_clear(struct reposset *rset) {
    rset->entries = rset->rec_len = rset->check_len = 0;
}

struct reposset_check *reposset_check(struct reposset *rset, 
  unsigned int reposno) {
    struct reposset_check *find,
                          target;

    target.reposno = reposno;
    target.offset = 0;

    /* find the correct page in the map */
    find = binsearch(&target, rset->check, rset->check_len, sizeof(target), 
        check_cmp);

    if (find < rset->check + rset->check_len && find->reposno == reposno) {
        return find;
    } else {
        return NULL;
    }
}

struct reposset_check *reposset_check_first(struct reposset *rset) {
    return rset->check;
}

unsigned int reposset_checks(struct reposset *rset) {
    return rset->check_len;
}

