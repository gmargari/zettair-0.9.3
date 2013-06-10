/* freemap.c implements a reasonable sorted-list freemap to allocate disk space
 * for vectors.
 * 
 * The freemap has undergone a number of redesign phases in order to address
 * performance issues (it accumulates a large number of entries during
 * updates). The current design is to have one large linked list with all free
 * entries on it, with a partial red-black tree index keyed on location to allow
 * quick reallocation/freeing with merging of contiguous entries.  We also have
 * a seperate set of linked lists that list free entries in a log range, which
 * allows us to malloc entries with a size requirement relatively quickly.
 *
 * written nml 2003-04-28
 *
 */

#include "firstinclude.h"

#include "freemap.h"

#include "bit.h"
#include "rbtree.h"
#include "def.h"
#include "str.h"
#include "lcrand.h"
#include "objalloc.h"

#include "zstdint.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

enum freerec_flags {
    FREEREC_INDEXED = (1 << 0)            /* whether this record is in the 
                                           * red-black index tree */
};

/* struct to hold a single free record */
struct freerec {
    struct freerec *next;                 /* next in linked list */
    struct freerec *prev;                 /* previous in linked list */
    struct freerec *next_size;            /* next in size-based linked list */
    struct freerec *prev_size;            /* previous in size-based list */
    unsigned int fileno;                  /* what file space occurs in */
    unsigned long int offset;             /* what offset it occurs at */
    unsigned int size;                    /* how big it is */
    enum freerec_flags flags;             /* flags associated with this record 
                                           * (see enum above) */
};

/* number of lists we index */
#define SIZELISTS (sizeof(int) * 8)

struct freemap {
    struct rbtree *index;                 /* red-black tree storing some of the 
                                           * entries in the linked list, to 
                                           * provide fast ordered access to the 
                                           * linked list entries */
    struct freerec *sizeindex[SIZELISTS]; /* pointers to linked list of free 
                                           * entries where each entry is of 
                                           * size 2^index - 2^(index + 1) */
    struct freerec *sizetail[SIZELISTS];  /* pointers to tails of size-based 
                                           * linked lists */
    struct freerec *first;                /* sorted linked list of free 
                                           * records */
    struct freerec *unused;               /* linked list of unused free 
                                           * records */
    unsigned int entries;                 /* number of entries in linked list */
    int err;                              /* last occuring error */
    double space;                         /* amount of space in map */
    double waste;                         /* wasted space */
    unsigned int append;                  /* how much space we're allowed to 
                                           * append to keep the number of 
                                           * entries down */
    unsigned int files;                   /* how many files we have 
                                           * instanciated */
    enum freemap_strategies strategy;     /* allocation strategy */
    unsigned int index_mark;              /* integer between 0 and RAND_MAX that
                                           * indicates what percentage of items 
                                           * to index */
    struct lcrand *rand;                  /* random number sequence */
    struct objalloc *alloc;               /* node allocator */
    void *opaque;                         /* opaque pointer for newfile fn */
    int (*newfile)(void *opaque,          /* what to call to find out size */
      unsigned int file,                  /* limit on a new file */
      unsigned int *maxsize);
};

/* internal function to help qsort a set of freerecs (assuming that they can't
 * overlap) - sorts into reverse order */
static int freerec_cmp(const void *one, const void *two) {
    const struct freerec *node1 = one,
                         *node2 = two;

    /* have to do it the slow way (not subtraction) because of the danger of
     * overflow (think *very* hard before you change this - what happens when
     * you compare UINT_MAX with 0?) */
    if (node1->fileno == node2->fileno) {
        if (node1->offset < node2->offset) {
            return -1;
        } else if (node1->offset > node2->offset) {
            return 1;
        } else {
            return 0;
        }
    } else {
        if (node1->fileno < node2->fileno) {
            return -1;
        } else if (node1->fileno > node2->fileno) {
            return 1;
        } else {
            return 0;
        }
    }
}

/* internal function to return the size of a given size-indexed list */
static unsigned int sizelist_size(const struct freerec *rec, 
  unsigned int *bytes) {
    unsigned int size = 0,
                 lbytes = 0;

    while (rec) {
        size++;
        lbytes += rec->size;
        rec = rec->next_size;
    }

    if (bytes) {
        *bytes = lbytes;
    }

    return size;
}

/* internal function to ensure that the freemap is sane */
static int freemap_invariant(struct freemap *map) {
    if (DEAR_DEBUG) {
        struct freerec *curr,
                       *prev;
        unsigned int count = 0,
                     count2,
                     count3,
                     count4,
                     i;
        int cmp;

        count = 0;
        for ((prev = NULL), curr = map->first; curr; 
          (prev = curr), (curr = curr->next), count++) {
            /* check that linked lists are linked correctly */
            if ((curr->prev != prev) || (curr->next == map->first)) {
                assert(!CRASH);
                return 0;
            }

            /* check for overlap and zero-length and incorrect sorting 
             * in entries */
            if (prev) {
                if (!curr->size 
                  || ((curr->fileno == prev->fileno) 
                    && ((curr->offset <= prev->offset + prev->size) 
                      || (curr->offset <= prev->offset)
                      || (prev->offset + prev->size <= prev->offset)))) {

                    assert(!CRASH);
                    return 0;
                }
            }
        }

        for ((curr = prev), prev = NULL; curr; 
          (prev = curr), (curr = curr->prev)) {
            /* check that linked lists are linked correctly */
            if (curr->next != prev) {
                assert(!CRASH);
                return 0;
            }
        }
      
        count4 = 0;
        for (i = 0; i < SIZELISTS; i++) {
            if (map->sizeindex[i] && map->sizeindex[i]->prev_size) {
                assert(!CRASH);
                return 0;
            }

            for ((prev = NULL), curr = map->sizeindex[i]; curr; 
              (prev = curr), (curr = curr->next_size), count4++) {
                /* check that linked lists are linked correctly */
                if ((curr->prev_size != prev) 

                  /* try to check for loops in lists */
                  || (curr->next_size == map->sizeindex[i]) 
                    || (curr->next_size 
                      && (curr->next_size == curr->prev_size))) {

                    assert(!CRASH);
                    return 0;
                }

                /* check that the entry is of the correct size */
                if (bit_log2(curr->size) != i) {
                    assert(!CRASH);
                    return 0;
                }
            }
            assert(map->sizetail[i] == prev);

            for ((curr = prev), prev = NULL; curr; 
              (prev = curr), (curr = curr->prev_size)) {
                /* check that linked lists are linked correctly */
                if (curr->next_size != prev) {
                    assert(!CRASH);
                    return 0;
                }
            }
        }

        count3 = 0;
        for ((prev = NULL), curr = map->unused; curr; 
          (prev = curr), (curr = curr->next), count3++) {
            /* check that linked lists are linked correctly */
            if (curr->prev != prev) {
                assert(!CRASH);
                return 0;
            }

            /* unused entries shouldn't be indexed */
            if (curr->flags) {
                assert(!CRASH);
                return 0;
            }

            /* unused entries shouldn't be sizeindexed */
            if (curr->prev_size || curr->next_size) {
                assert(!CRASH);
                return 0;
            }

            /* check for overlap and incorrect sorting in entries */
            if (prev) {
                if ((curr->fileno == prev->fileno) 
                  && ((curr->offset <= prev->offset + prev->size) 
                    || (curr->offset <= prev->offset))) {

                    assert(!CRASH);
                    return 0;
                }
            }
        }

        for ((curr = prev), prev = NULL; curr; 
          (prev = curr), (curr = curr->prev)) {
            /* check that linked lists are linked correctly */
            if (curr->next != prev) {
                assert(!CRASH);
                return 0;
            }
        }

        /* check entries count is consistent */
        if (map->entries != count + count3) {
            assert(!CRASH);
            return 0;
        }

        /* check that all entries are in sizeindex */
        if (count4 != count) {
            assert(!CRASH);
            return 0;
        }

        /* check that free and unused lists don't overlap */
        curr = map->first;
        prev = map->unused;
        while (curr && prev) {
            cmp = freerec_cmp(curr, prev);

            if (!cmp) {
                assert(!CRASH);
                return 0;
            } else if (cmp < 0) {
                if ((curr->fileno == prev->fileno) 
                  && (curr->offset + curr->size > prev->offset)) {
                    assert(!CRASH);
                    return 0;
                }

                curr = curr->next;
            } else if (cmp > 0) {
                if ((curr->fileno == prev->fileno) 
                  && (prev->offset + prev->size > curr->offset)) {
                    freemap_print(map, stdout);
                    assert(!CRASH);
                    return 0;
                }

                prev = prev->next;
            }
        }

        /* check that unused list has only one entry per file */
        for ((prev = NULL), curr = map->unused; curr; 
          (prev = curr), curr = curr->next) {
            if (prev && (prev->fileno == curr->fileno)) {
                assert(!CRASH);
                return 0;
            }
        }

        /* count number of entries without links to size-based list, and ensure
         * that its the same as the number of singleton entries in sizeindex */
        count = 0;
        count2 = 0;
        for (curr = map->first; curr; curr = curr->next) {
            if (!curr->next_size && !curr->prev_size) {
                count++;
            }
        }
        for (i = 0; i < SIZELISTS; i++) {
            if (map->sizeindex[i] && !map->sizeindex[i]->next_size) {
                count2++;
            }
        }
        if (count != count2) {
            assert(!CRASH);
            return 0;
        }

        /* count number of entries in sizeindex and ensure that its the
         * same as number of entries in freelist */
        count = 0;
        count2 = 0;
        for (curr = map->first; curr; curr = curr->next) {
            count++;
        }
        for (i = 0; i < SIZELISTS; i++) {
            for (curr = map->sizeindex[i]; curr; curr = curr->next_size) {
                count2++;
            }
        }
        if (count != count2) {
            assert(!CRASH);
            return 0;
        }
    }

    return 1;
}

struct freemap *freemap_new(enum freemap_strategies strategy,
  unsigned int append, void *opaque, 
  int (*addfile)(void *opaque, unsigned int file, unsigned int *maxsize)) {
    struct freemap *map;
    unsigned int i;

    if ((strategy != FREEMAP_STRATEGY_FIRST) 
      && (strategy != FREEMAP_STRATEGY_BEST) 
      && (strategy != FREEMAP_STRATEGY_WORST)
      && (strategy != FREEMAP_STRATEGY_CLOSE)) {
        return NULL;
    }

    if ((map = malloc(sizeof(*map))) 
      && (map->index = rbtree_ptr_new(freerec_cmp))
      && (map->rand = lcrand_new(time(NULL)))
      && (map->alloc 
        /* XXX: should allow setting of chunksize and underlying allocator for 
         * object alloctor from outside interface */
        = objalloc_new(sizeof(struct freerec), 0, !!DEAR_DEBUG, 1024, NULL))) {

        map->first = map->unused = NULL;
        map->strategy = strategy;
        map->entries = 0;
        map->err = 0;
        map->space = map->waste = 0.0;
        map->append = append;
        map->opaque = opaque;
        map->newfile = addfile;
        map->files = 0;

        /* FIXME: set via interface */
        map->index_mark = ((unsigned int) 0.2 * LCRAND_MAX);  

        for (i = 0; i < SIZELISTS; i++) {
            map->sizeindex[i] = NULL;
            map->sizetail[i] = NULL;
        }

        if (!freemap_invariant(map)) {
            freemap_delete(map);
            map = NULL;
        }
    } else {
        if (map) {
            if (map->index) {
                if (map->rand) {
                    lcrand_delete(map->rand);
                    map->rand = NULL;
                }
                rbtree_delete(map->index);
                map->index = NULL;
            }
            free(map);
            map = NULL;
        }
    }

    return map;
}

void freemap_delete(struct freemap *map) {
    assert(freemap_invariant(map));

    rbtree_delete(map->index);
    lcrand_delete(map->rand);
    objalloc_clear(map->alloc);
    objalloc_delete(map->alloc);
    free(map);
    return;
}

/* macros to alter the sizeindex for individual records easily */

#define SIZEINDEX(map, rec)                                                   \
    do {                                                                      \
        unsigned int sizeindex_index;                                         \
                                                                              \
        sizeindex_index = bit_log2(rec->size);                                \
        assert(!rec->next_size);                                              \
        assert(!rec->prev_size);                                              \
        rec->prev_size = map->sizetail[sizeindex_index];                      \
        rec->next_size = NULL;                                                \
        if (rec->prev_size) {                                                 \
            assert(!rec->prev_size->next_size);                               \
            rec->prev_size->next_size = rec;                                  \
        } else {                                                              \
            assert(!map->sizeindex[sizeindex_index]);                         \
            map->sizeindex[sizeindex_index] = rec;                            \
        }                                                                     \
        map->sizetail[sizeindex_index] = rec;                                 \
    } while (0)

#define SIZEUNINDEX(map, rec, oldsize)                                        \
    do {                                                                      \
        unsigned int sizeindex_index = bit_log2(oldsize);                     \
                                                                              \
        /* remove from old location */                                        \
        if (rec->prev_size) {                                                 \
            rec->prev_size->next_size = rec->next_size;                       \
        } else {                                                              \
            assert(rec == map->sizeindex[sizeindex_index]);                   \
            map->sizeindex[sizeindex_index] = rec->next_size;                 \
        }                                                                     \
        if (rec->next_size) {                                                 \
            rec->next_size->prev_size = rec->prev_size;                       \
        } else {                                                              \
            assert(rec == map->sizetail[sizeindex_index]);                    \
            map->sizetail[sizeindex_index] = rec->prev_size;                  \
        }                                                                     \
        rec->prev_size = NULL;                                                \
        rec->next_size = NULL;                                                \
    } while (0)

#define SIZEREINDEX(map, rec, oldsize)                                        \
    SIZEUNINDEX(map, rec, oldsize); SIZEINDEX(map, rec)

/* note that at various times during the next few routines we alter records that
 * may be inserted in the index.  This would generally be a problem, since we
 * are altering the key of an item in a sorted data structure, but since this
 * change will not change the sorting order of the index, we should be ok */

static int freemap_malloc_unused(struct freemap *map, unsigned int fileno, 
  unsigned int offset, unsigned int *size, int options, 
  struct freerec *unused) {
    struct freerec *rec,
                   *prev;
    void **find;
    void *findkey;
    unsigned int tmp;

    if (unused->offset != offset) {
        /* we're either going to allocate from the middle or the end, either
         * way we need to insert the start into the free list */
        
        struct freerec key;

        key.offset = offset;
        key.fileno = fileno;

        /* search the red-black tree for the location nearest 
         * specified location */
        if (rbtree_ptr_ptr_find_near(map->index, &key, &findkey, 
            &find) == RBTREE_OK) {

            rec = *find;
            prev = rec->prev;
        } else {
            rec = map->first;
            prev = NULL;
        }

        /* search forward for correct place */
        while (rec && freerec_cmp(rec, &key) < 0) {
            prev = rec;
            rec = rec->next;
        }

        if (prev && (prev->fileno == unused->fileno) 
          && (prev->offset + prev->size == unused->offset)) {
            /* can coalesce first half of unused record into free
             * record */
            tmp = prev->size;
            prev->size += offset - unused->offset;
            SIZEREINDEX(map, prev, tmp);
            unused->size -= offset - unused->offset;
            unused->offset = offset;
        } else {
            /* need to create a new rec to split the current record */
            struct freerec *newrec;

            if ((newrec = objalloc_malloc(map->alloc, sizeof(*newrec)))) {
                newrec->fileno = unused->fileno;
                newrec->offset = unused->offset;
                newrec->size = offset - newrec->offset;

                unused->size -= newrec->size;
                unused->offset = offset;

                /* insert first half into free records, to ensure
                 * that our assumption is maintained */

                newrec->next = rec;
                newrec->prev = prev;
                newrec->next_size = newrec->prev_size = NULL;
                if (newrec->prev) {
                    newrec->prev->next = newrec;
                } else {
                    map->first = newrec;
                }
                if (newrec->next) {
                    newrec->next->prev = newrec;
                }
                SIZEINDEX(map, newrec);

                if ((lcrand(map->rand) <= map->index_mark)
                  /* randomly decided to index this record */
                  && (rbtree_ptr_ptr_insert(map->index, newrec, newrec) 
                      == RBTREE_OK)) {

                    newrec->flags = FREEREC_INDEXED;
                } else {
                    newrec->flags = 0;
                }

                map->entries++;
            } else {
                /* indicate out of memory error */
                map->err = ENOMEM;
                assert(freemap_invariant(map));
                return 0;
            }
        }
    }

    assert(unused->offset == offset);

    if ((unused->size == *size) || (!(options & FREEMAP_OPT_EXACT) 
      && (unused->size <= *size + map->append))) {
        /* unused gets consumed by allocation */
        *size = unused->size;

        if (unused->prev) {
            unused->prev->next = unused->next;
        } else {
            map->unused = unused->next;
        }

        if (unused->next) {
            unused->next->prev = unused->prev;
        }
        assert(!unused->flags);
        objalloc_free(map->alloc, unused);
        map->entries--;
    } else {
        /* tail of unused remains, the rest gets allocated */
        unused->offset += *size;
        unused->size -= *size;
    }
    assert(freemap_invariant(map));
    return 1;
}

/* internal function to handle allocation from a specific location */
static int freemap_malloc_location(struct freemap *map, unsigned int fileno, 
  unsigned int offset, unsigned int *size, int options) {
    struct freerec key,
                   *rec,
                   *prev,
                   *unused;
    void **find;
    void *findkey;
    unsigned int tmp;

    assert(freemap_invariant(map));

    if (!*size) {
        assert(freemap_invariant(map));
        return 1;
    }

    /* note that we make the assumption that unused elements can only occur at
     * the end of the file.  This assumption is localised, because allocating a
     * specific location is the only way we can break that assumption.  If we
     * move all unused space that has allocations after it to the previously
     * used list, then everything works */

    /* search the red-black tree for the location nearest specified location */
    key.fileno = fileno;
    key.offset = offset;
    if (rbtree_ptr_ptr_find_near(map->index, &key, &findkey, &find) 
          == RBTREE_OK) {

        rec = *find;
    } else {
        rec = map->first;
    }

    /* iterate until we hit the first free block that could be it */
    while (rec && ((rec->fileno < fileno) || ((rec->fileno == fileno) 
      && (rec->offset + rec->size) <= offset))) {
        rec = rec->next;
    }

    if (rec && (rec->fileno == fileno) && (rec->offset <= offset) 
      && (rec->offset + rec->size >= offset + *size)) {
        if (rec->offset != offset) {
            if ((rec->size - (offset - rec->offset) == *size) 
              || (!(options & FREEMAP_OPT_EXACT) 
                && (rec->size - (offset - rec->offset) 
                  <= *size + map->append))) {
                
                /* only the start of the record will remain, just shorten it */
                tmp = rec->size;
                rec->size = offset - rec->offset;
                SIZEREINDEX(map, rec, tmp);
            } else {
                /* need to create a new rec to split record */
                struct freerec *newrec;

                if ((newrec = objalloc_malloc(map->alloc, sizeof(*newrec)))) {
                    newrec->offset = offset + *size;
                    newrec->fileno = fileno;
                    newrec->size = rec->offset + rec->size - newrec->offset;

                    tmp = rec->size;
                    rec->size = offset - rec->offset;
                    SIZEREINDEX(map, rec, tmp);

                    newrec->next = rec->next;
                    newrec->prev = rec;
                    newrec->next_size = newrec->prev_size = NULL;
                    rec->next = newrec;
                    if (newrec->next) {
                        newrec->next->prev = newrec;
                    }
                    SIZEINDEX(map, newrec);

                    if ((lcrand(map->rand) <= map->index_mark)
                      /* randomly decided to index this record */
                      && (rbtree_ptr_ptr_insert(map->index, newrec, newrec) 
                          == RBTREE_OK)) {

                        newrec->flags = FREEREC_INDEXED;
                    } else {
                        newrec->flags = 0;
                    }
                    map->entries++;
                } else {
                    /* indicate out of memory error */
                    map->err = ENOMEM;
                    assert(freemap_invariant(map));
                    return 0;
                }
            }
        } else if ((rec->size == *size) || (!(options & FREEMAP_OPT_EXACT) 
            && (rec->size <= *size + map->append))) {

            /* rec gets consumed by allocation */
            *size = rec->size;

            SIZEUNINDEX(map, rec, rec->size);
            if (rec->prev) {
                rec->prev->next = rec->next;
            } else {
                map->first = rec->next;
            }

            if (rec->next) {
                rec->next->prev = rec->prev;
            }

            if ((rec->flags & FREEREC_INDEXED) 
              && (rbtree_ptr_ptr_remove(map->index, rec, &findkey) 
                != RBTREE_OK)) {
                assert(!CRASH);
                objalloc_free(map->alloc, rec);
                /* indicate failure (and just lost memory) */
                map->err = EINVAL;
                assert(freemap_invariant(map));
                return 0;
            }
            objalloc_free(map->alloc, rec);
            map->entries--;
        } else {
            tmp = rec->size;
            rec->offset += *size;
            rec->size -= *size;
            SIZEREINDEX(map, rec, tmp);
        }
        assert(freemap_invariant(map));
        return 1;
    }

    /* couldn't cover whole allocation with one used block.  rec points to the 
     * only free block that can be involved in the allocation, need to find
     * unused blocks that are involved */
    unused = map->unused;
    prev = NULL;
    while (unused && ((unused->fileno < fileno) || ((unused->fileno == fileno) 
      && (unused->offset + unused->size) <= offset))) {
        prev = unused;
        unused = unused->next;
    }

    if (unused && (unused->fileno == fileno) && (unused->offset <= offset) 
      && (unused->offset + unused->size >= offset + *size)) {
        /* allocate from unused */
        return freemap_malloc_unused(map, fileno, offset, size, options, 
          unused);
    }

    /* if we have a candidate from both, and they're both in the correct 
     * file ... */
    if (unused && rec && (unused->fileno == fileno) 
      && (unused->fileno == rec->fileno) 
      /* ... and they're contiguous with rec first (has to be first under our
       * unused-at-the-end assumption) */
      && (unused->offset == rec->offset + rec->size)) {
        unsigned int start = offset - rec->offset;
        unsigned int end = unused->size + rec->size - start - *size;

        /* start is the offset from the start of rec to the start of the
         * allocation.  end is the amount of space left in unused after
         * allocation */

        if (!end || (!(options & FREEMAP_OPT_EXACT) && (end <= map->append))) {
            /* unused is consumed by the allocation, remove it */

            /* calculate size of allocated block, note that we need to do this
             * before we fiddle with the rec record using start value below */
            *size = rec->size + unused->size - start;

            if (unused->prev) {
                unused->prev->next = unused->next;
            } else {
                map->unused = unused->next;
            }

            if (unused->next) {
                unused->next->prev = unused->prev;
            }
            assert(!unused->flags);
            objalloc_free(map->alloc, unused);
            map->entries--;
        } else {
            unused->offset = unused->offset + unused->size - end;
            unused->size = end;
        }

        if (!start) {
            /* rec is consumed by the allocation, remove it */
            assert(rec->offset + rec->size < offset + *size);

            SIZEUNINDEX(map, rec, rec->size);
            if (rec->prev) {
                rec->prev->next = rec->next;
            } else {
                map->first = rec->next;
            }

            if (rec->next) {
                rec->next->prev = rec->prev;
            }

            if ((rec->flags & FREEREC_INDEXED) 
              && (rbtree_ptr_ptr_remove(map->index, rec, &findkey) 
                != RBTREE_OK)) {

                assert(!CRASH);
                objalloc_free(map->alloc, rec);
                /* indicate failure (and just lost memory) */
                map->err = EINVAL;
                assert(freemap_invariant(map));
                return 0;
            }
            objalloc_free(map->alloc, rec);
            map->entries--;
        } else {
            assert(rec->offset < offset);
            tmp = rec->size;
            rec->size = start;
            SIZEREINDEX(map, rec, tmp);
        }

        assert(freemap_invariant(map));
        return 1;
    }

    /* get allocations from allocating function if necessary */
    while ((!unused || (fileno > unused->fileno)) && (fileno >= map->files)) {
        if ((unused = objalloc_malloc(map->alloc, sizeof(*unused))) 
          /* get a new allocation from the providing function */
          && (map->newfile(map->opaque, map->files, &unused->size))) {
            map->space += unused->size;
            unused->fileno = map->files++;
            unused->offset = 0;
           
            unused->prev_size = unused->next_size = NULL;
            unused->prev = prev;
            unused->next = NULL;
            if (prev) {
                assert(prev->next == NULL);
                prev->next = unused;
            } else {
                map->unused = unused;
            }
            unused->flags = 0;
            prev = unused;
            map->entries++;
        } else {
            /* indicate failure */
            map->err = ENOMEM;
            if (unused) {
                objalloc_free(map->alloc, unused);
            }
            assert(freemap_invariant(map));
            return 0;
        }
    }

    if (unused && (unused->fileno == fileno) && (unused->offset <= offset) 
      && (unused->offset + unused->size >= offset + *size)) {
        /* allocate from unused */
        return freemap_malloc_unused(map, fileno, offset, size, options, 
          unused);
    }

    return 0;
}

int freemap_malloc(struct freemap *map, unsigned int *fileno, 
  unsigned long int *offset, unsigned int *size, int options, ...) {
    va_list varargs;
    struct freerec *rec = NULL;
    unsigned int space,
                 tmp;
    unsigned int lsize = *size,         /* local copy of size */
                 index,
                 i;
    void *findkey;

    assert(freemap_invariant(map));

    va_start(varargs, options);
    if (options & FREEMAP_OPT_LOCATION) {
        /* deal with LOCATION in a different function, too hard otherwise */
        unsigned int fileno = (unsigned int) va_arg(varargs, unsigned int);
        unsigned long int offset 
          = (unsigned int) va_arg(varargs, unsigned long int);

        va_end(varargs);
        return freemap_malloc_location(map, fileno, offset, size, options);
    }
    va_end(varargs);

    if (!lsize) {
        *fileno = 0;
        *offset = 0;
        return 1;
    }

    index = bit_log2(lsize);
    switch (map->strategy) {
    case FREEMAP_STRATEGY_FIRST:
        /* first-fit along location-based list */
        for (rec = map->first; rec && (rec->size < lsize); rec = rec->next) ;
        break;

    case FREEMAP_STRATEGY_CLOSE:
        /* search, ascending up to larger sizeindex lists, for an entry to fit
         * this size */
        rec = NULL;
        i = index;
        do {
            for (rec = map->sizeindex[i]; rec && (rec->size < lsize); 
              rec = rec->next_size) ;
        } while (!rec && (++i < SIZELISTS));
        break;

    case FREEMAP_STRATEGY_BEST:
        /* search, ascending up to larger sizeindex lists, for the best entry in
         * this list (which will be the best fit overall) to fit this size */
        i = index;
        do {
            struct freerec *best;

            /* find first acceptable chunk */
            for (best = map->sizeindex[i]; best && (best->size < lsize); 
              best = best->next_size) ;

            /* have found first fit, now process the rest to find best fit */
            if (best) {
                for (rec = best->next_size; rec; rec = rec->next_size) {
                    if ((rec->size >= lsize) && (rec->size < best->size)) {
                        best = rec;
                    }
                }
            }

            rec = best;
        } while (!rec && (++i < SIZELISTS));
        break;

    case FREEMAP_STRATEGY_WORST:
        /* search, descending down from larger sizeindex lists, for the worst 
         * entry in this list (which will be the worst fit overall) to fit 
         * this size */
        i = SIZELISTS - 1;
        do {
            struct freerec *worst;

            /* find first acceptable chunk */
            for (worst = map->sizeindex[i]; worst && (worst->size < lsize); 
              worst = worst->next_size) ;

            /* have found first fit, now process the rest to find worst fit */
            if (worst) {
                for (rec = worst->next_size; rec; rec = rec->next_size) {
                    if ((rec->size >= lsize) && (rec->size > worst->size)) {
                        worst = rec;
                    }
                }
            }

            rec = worst;
        } while (!rec && (i-- + 1 > index));
        break;

    default: 
        assert("can't get here" && 0);
        break;
    }

    /* if we've selected a record it should be suitable... */
    assert(!rec || (rec->size >= lsize));

    /* ...but we'll check anyway */
    if (rec && (rec->size >= lsize)) {
        /* allocate from rec and return it */
        *fileno = rec->fileno;
        *offset = rec->offset;

        if ((rec->size == lsize) 
          || (!(options & FREEMAP_OPT_EXACT) 
          && (rec->size <= lsize + map->append))) {
            /* its a close fit, just allocate all of it */
            *size = rec->size;

            SIZEUNINDEX(map, rec, rec->size);
            if (rec->prev) {
                rec->prev->next = rec->next;
            } else {
                map->first = rec->next;
            }

            if (rec->next) {
                rec->next->prev = rec->prev;
            }

            if ((rec->flags & FREEREC_INDEXED) 
              && (rbtree_ptr_ptr_remove(map->index, rec, &findkey) 
                != RBTREE_OK)) {

                assert(!CRASH);
                objalloc_free(map->alloc, rec);
                /* indicate failure (and just lost memory) */
                map->err = EINVAL;
                return 0;
            }
            objalloc_free(map->alloc, rec);
            map->entries--;
        } else {
            /* need to fragment space */
            tmp = rec->size;
            rec->size -= lsize;
            rec->offset += lsize;
            SIZEREINDEX(map, rec, tmp);
        }
        assert(freemap_invariant(map));
        return 1;
    } else {
        /* check for unused portions that we can use on a first-fit basis */
        struct freerec *prev = NULL;

        rec = map->unused;

        while (rec) {
            prev = rec;
            if (rec->size >= lsize) {
                *fileno = rec->fileno;
                *offset = rec->offset;

                if ((rec->size == lsize) || (!(options & FREEMAP_OPT_EXACT) 
                    && (rec->size <= lsize + map->append))) {

                    /* its a close fit, just allocate all of it */
                    *size = rec->size;

                    if (rec->prev) {
                        rec->prev->next = rec->next;
                    } else {
                        map->unused = rec->next;
                    }

                    if (rec->next) {
                        rec->next->prev = rec->prev;
                    }

                    assert(!rec->flags);
                    objalloc_free(map->alloc, rec);

                    map->entries--;
                } else {
                    /* need to fragment space */
                    rec->size -= lsize;
                    rec->offset += lsize;
                }

                assert(freemap_invariant(map));
                return 1;
            }
            rec = rec->next;
        }

        /* get a new allocation from the providing function */
        if (map->newfile(map->opaque, map->files, &space)) {
            map->space += space;
            map->files++;

            if ((space == lsize)
              || (!(options & FREEMAP_OPT_EXACT) 
                && (space <= lsize + map->append)
                && (space >= lsize))) {

                /* whole file goes to new allocation */
                *fileno = map->files - 1;
                *offset = 0;
                *size = space;
                assert(freemap_invariant(map));
                return 1;
            } else {
                /* need to allocate an unused link.  Note that we put new unused
                 * links at the back of the unused link list so that space in
                 * earlier files gets used in preference to space in later
                 * files. */
                if ((rec = objalloc_malloc(map->alloc, sizeof(*rec)))) {
                    if (space < lsize) {
                        /* not enough space returned, save it and indicate
                         * failure */
                        rec->fileno = map->files - 1;
                        rec->offset = 0;
                        rec->size = space;
                    } else {
                        /* more than enough space returned, save extra and
                         * return success */
                        rec->fileno = *fileno = map->files - 1;
                        *offset = 0;
                        rec->offset = lsize;
                        rec->size = space - lsize;
                    }

                    rec->flags = 0;
                    rec->next = NULL;
                    rec->prev = prev;
                    rec->prev_size = rec->next_size = NULL;
                    if (prev) {
                        prev->next = rec;
                    } else {
                        map->unused = rec;
                    }
                    map->entries++;
                       
                    assert(freemap_invariant(map));
                    if (space < lsize) {
                        return 0;
                    } else {
                        return 1;
                    }
                } else {
                    /* need to indicate out of memory error */
                    map->err = ENOMEM;
                    return 0;
                }
            }
        }
    }

    return 0;
}

int freemap_free(struct freemap *map, unsigned int fileno, 
  unsigned long int offset, unsigned int size) {
    struct freerec key,
                   *rec,
                   *prev;
    void **find;
    void *findkey;
    unsigned int tmp;

    assert(freemap_invariant(map));

    if (!size) {
        return 1;
    }

    /* find position in freelist */
    key.fileno = fileno;
    key.offset = offset;
    key.size = 0;

    /* search the red-black tree for the location nearest specified 
     * location */
    if (rbtree_ptr_ptr_find_near(map->index, &key, &findkey, &find) 
        == RBTREE_OK) {

        rec = *find;
        prev = NULL;
    } else {
        rec = map->first;
        prev = NULL;
    }

    /* search forward for correct place */
    while (rec && (freerec_cmp(rec, &key) < 0)) {
        assert(rec->fileno != fileno 
          || (rec->offset + rec->size <= offset));
        prev = rec;
        rec = rec->next;
    }

    /* shouldn't have actually found a match */
    assert(!rec || freerec_cmp(rec, &key));

    /* try to coalesce it with the previous entry */
    if (prev && (prev->fileno == fileno) 
      && (prev->offset + prev->size == offset)) {
        tmp = prev->size;
        prev->size += size;
        SIZEREINDEX(map, prev, tmp);

        /* try to coalesce with next as well */
        if (rec && (rec->fileno == fileno) 
          && (offset + size == rec->offset)) {
            tmp = prev->size;
            prev->size += rec->size;
            SIZEREINDEX(map, prev, tmp);

            /* don't need rec any more, remove it */
            SIZEUNINDEX(map, rec, rec->size);
            prev->next = rec->next;
            if (rec->next) {
                rec->next->prev = prev;
            }

            if ((rec->flags & FREEREC_INDEXED) 
              && (rbtree_ptr_ptr_remove(map->index, rec, &findkey) 
                != RBTREE_OK)) {

                assert(!CRASH);
                objalloc_free(map->alloc, rec);
                /* indicate failure (and just lost memory) */
                map->err = EINVAL;
                return 0;
            }

            objalloc_free(map->alloc, rec);
            map->entries--;
        }
    /* try to coalesce it with next entry */
    } else if (rec && (rec->fileno == fileno)
      && (offset + size == rec->offset)) {
        tmp = rec->size;
        rec->size += size;
        rec->offset -= size;
        SIZEREINDEX(map, rec, tmp);
    /* need to insert new record for this entry */
    } else {
        struct freerec *newrec;

        if ((newrec = objalloc_malloc(map->alloc, sizeof(*newrec)))) {
            newrec->fileno = fileno;
            newrec->offset = offset;
            newrec->size = size;

            newrec->prev = prev;
            newrec->next = rec;
            newrec->next_size = newrec->prev_size = NULL;
            if (prev) {
                prev->next = newrec;
            } else {
                map->first = newrec;
            }
            if (rec) {
                rec->prev = newrec;
            }
            SIZEINDEX(map, newrec);
            map->entries++;

            if ((lcrand(map->rand) <= map->index_mark)
              /* randomly decided to index this record */
              && (rbtree_ptr_ptr_insert(map->index, newrec, newrec) 
                == RBTREE_OK)) {

                newrec->flags = FREEREC_INDEXED;
            } else {
                newrec->flags = 0;
            }
        } else {
            /* indicate out of memory error */
            map->err = ENOMEM;
            return 0;
        }
    }

    assert(freemap_invariant(map));
    return 1;
}

int freemap_waste(struct freemap *map, unsigned int fileno, 
  unsigned long int offset, unsigned int size) {
    map->waste += size;
    return 1;
}

unsigned int freemap_realloc(struct freemap *map, unsigned int fileno, 
  unsigned long int offset, unsigned int size, unsigned int additional, 
  int options, ...) {
    struct freerec key,
                   *rec,
                   *prev;
    void **find;
    void *findkey;
    unsigned int tmp;

    assert(freemap_invariant(map));

    /* find position in freelist */
    key.fileno = fileno;
    key.offset = offset + size;

    /* search the red-black tree for the location nearest specified 
     * location */
    if (rbtree_ptr_ptr_find_near(map->index, &key, &findkey, &find) 
        == RBTREE_OK) {

        rec = *find;
        prev = rec->prev;
    } else {
        rec = map->first;
        prev = NULL;
    }

    /* search forward for correct place */
    while (rec && freerec_cmp(rec, &key) < 0) {
        prev = rec;
        rec = rec->next;
    }

    if (rec && (rec->fileno == fileno) && (rec->offset == offset + size) 
      && (rec->size >= additional)) {

        if (rec->size == additional || (!(options & FREEMAP_OPT_EXACT) 
          && (rec->size <= additional + map->append))) {
            /* rec is consumed by allocation */
            tmp = rec->size;

            SIZEUNINDEX(map, rec, rec->size);
            assert(rec->prev == prev);
            if (prev) {
                prev->next = rec->next;
            } else {
                map->first = rec->next;
            }

            if (rec->next) {
                rec->next->prev = prev;
            }

            if ((rec->flags & FREEREC_INDEXED) 
              && (rbtree_ptr_ptr_remove(map->index, rec, &findkey) 
                != RBTREE_OK)) {

                assert(!CRASH);
                objalloc_free(map->alloc, rec);
                /* indicate failure (and just lost memory) */
                map->err = EINVAL;
                return 0;
            }

            map->entries--;
            assert(freemap_invariant(map));
            objalloc_free(map->alloc, rec);
            return tmp;
        } else {
            tmp = rec->size;
            rec->size -= additional;
            rec->offset += additional;
            SIZEREINDEX(map, rec, tmp);
            assert(freemap_invariant(map));
            return additional;
        }
    }

    /* attempt to realloc from unused */
    rec = map->unused;

    /* search forward for correct place */
    while (rec && freerec_cmp(rec, &key) < 0) {
        prev = rec;
        rec = rec->next;
    }

    if (rec && (rec->fileno == fileno) && (rec->offset == offset + size) 
      && (rec->size >= additional)) {

        if (rec->size == additional || (!(options & FREEMAP_OPT_EXACT) 
          && (rec->size <= additional + map->append))) {
            /* rec is consumed by allocation */
            tmp = rec->size;

            if (prev) {
                prev->next = rec->next;
            } else {
                map->unused = rec->next;
            }

            if (rec->next) {
                rec->next->prev = prev;
            }

            assert(!rec->flags);
            map->entries--;
            assert(freemap_invariant(map));
            objalloc_free(map->alloc, rec);
            return tmp;
        } else {
            rec->size -= additional;
            rec->offset += additional;
            assert(freemap_invariant(map));
            return additional;
        }
    }

    return 0;
}

int freemap_err(const struct freemap *map) {
    return map->err;
}

double freemap_utilisation(const struct freemap *map) {
    double free = 0.0;
    double unused = 0.0;
    const struct freerec *rec;

    for (rec = map->first; rec; rec = rec->next) {
        free += rec->size;
    }

    for (rec = map->unused; rec; rec = rec->next) {
        unused += rec->size;
    }

    if (map->space == unused) {
        return 1.0;
    } else {
        return (map->space - (unused + free)) / (map->space - unused);
    }
}

double freemap_space(const struct freemap *map) {
    double unused = 0.0;
    const struct freerec *rec;

    for (rec = map->unused; rec; rec = rec->next) {
        unused += rec->size;
    }

    return map->space - unused;
}

void freemap_print(const struct freemap *map, FILE *output) {
    struct freerec *rec;

    for (rec = map->first; rec; rec = rec->next) {
        fprintf(output, "(%u %lu) size %u %s\n", rec->fileno, rec->offset, 
          rec->size, rec->flags & FREEREC_INDEXED ? "(indexed)" : "");
    }

    for (rec = map->unused; rec; rec = rec->next) {
        fprintf(output, "(%u %lu) size %u (unused)\n", rec->fileno, rec->offset,
          rec->size);
    }

    return;
}

unsigned int freemap_entries(const struct freemap *fm) {
    return fm->entries;
}

double freemap_wasted(const struct freemap *fm) {
    return fm->waste;
}

unsigned int freemap_append(const struct freemap *fm) {
    return fm->append;
}

unsigned int freemap_indexed_entries(const struct freemap *fm) {
    return rbtree_size(fm->index);
}

enum freemap_strategies freemap_strategy(const struct freemap *fm) {
    return fm->strategy;
}

unsigned int freelist_size(const struct freerec *rec) {
    unsigned int i = 0;

    while (rec) {
        rec = rec->next;
        i++;
    }

    return i;
}

unsigned int freelist_size_size(const struct freerec *rec) {
    unsigned int i = 0;

    while (rec) {
        rec = rec->next_size;
        i++;
    }

    return i;
}

void freemap_print_profile(const struct freemap *map, FILE *output) {
    unsigned int i;
    struct freerec *rec;
    double total = freemap_space(map);

    for (i = 0; i < SIZELISTS; i++) {
        unsigned int count,
                     sum;

        count = sizelist_size(map->sizeindex[i], &sum);
        fprintf(output, "%u - %u: %u entries %u bytes, %f%%\n", 
          BIT_POW2(i), BIT_POW2(i) + (BIT_POW2(i) - 1), count, sum, 
          sum * 100.0 / total);
    }

    for (rec = map->unused; rec; rec = rec->next) {
        fprintf(output, "unused: %u %lu %u\n", rec->fileno, rec->offset, 
          rec->size);
    }
}

