/* iobtree.c implements a b+tree that does IO internally.  See
 * iobtree.h for more details.  For an introduction to b-trees see 'The
 * Ubiquitous B-Tree' by Douglas Comer.
 *
 * See btbucket.h for details of the internal storage format of the btree 
 * pages.
 *
 * ideas for improvement:
 *     - write space-guarantee remove/realloc
 *     - uniform binary search (see knuth vol 3 pg 416 under shar and sigmod
 *       record lomet paper 'evolution of btree buckets' or something like)
 *     - prefix b-tree
 *         - improved prefix b-tree where we examine siblings to get better
 *           idea of range (note that directory nodes will need sibling
 *           pointers for this?)
 *     - split-avoidance techniques? (redistribution, although i note that 
 *       it probably conflicts with prefix b-tree  compression))
 *     - bucket formats:
 *         - frequency ordered?
 *         - micro-indexed?
 *         - store 4 chars of prefix of strings and use for binary search
 *           (note that this really needs prefix b-tree to work well) and is
 *           probably better done in native endian-ness
 *         - break up entries into different vectors
 *
 * written nml 2004-01-06
 *
 */

#include "firstinclude.h"

#include "iobtree.h"

#include "_btbucket.h"
#include "_mem.h"

#include "bit.h"
#include "bucket.h"
#include "btbucket.h"
#include "freemap.h"
#include "fdset.h"
#include "def.h"
#include "str.h"
#include "zstdint.h"
#include "zvalgrind.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

/* we need a sentinal pointer to indicate that the pointer points at a leaf, and
 * we can't use NULL, since that means unexplored.  So we use a pointer to the
 * btree itself (since its invariant for the life of the btree, which most other
 * options aren't) to indicate this */
#define LEAF_PTR(btree) ((struct page *) btree)

/* FIXME: - safe interface
          - bucket strategy 2 for internal nodes 
          - testing of new fns, errors on old 
          - fix bucket_search issue 
 */

struct page_header {
    unsigned int fileno;       /* file that this page lives at */
    unsigned long int offset;  /* offset that this page lives at */
    unsigned int dirsize;      /* number of spaces in directory.  Is always 
                                * greater than or equal to number of entries 
                                * in bucket, unless bucket is a leaf in which
                                * case its always 0. */
    struct page **directory;   /* pointers to pages below this page.  Each 
                                * pointer will be NULL if unexplored, 
                                * will point to LEAF_PTR if descendant is 
                                * a leaf */
    struct page *parent;       /* pointer to parent page, NULL if this is the 
                                * root */
    uint8_t dirty;             /* whether this page is dirty */
};

/* structure to hold an internal node in memory (split into two bits to allow
 * easy exact allocation) */
struct page {
    struct page_header h;      /* memory only information */
    char mem[1];               /* bucket image in memory (struct hack) */
};

struct iobtree {
    struct fdset *fd;          /* file set that the btree is resident in */
    unsigned int fdset;        /* number of the type in the fdset */
    struct freemap *freemap;   /* map of free space */

    int leaf_strategy;         /* leaf node format, see bucket.h */
    int node_strategy;         /* internal node format, see bucket.h */
    unsigned int pagesize;     /* size of a page */

    unsigned int entries;      /* number of entries in the btree */

    struct page *root;         /* pointer to root node in array */
    struct page *leaf;         /* pointer to stored leaf */
    struct page *tmp;          /* pointer to temporary leaf space */
    unsigned int timestamp;    /* change version of the btree */
    unsigned int levels;       /* number of levels in the btree */

    /* address of the right-most leaf page, which we need to keep track of 
     * to bulk-append to the btree */
    struct {
        unsigned int fileno;
        unsigned long int offset;
    } right;
};

enum page_iter_filter {
    FILTER_NONE = 0,      /* return all pages */
    FILTER_NODES = 1,     /* return all non-leaf pages */
    FILTER_INMEM = 2      /* return all non-leaf pages that are in memory */
};

/* structure to support iterating over the pages in the btree (needs a stack of
 * visited pages).  Note that despite having parent pointers, we still need a
 * stack to iterate as we need to keep track of where we are within each 
 * bucket, since each node has multiple entries. */
struct page_iter {
    struct {
        struct page *curr;   /* page at this level */
        unsigned int term;   /* number of terms iterated over */
        unsigned int state;  /* iteration state */
    } *state;
    unsigned int size;
    unsigned int capacity;
};

/* internal function to iterate over pages in the btree.  Returns 0 on success
 * and -errno otherwise (1 to finish). */
static enum iobtree_ret page_iter_next(struct iobtree *btree, 
  struct page_iter *iter, enum page_iter_filter filter, 
  struct page **retpage);

/* internal function to print out an understandable representation of the btree
 * below the given page */
int iobtree_print_page(struct iobtree *btree, struct page *page, 
  unsigned int level);

/* internal function to create a new btree page and initialise things 
 * sensibly */
static struct page *new_page(unsigned int pagesize, struct page *parent) {
    struct page *page;

    if ((page = malloc(sizeof(struct page_header) + pagesize))) {
        page->h.directory = NULL;
        page->h.dirsize = 0;
        page->h.fileno = -1;
        page->h.offset = -1;
        page->h.dirty = 0;
        page->h.parent = parent;
        return page;
    } else {
        return NULL;
    }
}

/* internal function to read a btree page from disk, creating a new page to hold
 * it if necessary */
static struct page *read_page(struct iobtree *btree, unsigned int fileno, 
  unsigned long int offset, struct page *givenpage, struct page *parent, 
  enum iobtree_ret *err) {
    int fd,
        ret;
    off_t prev;
    struct page *page = givenpage;

    if (!page) {
        if ((page = new_page(btree->pagesize, NULL))) {
            /* do nothing */
        } else {
            if (err) {
                *err = IOBTREE_ENOMEM;
            }
            return NULL;
        }
    }

    assert(!page->h.dirty);
    if ((fd = fdset_pin(btree->fd, btree->fdset, fileno, 0, SEEK_CUR)) >= 0) {

        /* XXX: hack to restore fd to previous location after paging in, so 
         * that other algorithms don't have to worry about the btree seeking 
         * fds around */
        errno = 0;
        if (((prev = lseek(fd, 0, SEEK_CUR)) != (off_t) -1)
          && (lseek(fd, offset, SEEK_SET) == (off_t) offset)

          && (read(fd, page->mem, btree->pagesize) == (ssize_t) btree->pagesize)
          && (lseek(fd, prev, SEEK_SET) == prev)) {
            if ((ret = fdset_unpin(btree->fd, btree->fdset, fileno, fd)) 
              == FDSET_OK) {
                page->h.fileno = fileno;
                page->h.offset = offset;
                page->h.dirty = 0;
                page->h.parent = parent;

                if (!BTBUCKET_LEAF(page->mem, btree->pagesize)) {
                    unsigned int entries;

                    entries = bucket_entries(BTBUCKET_BUCKET(page->mem), 
                        btree->pagesize, btree->leaf_strategy);

                    if (page->h.dirsize < entries) {
                        void *ptr;

                        if ((ptr = realloc(page->h.directory, 
                              sizeof(*page->h.directory) * entries))) {
                            page->h.directory = ptr;
                            page->h.dirsize = entries;
                        } else {
                            if (!givenpage) {
                                free(page);
                            }
                            *err = IOBTREE_ENOMEM;
                            return NULL;
                        }
                    }

                    BIT_ARRAY_NULL(page->h.directory, page->h.dirsize);
                }

                return page;
            } else {
                errno = -ret;
            }
        }
    } else {
        errno = -fd;
    }

    if (err) {
        switch (errno) {
        default: *err = IOBTREE_ERR;
        case EINTR: *err = IOBTREE_EINTR;
        case EAGAIN: *err = IOBTREE_EAGAIN;
        case EIO: *err = IOBTREE_EIO;
        }
    }

    return NULL;
}

/* internal function to flush a page to disk */
static int write_page(struct iobtree *btree, struct page *page) {
    int fd,
        ret;
    off_t prev;

    if ((fd = fdset_pin(btree->fd, btree->fdset, page->h.fileno, 0, SEEK_CUR)) 
        >= 0) {

        /* XXX: hack to restore fd to previous location after paging in, so that
         * other algorithms don't have to worry about the btree seeking fds
         * around */
        errno = 0;
        if (((prev = lseek(fd, 0, SEEK_CUR)) != (off_t) -1)
          && (lseek(fd, page->h.offset, SEEK_SET) == (off_t) page->h.offset)
          && (write(fd, page->mem, btree->pagesize) 
            == (ssize_t) btree->pagesize)
          && (lseek(fd, prev, SEEK_SET) != (off_t) -1)) {

            if ((ret = fdset_unpin(btree->fd, btree->fdset, page->h.fileno, fd))
                == FDSET_OK) {

                page->h.dirty = 0;
                return IOBTREE_OK;
            } else {
                errno = -ret;
            }
        }
    } else {
        errno = -fd;
    }

    switch (errno) {
    default: return IOBTREE_ERR;
    case EINTR: return IOBTREE_EINTR;
    case EAGAIN: return IOBTREE_EAGAIN;
    case EIO: return IOBTREE_EIO;
    }
}

/* internal function to check all invariants of the btree */
static enum iobtree_ret iobtree_invariant(struct iobtree *btree) {
    struct page_iter iter;
    struct page *page;
    enum iobtree_ret iterret,
                     ret;
    const char *term,
               *pterm;
    unsigned int termlen,
                 ptermlen,
                 datalen,
                 index,
                 entries,
                 i,
                 levels,
                 side;
    void *data;

    if (!DEAR_DEBUG) {
        return IOBTREE_OK;
    }

    /* must always have three nodes available, and the root node can't have a
     * parent */
    if (!btree->root || !btree->leaf || !btree->tmp || btree->root->h.parent) {
        if (CRASH) {
            iobtree_print_page(btree, btree->root, 0);
        }
        assert(!CRASH);
        return IOBTREE_ERR;
    }

    /* leaf nodes don't have parent pointers, or directories */
    if (btree->leaf->h.parent || btree->leaf->h.directory 
      || btree->leaf->h.dirsize) {
        if (CRASH) {
            iobtree_print_page(btree, btree->root, 0);
        }
        assert(!CRASH);
        return IOBTREE_ERR;
    }

    /* right-most node must be a leaf, and must be right-most */
    page = btree->leaf;
    if ((btree->leaf->h.fileno == btree->right.fileno 
        && btree->leaf->h.offset == btree->right.offset) 
      || (page = read_page(btree, btree->right.fileno, btree->right.offset, 
        NULL, NULL, &ret))) {

        unsigned int fileno;
        unsigned long int offset;

        if (!BTBUCKET_LEAF(page->mem, btree->pagesize)) {
            if (CRASH) {
                iobtree_print_page(btree, btree->root, 0);
            }
            assert(!CRASH);
            return IOBTREE_ERR;
        }

        btbucket_sibling(page->mem, btree->pagesize, &fileno, &offset);
        if (fileno != btree->right.fileno || offset != btree->right.offset) {
            if (CRASH) {
                iobtree_print_page(btree, btree->root, 0);
            }
            assert(!CRASH);
            return IOBTREE_ERR;
        }

        if (page != btree->leaf) {
            free(page);
        }
    }

    /* number of levels should be correct.  try to iterate down left side first,
     * then right side if that fails (we *must* have iterated down one of the
     * two when we loaded, and all will be in memory if we created) */
    levels = 1;
    side = 0;
    for (page = btree->root; 
      (page != LEAF_PTR(btree)) 
        && !BTBUCKET_LEAF(page->mem, btree->pagesize);) {

        /* watch for infinite looping over our dummy node */
        assert(page && page != LEAF_PTR(btree));

        if (side) {
            index = bucket_entries(BTBUCKET_BUCKET(page->mem), 
              BTBUCKET_SIZE(page->mem, btree->pagesize), btree->node_strategy) 
                - 1;
        } else {
            index = 0;
        }

        assert(page->h.directory);
        assert(page->h.dirsize > index);
        if (page->h.directory[index]) {
            assert(page != page->h.directory[index]);
            page = page->h.directory[index];
            levels++;
        } else {
            assert(!side);

            /* failed to iterate down left side, try again down right side */
            side = 1;
            levels = 1;
            page = btree->root;
        }
    }

    if (levels != btree->levels) {
        assert(!CRASH);
        return IOBTREE_ERR;
    }

    /* all other internal nodes should have parent pointers */
    if ((iter.state = malloc(sizeof(*iter.state)))) {
        iter.size = 1;
        iter.capacity = 1;
        iter.state[0].curr = btree->root;
        iter.state[0].state = 0;
        iter.state[0].term = 0;
        while ((iterret = page_iter_next(btree, &iter, FILTER_INMEM, &page)) 
          == IOBTREE_OK) {
            assert(!BTBUCKET_LEAF(page->mem, btree->pagesize));

            /* check directory and parent */
            if (!page->h.directory 
              || (page != btree->root && !page->h.parent)) {
                if (CRASH) {
                    iobtree_print_page(btree, btree->root, 0);
                }
                assert(!CRASH);
                return IOBTREE_ERR;
            }

            /* check that parent doesn't have excess pointers set in the
             * directory, and that adjacent directory entries aren't equal 
             * (these came up for some reason) */
            entries = bucket_entries(BTBUCKET_BUCKET(page->mem), 
                  BTBUCKET_SIZE(page->mem, btree->pagesize), 
                  btree->node_strategy);

            /* find first non-NULL pointer */
            for (i = 0; page->h.dirsize && i < entries && !page->h.directory[i];
              i++) ;

            if (page->h.dirsize && page->h.directory[i++] != LEAF_PTR(btree)) {
                /* should be no leaf pointers in this directory then */
                for (; i < entries; i++) {
                    /* adjacent pointer check */
                    if ((page->h.directory[i] 
                        && (page->h.directory[i] == page->h.directory[i - 1])) 
                      || (page->h.directory[i] == LEAF_PTR(btree))) {
                        if (CRASH) {
                            iobtree_print_page(btree, btree->root, 0);
                        }
                        assert(!CRASH);
                        return IOBTREE_ERR;
                    }
                }
            } else {
                /* should be all leaf pointers */
                for (; i < entries; i++) {
                    if (page->h.directory[i]
                      && (page->h.directory[i] != LEAF_PTR(btree))) {
                        if (CRASH) {
                            iobtree_print_page(btree, btree->root, 0);
                        }
                        assert(!CRASH);
                        return IOBTREE_ERR;
                    }
                }
            }

            for (; i < page->h.dirsize; i++) {
                /* excess pointer check */
                if (page->h.directory[i]) {
                    if (CRASH) {
                        iobtree_print_page(btree, btree->root, 0);
                    }
                    assert(!CRASH);
                    return IOBTREE_ERR;
                }
            }

            /* check that parent has current node listed as a child */
            if (page->h.parent 
              /* get first term from bucket to search parent with */
              && (term = bucket_term_at(BTBUCKET_BUCKET(page->mem), 
                  BTBUCKET_SIZE(page->mem, btree->pagesize), 
                  btree->node_strategy, 0, &termlen, &data, &datalen))) {

                /* search parent */
                bucket_search(BTBUCKET_BUCKET(page->h.parent->mem), 
                  BTBUCKET_SIZE(page->h.parent->mem, btree->pagesize), 
                  btree->node_strategy, term, termlen, &datalen, &index);

                /* ensure the pointer we got back leads to the current node */
                if (page->h.parent->h.directory[index] != page) {
                    if (CRASH) {
                        iobtree_print_page(btree, btree->root, 0);
                    }
                    assert(!CRASH);
                    return IOBTREE_ERR;
                }

                /* ensure that the term in the parent node is not larger than
                 * the first term in this node (although the fact that the
                 * previous test passed strongly suggests that this is true) */
                if (!(pterm 
                    = bucket_term_at(BTBUCKET_BUCKET(page->h.parent->mem), 
                        BTBUCKET_SIZE(page->h.parent->mem, btree->pagesize), 
                        btree->node_strategy, index, &ptermlen, &data, 
                        &datalen))
                  || (str_nncmp(pterm, ptermlen, term, termlen) < 0)) {
                    if (CRASH) {
                        iobtree_print_page(btree, btree->root, 0);
                    }
                    assert(!CRASH);
                    return IOBTREE_ERR;
                }

                /* ensure that the first term in this node is larger than the
                 * parent entry for the left sibling, if it exists */
                if (index) {
                    if (!(pterm 
                        = bucket_term_at(BTBUCKET_BUCKET(page->h.parent->mem), 
                          BTBUCKET_SIZE(page->h.parent->mem, btree->pagesize),
                          btree->node_strategy, index - 1, &ptermlen, &data,
                          &datalen))
                      || (str_nncmp(term, termlen, pterm, ptermlen) <= 0)) {
                        if (CRASH) {
                            iobtree_print_page(btree, btree->root, 0);
                        }
                        assert(!CRASH);
                        return IOBTREE_ERR;
                    }
                }

                /* ensure that the last term in this node is smaller than the
                 * parent entry for right sibling, if it exists */
                entries = bucket_entries(BTBUCKET_BUCKET(page->h.parent->mem), 
                    BTBUCKET_SIZE(page->h.parent->mem, btree->pagesize),
                    btree->node_strategy);  /* get entries in parent */
                if (index + 1 < entries) {
                    entries = bucket_entries(BTBUCKET_BUCKET(page->mem), 
                        BTBUCKET_SIZE(page->mem, btree->pagesize),
                        btree->node_strategy); /* get entries in child */

                    /* get last entry from child */
                    term = bucket_term_at(BTBUCKET_BUCKET(page->mem), 
                        BTBUCKET_SIZE(page->mem, btree->pagesize), 
                        btree->node_strategy, entries - 1, &termlen, &data, 
                        &datalen);
                    assert(term);

                    if (!(pterm 
                        = bucket_term_at(BTBUCKET_BUCKET(page->h.parent->mem), 
                          BTBUCKET_SIZE(page->h.parent->mem, btree->pagesize),
                          btree->node_strategy, index + 1, &ptermlen, 
                          &data, &datalen))
                      || (str_nncmp(term, termlen, pterm, ptermlen) >= 0)) {
                        if (CRASH) {
                            iobtree_print_page(btree, btree->root, 0);
                        }
                        assert(!CRASH);
                        return IOBTREE_ERR;
                    }
                }
            }
        }
        free(iter.state);

        if (iterret != IOBTREE_ITER_FINISH) {
            return iterret;
        }
    }

    return IOBTREE_OK;
}

static enum iobtree_ret page_iter_next(struct iobtree *btree, 
  struct page_iter *iter, enum page_iter_filter filter, 
  struct page **retpage) {
    struct page *page = NULL,
                *nextpage;
    const char *term;
    void *addr;
    unsigned int termlen, 
                 len,
                 fileno;
    unsigned long int offset;
    enum iobtree_ret ret;

    while (iter->size 
      && (page = iter->state[iter->size - 1].curr)
      && (term = bucket_next_term(BTBUCKET_BUCKET(page->mem), 
          BTBUCKET_SIZE(page->mem, btree->pagesize), btree->node_strategy, 
          &iter->state[iter->size - 1].state, &termlen, &addr, &len))) {
        iter->state[iter->size - 1].term++;

        if (iter->size >= iter->capacity) {
            void *ret = realloc(iter->state, 
              sizeof(*iter->state) * iter->capacity * 2);

            if (!ret) {
                return IOBTREE_ENOMEM;
            }

            iter->state = ret;
            iter->capacity *= 2;
        }

        if (BTBUCKET_LEAF(page->mem, btree->pagesize)) {
            iter->size--;
            if (filter == FILTER_NONE) {
                *retpage = page;
                return IOBTREE_OK;
            }
        } else {
            nextpage = page->h.directory[iter->state[iter->size - 1].term - 1];
            if (nextpage) {
                if (nextpage == LEAF_PTR(btree)) {
                    /* next is a leaf, page it in */
                    if (filter == FILTER_NONE) {
                        /* may need to page out current leaf */
                        if (btree->leaf->h.dirty
                          && ((ret = write_page(btree, btree->leaf))
                            != IOBTREE_OK)) {

                            return ret;
                        }

                        BTBUCKET_ENTRY(addr, &fileno, &offset);
                        if (((btree->leaf->h.fileno == fileno) 
                            && (btree->leaf->h.offset == offset)) 
                          || read_page(btree, fileno, offset, 
                              btree->leaf, NULL, &ret)) {
                            *retpage = btree->leaf;
                            return IOBTREE_OK;
                        } else {
                            return ret;
                        }
                    } else {
                        /* we're ignoring leaves, and we know that all pages
                         * under this one are leaves, so we break the while 
                         * loop to immediately skip to returning this page if
                         * appropriate */
                        break;
                    }
                } else {
                    /* next is a node, already in memory */
                    iter->state[iter->size].curr = nextpage;
                    iter->state[iter->size].state = 0;
                    iter->state[iter->size++].term = 0;
                }
            } else if (filter != FILTER_INMEM) {
                /* don't know what's under this page, find out */

                /* may need to page out current leaf */
                if (btree->leaf->h.dirty
                  && ((ret = write_page(btree, btree->leaf)) != IOBTREE_OK)) {
                    return ret;
                }

                assert(addr);
                BTBUCKET_ENTRY(addr, &fileno, &offset);
                if (((btree->leaf->h.fileno == fileno) 
                    && (btree->leaf->h.offset == offset)) 
                  || read_page(btree, fileno, offset, btree->leaf, NULL, 
                      &ret)) {

                    if (BTBUCKET_LEAF(btree->leaf->mem, btree->pagesize)) {
                        page->h.directory[iter->state[iter->size - 1].term - 1] 
                          = LEAF_PTR(btree);
                        if (filter == FILTER_NONE) {
                            *retpage = btree->leaf;
                            return IOBTREE_OK;
                        } else {
                            /* we're ignoring leaves, and we know that all 
                             * pages under this one are leaves, so we break 
                             * the while loop to immediately skip to returning 
                             * this page if appropriate */
                            break;
                        }
                    } else {
                        page->h.directory[iter->state[iter->size - 1].term - 1] 
                          = btree->leaf;
                        iter->state[iter->size].curr = btree->leaf;
                        iter->state[iter->size].curr->h.parent = page;
                        iter->state[iter->size].state = 0;
                        iter->state[iter->size++].term = 0;

                        /* have used leaf as something else, need to replace it
                         * with a new node */
                        if (!(btree->leaf = new_page(btree->pagesize, NULL))) {
                            return IOBTREE_ENOMEM;
                        }
                    }
                } else {
                    return ret;
                }
            }
        }
    }
 
    if (iter->size && page) {
        iter->size--;
        if (!BTBUCKET_LEAF(page->mem, btree->pagesize) 
          || (filter == FILTER_NONE)) {
            *retpage = page;
            return IOBTREE_OK;
        }
    }

    return IOBTREE_ITER_FINISH;
}

int iobtree_clear(struct iobtree *btree) {
    unsigned int tmp;
    enum iobtree_ret iterret;

    if (btree->root) {
        struct page_iter iter;
        struct page *page;

        /* need to free the pages in the btree by iterating over them */

        if (!(iter.state = malloc(sizeof(*iter.state)))) {
            return 0;
        }

        iter.size = 1;
        iter.capacity = 1;
        iter.state[0].curr = btree->root;
        iter.state[0].state = 0;
        iter.state[0].term = 0;

        while ((iterret = page_iter_next(btree, &iter, FILTER_NONE, &page)) 
          == IOBTREE_OK) {
            freemap_free(btree->freemap, page->h.fileno, page->h.offset, 
              btree->pagesize);

            /* only free internal nodes */
            if (!BTBUCKET_LEAF(page->mem, btree->pagesize)) {
                free(page);
            }
        }
        free(iter.state);

        if (iterret != IOBTREE_ITER_FINISH) {
            assert(!CRASH);
            return 0;
        }
    }

    btree->entries = 0;

    /* allocate root node */
    if ((btree->root = new_page(btree->pagesize, NULL))
      && freemap_malloc(btree->freemap, &btree->root->h.fileno, 
          &btree->root->h.offset, &btree->pagesize, FREEMAP_OPT_EXACT)) {
        btree->root->h.dirty = 1;
    } else {
        assert(!CRASH);
        if (btree->root) {
            free(btree->root);
            btree->root = NULL;
        }
        return 0;
    }

    /* initialise it */
    tmp = 0;
    btbucket_new(btree->root->mem, btree->pagesize, btree->root->h.fileno, 
      btree->root->h.offset, 1, NULL, &tmp);
    bucket_new(BTBUCKET_BUCKET(btree->root->mem), 
      BTBUCKET_SIZE(btree->root->mem, btree->pagesize), btree->leaf_strategy);
    btree->leaf = btree->root;
    btree->right.fileno = btree->root->h.fileno;
    btree->right.offset = btree->root->h.offset;

    assert(iobtree_invariant(btree) == IOBTREE_OK);
    return 1;
}

struct iobtree *iobtree_new(unsigned int pagesize, int leaf_strategy, 
  int node_strategy, struct freemap *freemap, struct fdset *fds, 
  unsigned int fdset_type) {
    struct iobtree *btree = NULL;

    if ((pagesize <= 65535) && (btree = malloc(sizeof(*btree))) 
      && (btree->tmp = new_page(pagesize, NULL))) {
        btree->fd = fds;
        btree->fdset = fdset_type;
        btree->freemap = freemap;
        btree->leaf_strategy = leaf_strategy;
        btree->node_strategy = node_strategy;
        btree->pagesize = pagesize;
        btree->timestamp = 0;
        btree->levels = 1;

        btree->root = btree->leaf = NULL;

        if (iobtree_clear(btree)) {
            /* succeed, check invariant (we use it outside of an assert to
             * ensure that its used at least once when NDEBUG is on) */
            if (iobtree_invariant(btree) != IOBTREE_OK) {
                assert(iobtree_invariant(btree) == IOBTREE_OK);
                iobtree_delete(btree);
                return NULL;
            }
        } else {
            free(btree->tmp);
            free(btree);
            btree = NULL;
        }
    } else if (btree) {
        free(btree);
        btree = NULL;
    }
    
    return btree;
}

static struct iobtree *iobtree_load_int(unsigned int pagesize, 
  int leaf_strategy, int node_strategy, struct freemap *freemap, 
  struct fdset *fds, unsigned int fdset_type, unsigned int root_fileno, 
  unsigned long int root_offset, unsigned long int entries) {
    struct iobtree *btree = NULL;

    if ((pagesize <= 65535) && (btree = malloc(sizeof(*btree)))
      && ((btree->fd = fds), (btree->fdset = fdset_type), 
        (btree->leaf_strategy = leaf_strategy), 
        (btree->node_strategy = node_strategy), 
        (btree->pagesize = pagesize))
      && (btree->root 
        = read_page(btree, root_fileno, root_offset, NULL, NULL, NULL))
      && (btree->tmp = new_page(pagesize, NULL))) {

        if (!BTBUCKET_LEAF(btree->root->mem, btree->pagesize)) {
            if ((btree->leaf = new_page(pagesize, NULL))) {
                /* succeeded, do nothing */
            } else {
                free(btree->root);
                free(btree->tmp);
                free(btree);
                return NULL;
            }
        } else {
            btree->leaf = btree->root;
        }
        btree->freemap = freemap;
        btree->entries = entries;
        btree->timestamp = 0;
    } else if (btree) {
        if (btree->root) {
            if (btree->leaf) {
                free(btree->leaf);
            }
            free(btree->root);
        }
        free(btree);
        btree = NULL;
    }
    
    return btree;
}

struct iobtree *iobtree_load_quick(unsigned int pagesize, int leaf_strategy,
  int node_strategy, struct freemap *freemap, struct fdset *fds, 
  unsigned int fdset_type, unsigned int root_fileno, 
  unsigned long int root_offset, unsigned long int entries) {
    struct iobtree *btree;
    struct page *curr;
    void *addr = NULL;
    enum iobtree_ret ret;
    unsigned int termlen,
                 len;

    btree = iobtree_load_int(pagesize, leaf_strategy, node_strategy, freemap, 
        fds, fdset_type, root_fileno, root_offset, entries);

    if (btree) {
        /* calculate the number of levels and find the right-most leaf by
         * traversing down the right side of the tree */
        btree->levels = 1;
        for (curr = btree->root; !BTBUCKET_LEAF(curr->mem, btree->pagesize); ) {
            /* watch for infinite looping over our dummy node */
            assert(curr && curr != LEAF_PTR(btree));

            entries = bucket_entries(BTBUCKET_BUCKET(curr->mem), 
              BTBUCKET_SIZE(curr->mem, btree->pagesize), btree->node_strategy);

            assert(entries);
            assert(curr->h.directory);
            assert(curr->h.dirsize >= entries);
            if (curr->h.directory[entries - 1] 
              && curr->h.directory[entries - 1] != LEAF_PTR(btree)) {
                /* internal node below */
                assert(curr != curr->h.directory[entries - 1]);
                curr = curr->h.directory[entries - 1];
                continue;
            }

            /* get addr */
            bucket_term_at(BTBUCKET_BUCKET(curr->mem), 
              BTBUCKET_SIZE(curr->mem, btree->pagesize), btree->node_strategy,
              entries - 1, &termlen, &addr, &len);
            BTBUCKET_ENTRY(addr, &btree->right.fileno, &btree->right.offset);

            assert(!btree->leaf->h.dirty);

            if (read_page(btree, btree->right.fileno, btree->right.offset, 
                btree->leaf, NULL, &ret)) {

                /* either it is a leaf node, or we didn't know */
                if (BTBUCKET_LEAF(btree->leaf->mem, btree->pagesize)) {
                    curr->h.directory[entries - 1] = LEAF_PTR(btree);
                    curr = btree->leaf;
                } else {
                    /* ensure parent is set */
                    curr->h.directory[entries - 1] = btree->leaf;
                    btree->leaf->h.parent = curr;
                    curr = btree->leaf;
                    /* have read a non-leaf into leaf page, replace it */
                    if (!(btree->leaf
                      = new_page(btree->pagesize, NULL))) {
                        iobtree_delete(btree);
                        return NULL;
                    }
                }
            } else {
                assert(ret != IOBTREE_OK);
                assert(!CRASH);
                return NULL;
            }
            btree->levels++;
        }

        /* curr now points to the right-most leaf in the tree, and levels should
         * be correct */
        assert(BTBUCKET_LEAF(curr->mem, btree->pagesize));
        btree->right.fileno = curr->h.fileno;
        btree->right.offset = curr->h.offset;
        assert(iobtree_invariant(btree) == IOBTREE_OK);
    }

    return btree;
}

struct iobtree *iobtree_load(unsigned int pagesize, int leaf_strategy,
  int node_strategy, struct freemap *freemap, struct fdset *fds, 
  unsigned int fdset_type, unsigned int root_fileno, 
  unsigned long int root_offset) {
    struct iobtree *btree;
    enum iobtree_ret iterret;

    if ((btree = iobtree_load_int(pagesize, leaf_strategy, node_strategy, 
        freemap, fds, fdset_type, root_fileno, root_offset, 0))) {

        /* have to iterate over pages to calculate stats and remove pages
         * from the freemap */
        struct page_iter iter;
        struct page *page;
        unsigned int fileno;
        unsigned long int offset;

        /* need to allocate pages from freemap */
        if (!(iter.state = malloc(sizeof(*iter.state)))) {
            return 0;
        }

        iter.size = 1;
        iter.capacity = 1;
        iter.state[0].curr = btree->root;
        iter.state[0].state = 0;
        iter.state[0].term = 0;

        while ((iterret = page_iter_next(btree, &iter, FILTER_NONE, &page)) 
          == IOBTREE_OK) {
            if (!freemap_malloc(btree->freemap, &fileno, &offset, 
              &btree->pagesize, FREEMAP_OPT_EXACT | FREEMAP_OPT_LOCATION, 
              page->h.fileno, page->h.offset)) {
                assert(!CRASH);
                iobtree_delete(btree);
                if (iter.state) {
                    free(iter.state);
                }
                return NULL;
            }
            if (BTBUCKET_LEAF(page->mem, btree->pagesize)) {
                btree->entries += bucket_entries(BTBUCKET_BUCKET(page->mem),
                    BTBUCKET_SIZE(page->mem, btree->pagesize), 
                    btree->leaf_strategy);

                /* end page should be the right-most in the tree (note that we
                 * can't put this outside the page_iter loop because the loop
                 * goes over non-leaf pages too */
                btree->right.fileno = page->h.fileno;
                btree->right.offset = page->h.offset;
            }
        }

        free(iter.state);

        /* calculate the number of levels, by traversing down the left side of
         * the b-tree, where we are guaranteed to have just explored */
        btree->levels = 1;
        if (!BTBUCKET_LEAF(btree->root->mem, btree->pagesize)) {
            btree->levels++;  /* for the leaf level */
            for (page = btree->root; 
              page->h.dirsize && page->h.directory[0] != LEAF_PTR(btree); 
              btree->levels++, page = page->h.directory[0]) {
                assert(page->h.directory && page->h.dirsize 
                  && page->h.directory[0]);
            }
        }

        if (iterret != IOBTREE_ITER_FINISH) {
            assert(!CRASH);
            iobtree_delete(btree);
            return NULL;
        }
        assert(iobtree_invariant(btree) == IOBTREE_OK);
    }
    
    return btree;
}

int iobtree_flush(struct iobtree *btree) {
    struct page_iter iter;
    struct page *page;
    unsigned int fileno = -1;
    int fd = -1;
    enum iobtree_ret iterret;

    /* need to allocate pages from freemap */
    if (!(iter.state = malloc(sizeof(*iter.state)))) {
        return 0;
    }

    iter.size = 1;
    iter.capacity = 1;
    iter.state[0].curr = btree->root;
    iter.state[0].state = 0;
    iter.state[0].term = 0;

    while ((iterret = page_iter_next(btree, &iter, FILTER_INMEM, &page)) 
      == IOBTREE_OK) {
        if (page->h.dirty) {
            /* pin the correct fd */
            if ((fileno != page->h.fileno)) {
                if (fd != -1) {
                    fdset_unpin(btree->fd, btree->fdset, fileno, fd);
                }

                if ((fd = fdset_pin(btree->fd, btree->fdset, 
                    page->h.fileno, 0, SEEK_CUR)) >= 0) {

                    fileno = page->h.fileno;
                } else {
                    return 0;
                }
            }

            if ((lseek(fd, page->h.offset, SEEK_SET) != (off_t) -1) 
              && (write(fd, page->mem, btree->pagesize) 
                == (ssize_t) btree->pagesize)) {

                page->h.dirty = 0;
            } else {
                return 0;
            }
        }
    }

    free(iter.state);

    if (iterret != IOBTREE_ITER_FINISH) {
        assert(!CRASH);
        return 0;
    }

    if (btree->leaf->h.dirty) {
        if ((fileno != btree->leaf->h.fileno)) {
            if (fd != -1) {
                fdset_unpin(btree->fd, btree->fdset, fileno, fd);
            }

            if ((fd = fdset_pin(btree->fd, btree->fdset, 
                btree->leaf->h.fileno, 0, SEEK_CUR)) >= 0) {

                fileno = btree->leaf->h.fileno;
            } else {
                return 0;
            }
        }

        if ((lseek(fd, btree->leaf->h.offset, SEEK_SET) != (off_t) -1) 
          && (write(fd, btree->leaf->mem, btree->pagesize) 
            == (ssize_t) btree->pagesize)) {

            btree->leaf->h.dirty = 0;
        } else {
            return 0;
        }
    }

    if (btree->root->h.dirty) {
        if ((fileno != btree->root->h.fileno)) {
            if (fd != -1) {
                fdset_unpin(btree->fd, btree->fdset, fileno, fd);
            }

            if ((fd = fdset_pin(btree->fd, btree->fdset, 
                btree->root->h.fileno, 0, SEEK_CUR)) >= 0) {

                fileno = btree->root->h.fileno;
            } else {
                return 0;
            }
        }

        if ((lseek(fd, btree->root->h.offset, SEEK_SET) != (off_t) -1) 
          && (write(fd, btree->root->mem, btree->pagesize) 
            == (ssize_t) btree->pagesize)) {

            btree->root->h.dirty = 0;
        } else {
            return 0;
        }
    }

    if (fd >= 0) {
        fdset_unpin(btree->fd, btree->fdset, fileno, fd);
    }

    assert(iobtree_invariant(btree) == IOBTREE_OK);
    return 1;
}

void iobtree_delete(struct iobtree *btree) {
    struct page_iter iter;
    struct page *page;
    enum iobtree_ret iterret;

    /* need to iterate over in-memory pages, freeing them */
    if (!(iter.state = malloc(sizeof(*iter.state)))) {
        return;
    }

    iter.size = 1;
    iter.capacity = 1;
    iter.state[0].curr = btree->root;
    iter.state[0].state = 0;
    iter.state[0].term = 0;

    while ((iterret = page_iter_next(btree, &iter, FILTER_INMEM, &page)) 
      == IOBTREE_OK) {
        assert(page->h.directory);
        assert(!BTBUCKET_LEAF(page->mem, btree->pagesize));
        if (page != btree->leaf) {
            free(page->h.directory);
            free(page);
        }
    }

    free(iter.state);

    if (iterret != IOBTREE_ITER_FINISH) {
        assert(!CRASH);
    }

    assert(!btree->leaf->h.directory);
    assert(!btree->tmp->h.directory);
    free(btree->leaf);
    free(btree->tmp);
    free(btree);
    return;
}
 
static enum iobtree_ret traverse(struct iobtree *btree, const char *term, 
  unsigned int termlen, struct page *start, struct page **page, 
  struct page **parent) {
    struct page *curr,
                *prev = NULL;
    unsigned int veclen,
                 fileno,
                 index;
    unsigned long int offset;
    void *addr;
    enum iobtree_ret ret;

    for (curr = start; !BTBUCKET_LEAF(curr->mem, btree->pagesize); ) {
        /* watch for infinite looping over our dummy node */
        assert(curr && curr != LEAF_PTR(btree));

        prev = curr;

        if ((addr = bucket_search(BTBUCKET_BUCKET(curr->mem),
            BTBUCKET_SIZE(curr->mem, btree->pagesize),
            btree->node_strategy, term, termlen, &veclen, &index))) {

            assert(curr->h.directory);
            assert(curr->h.dirsize > index);
            if (curr->h.directory[index] 
              && curr->h.directory[index] != LEAF_PTR(btree)) {
                /* internal node below */
                assert(curr != curr->h.directory[index]);
                curr = curr->h.directory[index];
                continue;
            }

            BTBUCKET_ENTRY(addr, &fileno, &offset);

            if (((btree->leaf->h.fileno == fileno)
                && (btree->leaf->h.offset == offset))
              || ((!btree->leaf->h.dirty
                  || ((ret = write_page(btree, btree->leaf)) == IOBTREE_OK))
                && read_page(btree, fileno, offset, btree->leaf, NULL, 
                    &ret))) {

                /* either it is a leaf node, or we didn't know */
                if (BTBUCKET_LEAF(btree->leaf->mem, btree->pagesize)) {
                    curr->h.directory[index] = LEAF_PTR(btree);
                    curr = btree->leaf;
                    break;
                } else {
                    /* ensure parent is set */
                    curr->h.directory[index] = btree->leaf;
                    btree->leaf->h.parent = curr;
                    curr = btree->leaf;
                    /* have read a non-leaf into leaf page, replace it */
                    if (!(btree->leaf
                      = new_page(btree->pagesize, NULL))) {
                        return IOBTREE_ENOMEM;
                    }
                }
            } else {
                assert(ret != IOBTREE_OK);
                assert(!CRASH);
                return ret;
            }
        } else {
            /* shouldn't get here, something has gone badly wrong */
            assert(!CRASH);
            return IOBTREE_ERR;
        }
    }

    if (page) {
        *page = curr;
    }
    if (parent) {
        *parent = prev;
    }
    return IOBTREE_OK;
}

enum split_fn {
    SPLIT_ALLOC,
    SPLIT_REALLOC,
    SPLIT_APPEND
};

/* internal function to split a leaf node and propagate split back up the 
 * tree */
static enum iobtree_ret split_nodes(struct iobtree *btree, const char *term, 
  unsigned int termlen, unsigned int fileno, unsigned long int offset, 
  struct page *curr, enum split_fn fn) {
    void *ret;                          /* return value from bucket ops */
    int toobig,                         /* whether requested op would overflow 
                                         * an empty bucket */
        leftcmp;                        /* whether additional term goes in 
                                         * left bucket or not */
    unsigned int entries,               /* number of entries in bucket */
                 index,                 /* index bucket ops occurred at */
                 termno;                /* termno of term we're inserting */
    struct page *newpage = NULL,        /* pointer to new page structure */
                *prev;                  /* pointer to page below current 
                                         * level */

    if (fn == SPLIT_REALLOC) {
        /* we don't care about the difference between alloc and realloc 
         * any more */
        fn = SPLIT_ALLOC;
    }

    prev = LEAF_PTR(btree);

    assert(iobtree_invariant(btree) == IOBTREE_OK);

    while (curr) {
        /* attempt to insert into current bucket */
        assert(!BTBUCKET_LEAF(curr->mem, btree->pagesize));

        if (fn == SPLIT_ALLOC) {
            ret = bucket_alloc(BTBUCKET_BUCKET(curr->mem), 
                BTBUCKET_SIZE(curr->mem, btree->pagesize),  
                btree->node_strategy, term, termlen, BTBUCKET_ENTRY_SIZE(), 
                &toobig, &index);
        } else {
            assert(fn == SPLIT_APPEND);
            ret = bucket_append(BTBUCKET_BUCKET(curr->mem), 
                BTBUCKET_SIZE(curr->mem, btree->pagesize),  
                btree->node_strategy, term, termlen, BTBUCKET_ENTRY_SIZE(), 
                &toobig);
            index = bucket_entries(BTBUCKET_BUCKET(curr->mem), 
                BTBUCKET_SIZE(curr->mem, btree->pagesize),  
                btree->node_strategy) - 1;
        }

        if (ret) {
            /* increase directory size if necessary */
            entries = bucket_entries(BTBUCKET_BUCKET(curr->mem), 
                BTBUCKET_SIZE(curr->mem, btree->pagesize), 
                btree->node_strategy);
            if (curr->h.dirsize < entries) {
                void *ptr;
                unsigned int newsize;
                newsize = curr->h.dirsize + BIT_DIV2(curr->h.dirsize, 1);
                if ((ptr = realloc(curr->h.directory, 
                    newsize * sizeof(*curr->h.directory)))) {

                    curr->h.directory = ptr;
                    BIT_ARRAY_NULL(&curr->h.directory[curr->h.dirsize], 
                      newsize - curr->h.dirsize);
                    curr->h.dirsize = newsize;
                } else {
                    return IOBTREE_ENOMEM;
                }
            }

            /* fix dir entry */
            assert(!index || (prev == LEAF_PTR(btree))
              || curr->h.directory[index - 1] != prev);
            memmove(&curr->h.directory[index + 1], &curr->h.directory[index], 
              sizeof(*curr->h.directory) * (entries - index - 1));
            curr->h.directory[index] = prev;

            /* insertion succeeded, copy reference in and finish */
            BTBUCKET_SET_ENTRY(ret, fileno, offset);
            curr->h.dirty = 1;
            assert(iobtree_invariant(btree) == IOBTREE_OK);
            return IOBTREE_OK;
        } else if (toobig) {
            assert(0);
            return IOBTREE_ERR;
        }
 
        /* allocate a new node */
        if ((newpage = new_page(btree->pagesize, curr->h.parent))) {
            if (freemap_malloc(btree->freemap, &newpage->h.fileno, 
              &newpage->h.offset, &btree->pagesize, FREEMAP_OPT_EXACT)) {

                termno = 0;
                btbucket_new(newpage->mem, btree->pagesize, 0, 0, 0, NULL, 
                  &termno);
            } else {
                if (newpage) {
                    free(newpage);
                }
                assert(0);
                return IOBTREE_ERR;
            }
        } else {
            return IOBTREE_ENOMEM;
        }

        if (fn == SPLIT_APPEND) {
            /* XXX: when we allow bucket fill specification we should still 
             * fill to failure and then split the bucket afterward.  
             * With a little modification this could give us shortest term 
             * selection for free. */

            termno = bucket_entries(BTBUCKET_BUCKET(curr->mem), 
                BTBUCKET_SIZE(curr->mem, btree->pagesize), 
                btree->leaf_strategy);
            leftcmp = 0;
        } else {
            /* find split point */
            termno = bucket_find_split_entry(BTBUCKET_BUCKET(curr->mem), 
                BTBUCKET_SIZE(curr->mem, btree->pagesize), btree->node_strategy,
                0 /* XXX: provide range */, 
                term, termlen, termlen + BTBUCKET_ENTRY_SIZE(), &leftcmp);
        }
        newpage->h.dirsize = curr->h.dirsize;

        /* assign a directory to newpage and split the node */
        if ((newpage->h.directory 
            = malloc(sizeof(*newpage->h.directory) * newpage->h.dirsize))
          && bucket_split(BTBUCKET_BUCKET(curr->mem), 
            BTBUCKET_SIZE(curr->mem, btree->pagesize), 
            BTBUCKET_BUCKET(newpage->mem),
            BTBUCKET_SIZE(newpage->mem, btree->pagesize), btree->node_strategy,
            termno)) {

            unsigned int i;

            /* it split successfully */

            curr->h.dirty = 1;
            newpage->h.dirty = 1;
            newpage->h.parent = curr->h.parent;

            /* fix up directories */
            memcpy(newpage->h.directory, &curr->h.directory[termno], 
              (newpage->h.dirsize - termno) * sizeof(*curr->h.directory));

            /* fix up parent pointers from moved sub-pages */
            for (i = 0; i < newpage->h.dirsize - termno; i++) {
                if (newpage->h.directory[i]) {
                    if (newpage->h.directory[i] != LEAF_PTR(btree)) {
                        newpage->h.directory[i]->h.parent = newpage;
                    }
                }
            }

            /* set unused pointers to NULL */
            BIT_ARRAY_NULL(&curr->h.directory[termno], 
              newpage->h.dirsize - termno);
            BIT_ARRAY_NULL(&newpage->h.directory[newpage->h.dirsize - termno], 
              termno);

            /* insert new entry */
 
            if ((!leftcmp
              /* allocate to right bucket */
              && (ret = bucket_alloc(BTBUCKET_BUCKET(newpage->mem),
                BTBUCKET_SIZE(newpage->mem, btree->pagesize), 
                btree->node_strategy, term, termlen, BTBUCKET_ENTRY_SIZE(), 
                &toobig, &index)))

              /* allocate to left bucket */
              || (leftcmp 
                && ((ret = bucket_alloc(BTBUCKET_BUCKET(curr->mem),
                    BTBUCKET_SIZE(curr->mem, btree->pagesize), 
                    btree->node_strategy, term, termlen, BTBUCKET_ENTRY_SIZE(),
                    &toobig, &index))))) {

                unsigned int veclen;
                struct page *tmppage;

                if (leftcmp) {
                    tmppage = curr;
                    entries = termno;
                } else {
                    tmppage = newpage;
                    entries = newpage->h.dirsize - termno;
                }

                /* fix dir entry, set page for next iteration */
                memmove(&tmppage->h.directory[index + 1], 
                  &tmppage->h.directory[index], 
                  sizeof(*tmppage->h.directory) * (entries - index));
                tmppage->h.directory[index] = prev;

                /* ensure parent is correct, previously set value can be out 
                 * of date by now */
                if (prev != LEAF_PTR(btree)) {
                    prev->h.parent = tmppage; 
                }

                prev = newpage;      /* set up prev for next iteration */

                /* copy pointer in */
                BTBUCKET_SET_ENTRY(ret, fileno, offset);

                /* succeeded, retrieve splitting key (first in right-hand 
                 * bucket) */
                termno = 0;
                term = bucket_next_term(BTBUCKET_BUCKET(newpage->mem),
                    BTBUCKET_SIZE(newpage->mem, btree->pagesize), 
                    btree->node_strategy, &termno, &termlen, &ret, &veclen);
                assert(bucket_entries(BTBUCKET_BUCKET(newpage->mem),
                    BTBUCKET_SIZE(newpage->mem, btree->pagesize), 
                    btree->node_strategy));
                assert(term);
                fileno = newpage->h.fileno;
                offset = newpage->h.offset;
            } else {
                /* shouldn't get here, something has gone badly wrong */
                assert(!toobig);
                assert(0);
                return IOBTREE_ERR;
            }
        } else {
            if (newpage->h.directory) {
                free(newpage->h.directory);
            }
            /* it didn't split, this either means that theres one (big) entry 
             * or that something is wrong */
            assert(0);
            return IOBTREE_ERR;
        }
        
        curr = curr->h.parent;
    }

    /* recursive splitting of nodes has finished, we need to add a new root 
     * node (old one has already been split into newpage and prev) */
    assert(prev == btree->root || prev == LEAF_PTR(btree) || prev == newpage);

    /* allocate new page */
    if (!((curr = new_page(btree->pagesize, NULL))
      && (curr->h.dirsize = newpage ? newpage->h.dirsize : 8)
      && (curr->h.directory = malloc(sizeof(*curr->h.directory) 
          * (curr->h.dirsize))))) {
        if (curr) {
            free(curr);
        }
        return IOBTREE_ENOMEM;
    }
 
    /* place it on the disk */
    if (freemap_malloc(btree->freemap, &curr->h.fileno, &curr->h.offset, 
          &btree->pagesize, FREEMAP_OPT_EXACT)) {

        /* initialise it */
        BIT_ARRAY_NULL(curr->h.directory, curr->h.dirsize);

        termno = 0;
        btbucket_new(curr->mem, btree->pagesize, 0, 0, 0, NULL, &termno);
        bucket_new(BTBUCKET_BUCKET(curr->mem),
            BTBUCKET_SIZE(curr->mem, btree->pagesize), 
            btree->node_strategy);
    } else {
        assert(!CRASH);
        if (curr) {
            if (curr->h.directory) {
                free(curr->h.directory);
            }
            free(curr);
        }
        return IOBTREE_ERR;
    }

    /* insert NULL and and new entry to the new root node */
    if ((ret = bucket_alloc(BTBUCKET_BUCKET(curr->mem), 
        BTBUCKET_SIZE(curr->mem, btree->pagesize), btree->node_strategy, 
        "", 0, BTBUCKET_ENTRY_SIZE(), &toobig, &index))) {

        assert(index == 0);

        /* enter reference to previous root node */
        BTBUCKET_SET_ENTRY(ret, btree->root->h.fileno, 
          btree->root->h.offset);

        if ((ret = bucket_alloc(BTBUCKET_BUCKET(curr->mem), 
            BTBUCKET_SIZE(curr->mem, btree->pagesize), btree->node_strategy,
            term, termlen, BTBUCKET_ENTRY_SIZE(), &toobig, &index))) {

            assert(index == 1);

            /* fix dir entries */
            if (newpage) {
                assert(!BTBUCKET_LEAF(curr->mem, btree->pagesize)
                  && !BTBUCKET_LEAF(newpage->mem, btree->pagesize));

                /* push new root over internal nodes, reference them 
                 * correctly */
                curr->h.directory[1] = newpage;
                newpage->h.parent = curr;

                curr->h.directory[0] = btree->root; 
                btree->root->h.parent = curr;
            } else {
                assert(BTBUCKET_LEAF(btree->root->mem, btree->pagesize));

                /* push new root over leaves, mark them as such */
                curr->h.directory[0] = LEAF_PTR(btree);
                curr->h.directory[1] = LEAF_PTR(btree);
            }

            /* enter reference to new node */
            BTBUCKET_SET_ENTRY(ret, fileno, offset);

            /* mark new root node */
            btree->root = curr;
            btree->root->h.dirty = 1;
            btree->levels++;

            assert(!BTBUCKET_LEAF(btree->root->mem, btree->pagesize));
            assert(iobtree_invariant(btree) == IOBTREE_OK);
            return IOBTREE_OK;
        }
    }

    /* something failed */
    assert(0);
    return IOBTREE_ERR;
}

/* internal function to split a leaf node, then use split_nodes to recursively
 * push the split up the tree */
static void *split_leaf(struct iobtree *btree, struct page *curr, 
  struct page *parent, const char *term, unsigned int termlen, 
  unsigned int newsize, enum split_fn fn, enum iobtree_ret *err) {
    struct page *newpage;                /* current node in cache */
    unsigned int index,                  /* index bucket ops occur at */
                 termno,                 /* term number to split at */
                 fileno,                 /* file number of page */
                 oldsize;                /* size of old allocation for term */
    unsigned long int offset;            /* offset of page */
    int leftcmp,                         /* whether term being changed goes in 
                                          * right or left bucket */
        toobig;                          /* whether allocations are too big 
                                          * for a bucket */
    void *ret;                           /* return value */
    enum iobtree_ret iobret;

    assert(BTBUCKET_LEAF(curr->mem, btree->pagesize));

    /* allocate a new node */
    if ((newpage = btree->tmp)
      && ((newpage->h.parent = NULL), 1)
      && freemap_malloc(btree->freemap, &newpage->h.fileno, &newpage->h.offset,
          &btree->pagesize, FREEMAP_OPT_EXACT)) {
        unsigned int tmp;

        /* copy sibling pointer from current node if its not a reference to the
         * current node */
        btbucket_sibling(curr->mem, btree->pagesize, &fileno, &offset);
        if ((offset == curr->h.offset) && (fileno == curr->h.fileno)) {
            /* reference to current node (ending pointer chain), copy reference
             * to new node into new node to terminate chain */
            fileno = newpage->h.fileno;
            offset = newpage->h.offset;
        }

        tmp = 0;
        btbucket_new(newpage->mem, btree->pagesize, fileno, offset, 1, NULL, 
            &tmp);
    } else {
        if (newpage) {
            if (newpage->h.directory) {
                free(newpage->h.directory);
            }
        }
        return NULL;
    }

    if (fn == SPLIT_REALLOC) {
        /* find out how large the allocation currently is (have to do this 
         * so that we can pass correct stats to find_split_entry) */
        if (!bucket_find(BTBUCKET_BUCKET(curr->mem), 
          BTBUCKET_SIZE(curr->mem, btree->pagesize), 
          btree->leaf_strategy, term, termlen, &oldsize, &index)) {
            /* term wasn't allocated in the first place */
            assert(0);
            if (err) {
                *err = IOBTREE_ERR;
            }
            return NULL;
        }

        /* we do a little bit of buggering around with oldsize so that size 
         * arg to bucket_find_split_entry is newsize - oldsize for 
         * reallocation, and newsize + termlen for allocation */
        oldsize += termlen;
    } else {
        oldsize = 0;
    }

    if (fn == SPLIT_APPEND) {
        /* XXX: when we allow bucket fill specification we should still fill to
         * failure and then split the bucket afterward.  With a little 
         * modification this could give us shortest term selection for free. */

        termno = bucket_entries(BTBUCKET_BUCKET(curr->mem), 
            BTBUCKET_SIZE(curr->mem, btree->pagesize), btree->leaf_strategy);
        leftcmp = 0;
    } else {
        /* find split point (note that the vector and the size of the term are
         * additional data here, since we're adding a term) */
        termno = bucket_find_split_entry(BTBUCKET_BUCKET(curr->mem), 
            BTBUCKET_SIZE(curr->mem, btree->pagesize), btree->leaf_strategy, 
            0 /* XXX: provide range */, 
            term, termlen, newsize + termlen - oldsize, &leftcmp);
    }

    /* split the node */
    assert(curr == btree->leaf);
    assert(newpage == btree->tmp);
    if (bucket_split(BTBUCKET_BUCKET(curr->mem), 
        BTBUCKET_SIZE(curr->mem, btree->pagesize), 
        BTBUCKET_BUCKET(newpage->mem),
        BTBUCKET_SIZE(newpage->mem, btree->pagesize), btree->leaf_strategy, 
        termno)) {

        newpage->h.dirty = 1;
        curr->h.dirty = 1;

        /* it split successfully */

        /* encode new sibling pointer */
        btbucket_set_sibling(curr->mem, btree->pagesize, newpage->h.fileno, 
          newpage->h.offset);

        /* insert/realloc new entry */
 
        if ((!leftcmp
          /* (re)allocate to right bucket */
          && (((fn == SPLIT_REALLOC)
              && (ret = bucket_realloc(BTBUCKET_BUCKET(newpage->mem), 
                BTBUCKET_SIZE(newpage->mem, btree->pagesize), 
                btree->leaf_strategy, term, termlen, newsize, &toobig)))

            || ((fn == SPLIT_ALLOC) 
              && (ret = bucket_alloc(BTBUCKET_BUCKET(newpage->mem), 
                  BTBUCKET_SIZE(newpage->mem, btree->pagesize), 
                  btree->leaf_strategy, term, termlen, newsize, &toobig, 
                  &index)))

            || ((fn == SPLIT_APPEND) 
              && (ret = bucket_append(BTBUCKET_BUCKET(newpage->mem), 
                  BTBUCKET_SIZE(newpage->mem, btree->pagesize), 
                  btree->leaf_strategy, term, termlen, newsize, &toobig)))))

          /* (re)allocate to left bucket */
          || (leftcmp 
            && (((fn == SPLIT_REALLOC)
              && (ret = bucket_realloc(BTBUCKET_BUCKET(curr->mem), 
                  BTBUCKET_SIZE(curr->mem, btree->pagesize),
                  btree->leaf_strategy, term, termlen, newsize, &toobig)))

              || ((fn == SPLIT_ALLOC)
                && (ret = bucket_alloc(BTBUCKET_BUCKET(curr->mem), 
                    BTBUCKET_SIZE(curr->mem, btree->pagesize),
                    btree->leaf_strategy, term, termlen, newsize, &toobig, 
                    &index)))

              || ((fn == SPLIT_APPEND)
                && (ret = bucket_append(BTBUCKET_BUCKET(curr->mem), 
                    BTBUCKET_SIZE(curr->mem, btree->pagesize),
                    btree->leaf_strategy, term, termlen, newsize, 
                    &toobig)))))) {

            /* succeeded, retrieve splitting key (first in right-hand 
             * bucket) */
            unsigned int veclen,
                         state = 0;
            void *tmp;

            term = bucket_next_term(BTBUCKET_BUCKET(newpage->mem), 
                BTBUCKET_SIZE(newpage->mem, btree->pagesize), 
                btree->leaf_strategy, &state, &termlen, &tmp, &veclen);
            assert(term);
        } else {
            /* shouldn't get here, something has gone badly wrong */
            assert(!toobig);
            assert(0);
            if (err) {
                *err = IOBTREE_ERR;
            }
            return NULL;
        }
    } else {
        /* it didn't split, this either means that theres one (big) entry 
         * or that something is wrong */
        /* FIXME: get rid of allocated node */
        assert(0);
        if (err) {
            *err = IOBTREE_ERR;
        }
        return NULL;
    }

    /* get parent node if it wasn't provided */
    if (!parent && (btree->root != btree->leaf)) {
        if ((iobret 
            = traverse(btree, term, termlen, btree->root, NULL, &parent) )
          != IOBTREE_OK) {
            assert(0);
            if (err) {
                *err = iobret;
            }
            return NULL;
        }
    }

    assert(curr == btree->leaf);
    assert(newpage == btree->tmp);

    /* adjust right-most entry in the btree if required */
    if ((btree->right.fileno == btree->leaf->h.fileno) 
      && (btree->right.offset == btree->leaf->h.offset)) {
        btree->right.fileno = newpage->h.fileno;
        btree->right.offset = newpage->h.offset;
    }

    /* ensure that the page with the term in it ends up at btree->leaf.  Note
     * that this doesn't affect which page is pointed at by newpage */
    if (!leftcmp) {
        /* swap the two pages over */
        curr = btree->tmp;
        btree->tmp = btree->leaf;
        btree->leaf = curr;
    }

    /* need to recursively split buckets until it accepts the given 
     * allocation */
    if (((iobret = write_page(btree, btree->tmp)) == IOBTREE_OK)
      && ((iobret = split_nodes(btree, term, termlen, newpage->h.fileno, 
        newpage->h.offset, parent, fn)) == IOBTREE_OK)) {

        btree->entries++;
        return ret;
    } else {
        /* XXX: we're pretty screwed here, split partially failed */
        assert(0);
        if (err) {
            *err = iobret;
        }
        return NULL;
    }
}

void *iobtree_alloc(struct iobtree *btree, const char *term, 
  unsigned int termlen, unsigned int size, int *toobig) {
    void *ret;                        /* return value */
    unsigned int index;               /* index bucket ops occur at */
    struct page *curr,                /* current node in cache */
                *parent;              /* curr's parent node */

    btree->timestamp++;               /* update timestamp */
    assert(iobtree_invariant(btree) == IOBTREE_OK);

    /* reject anything > 1/4 bucketsize */
    if (termlen + size > BIT_DIV2(btree->pagesize, 2)) {
        *toobig = 1;
        return NULL;
    }

    /* traverse down the internal nodes */
    if (traverse(btree, term, termlen, btree->root, &curr, &parent) 
      != IOBTREE_OK) {
        return NULL;
    }

    /* ok, have reached leaf node, now try to allocate space from it */
    assert(curr == btree->leaf);
    if ((ret = bucket_alloc(BTBUCKET_BUCKET(curr->mem), 
        BTBUCKET_SIZE(curr->mem, btree->pagesize), btree->leaf_strategy, term, 
        termlen, size, toobig, &index))) {

        /* allocation succeeded, return new space */
        btree->entries++;
        curr->h.dirty = 1;

        assert(iobtree_invariant(btree) == IOBTREE_OK);
        return ret;
    }
    assert(!*toobig); /* should never be too big, we limit them to 1/4 of a 
                       * bucket */

    return split_leaf(btree, curr, parent, term, termlen, size, 
      SPLIT_ALLOC, NULL);
}

void *iobtree_realloc(struct iobtree *btree, const char *term, 
  unsigned int termlen, unsigned int newsize, int *toobig) {
    void *ret;                        /* return value */
    struct page *curr,                /* current node in cache */
                *parent;              /* curr's parent node */

    btree->timestamp++;               /* update timestamp */
    assert(iobtree_invariant(btree) == IOBTREE_OK);

    /* reject anything > 1/4 bucketsize */
    if (termlen + newsize > BIT_DIV2(btree->pagesize, 2)) {
        *toobig = 1;
        return NULL;
    }

    /* traverse down the internal nodes */
    if (traverse(btree, term, termlen, btree->root, &curr, &parent) 
      != IOBTREE_OK) {
        return NULL;
    }

    /* ok, have reached leaf node, now try to reallocate space in it */
    if ((ret = bucket_realloc(BTBUCKET_BUCKET(curr->mem), 
        BTBUCKET_SIZE(curr->mem, btree->pagesize), btree->leaf_strategy, term, 
        termlen, newsize, toobig))) {

        /* XXX: should check if reallocation shrinks bucket too far and perform
         * merge like remove */

        /* allocation succeeded, return new space */
        curr->h.dirty = 1;

        if (!ret) {
            assert(!CRASH);
        }
        assert(iobtree_invariant(btree) == IOBTREE_OK);
        return ret;
    }
    assert(!*toobig); /* should never be too big, we limit them to 1/4 of a 
                       * bucket */

    return split_leaf(btree, curr, parent, term, termlen, newsize, 
      SPLIT_REALLOC, NULL);
}

/* XXX: implement b-tree removal with space guarantee at some stage */
int iobtree_remove(struct iobtree *btree, const char *term, 
  unsigned int termlen) {
    struct page *curr;                /* current node in cache */

    btree->timestamp++;               /* update timestamp */
    assert(iobtree_invariant(btree) == IOBTREE_OK);

    /* traverse down the internal nodes */
    if (traverse(btree, term, termlen, btree->root, &curr, NULL) 
      != IOBTREE_OK) {
        return 0;
    }

    if (bucket_remove(BTBUCKET_BUCKET(curr->mem), 
        BTBUCKET_SIZE(curr->mem, btree->pagesize), btree->leaf_strategy, term, 
        termlen)) {

        curr->h.dirty = 1;
        assert(iobtree_invariant(btree) == IOBTREE_OK);
        return 1;
    } else {
        return 0;
    }
}

void *iobtree_find(struct iobtree *btree, const char *term, 
  unsigned int termlen, int write, unsigned int *veclen) {
    struct page *curr;
    unsigned int index;

    /* traverse down the internal nodes */
    if (traverse(btree, term, termlen, btree->root, &curr, NULL) 
      != IOBTREE_OK) {
        return NULL;
    }

    /* record last access for caching algorithm */
    curr->h.dirty |= write;

    /* now just need to search the leaf node for the entry */
    return bucket_find(BTBUCKET_BUCKET(curr->mem), 
        BTBUCKET_SIZE(curr->mem, btree->pagesize), btree->leaf_strategy, term, 
        termlen, veclen, &index);
}

void *iobtree_append(struct iobtree *btree, const char *term,
  unsigned int termlen, unsigned int size, int *toobig) {
    struct page *parent;
    enum iobtree_ret ret;
    void *addr;
    unsigned int index;

    btree->timestamp++;               /* update timestamp */
    assert(iobtree_invariant(btree) == IOBTREE_OK);

    /* reject anything > 1/4 bucketsize */
    if (termlen + size > BIT_DIV2(btree->pagesize, 2)) {
        *toobig = 1;
        return NULL;
    }

    /* ensure right-most page is loaded */
    if (((btree->leaf->h.fileno == btree->right.fileno)
        && (btree->leaf->h.offset == btree->right.offset))
      || ((!btree->leaf->h.dirty
          || ((ret = write_page(btree, btree->leaf)) == IOBTREE_OK))
        && read_page(btree, btree->right.fileno, btree->right.offset, 
            btree->leaf, NULL, &ret))) {

        if (DEAR_DEBUG) {
            /* ensure that the term we're inserting really is larger than the
             * ones already here */

            unsigned int prevtermlen,
                         len;
            const char *prevterm;

            if ((prevterm = bucket_term_at(BTBUCKET_BUCKET(btree->leaf->mem), 
                BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize), 
                btree->leaf_strategy, 0, &prevtermlen, &addr, &len))) {

                assert(str_nncmp(prevterm, prevtermlen, term, termlen) < 0);
            }
        }

        /* insert into right-most page */
        if ((addr = bucket_alloc(BTBUCKET_BUCKET(btree->leaf->mem), 
            BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize), 
            btree->leaf_strategy, term, termlen, size, toobig, &index))) {

            /* successfully inserted */
            btree->leaf->h.dirty = 1;
            btree->entries++;
            return addr;
        }
        assert(!*toobig);

        /* XXX: when we allow bucket fill specification we should still fill to
         * failure and then split the bucket afterward.  With a little 
         * modification this could give us shortest term selection for free. */

        /* get parent node */
        if ((ret = traverse(btree, term, termlen, btree->root, NULL, &parent))
          != IOBTREE_OK) {
            assert(!CRASH);
            return NULL;
        }

        return split_leaf(btree, btree->leaf, parent, term, termlen, size, 
          SPLIT_APPEND, NULL);
    } else {
        assert(!CRASH);
        return NULL;
    }
}

const char *iobtree_next_term(struct iobtree *btree, 
  unsigned int *state, unsigned int *termlen, void **data, unsigned int *len) {
    const char *ret = NULL;
    struct page *curr;
    unsigned long int offset;

    if (!(state[0] || state[1] || state[2])) {
        /* traverse down the internal nodes */
        if (traverse(btree, "", 0, btree->root, &curr, NULL) 
          != IOBTREE_OK) {
            return NULL;
        }

        state[0] = curr->h.fileno;
        state[1] = curr->h.offset;
    } else {
        /* page in leaf if necessary */
        curr = btree->leaf;
        if (((btree->leaf->h.fileno == state[0]) 
            && (btree->leaf->h.offset == state[1])) 
          || ((!btree->leaf->h.dirty
              || (write_page(btree, btree->leaf) == IOBTREE_OK))
            /* note that we don't necessarily know what the parent is now,
             * because we haven't necessarily traversed downward to find it */
            && read_page(btree, state[0], state[1], btree->leaf, NULL, NULL))) {
            curr = btree->leaf;
        } else {
            return NULL;
        }
    }
    assert(curr);
    assert(curr == btree->leaf);
    assert(BTBUCKET_LEAF(curr->mem, btree->pagesize));

    while (curr
      && !(ret = bucket_next_term(BTBUCKET_BUCKET(curr->mem), 
          BTBUCKET_SIZE(curr->mem, btree->pagesize), 
          btree->leaf_strategy, &state[2], termlen, data, len))) {

        /* ran out of terms in that bucket, go to next one */
        btbucket_sibling(curr->mem, btree->pagesize, &state[0], &offset);
        state[1] = offset;  /* XXX: hack around long int/int mismatch */

        if ((state[0] == curr->h.fileno) && (state[1] == curr->h.offset)) {
            /* self pointer indicates the end of the btree */
            return NULL;
        }

        /* page in leaf */
        assert(curr == btree->leaf);
        if ((!btree->leaf->h.dirty
            || (write_page(btree, btree->leaf) == IOBTREE_OK))
          /* note that we don't necessarily know what the parent is now,
           * because we haven't necessarily traversed downward to find it */
          && read_page(btree, state[0], state[1], curr, NULL, NULL)) {
            state[2] = 0;
        } else {
            return NULL;
        }
    }

    return ret;
}

struct iobtree_iter {
    struct iobtree *btree;
    unsigned long int offset;
    unsigned int fileno;
    unsigned int entries;
    unsigned int index;
    unsigned int timestamp;
};

enum iobtree_ret iobtree_iter_invariant(struct iobtree_iter *iter) {
    struct iobtree *btree = iter->btree;

    if (!DEAR_DEBUG) {
        return IOBTREE_OK;
    }

    if (btree->leaf->h.fileno != iter->fileno
      || btree->leaf->h.offset != iter->offset) {
        /* page in the leaf we're on */
        if ((!btree->leaf->h.dirty
            || (write_page(btree, btree->leaf) == IOBTREE_OK))
          && read_page(btree, iter->fileno, iter->offset, btree->leaf, 
              NULL, NULL)) {
            /* successfully read node, do nothing */
        } else {
            return IOBTREE_OK;   /* its ok to fail in _invariant */
        }
    }

    /* bucket must be a leaf */
    if (!BTBUCKET_LEAF(btree->leaf, btree->pagesize)) {
        assert(!CRASH);
        return IOBTREE_ERR;
    }

    /* cached number of iterations should be correct */
    if (iter->entries != bucket_entries(BTBUCKET_BUCKET(btree->leaf->mem), 
        BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize), 
        btree->leaf_strategy)) {

        assert(!CRASH);
        return IOBTREE_ERR;
    }

    /* the index should be in the range [0, entries] */
    if (iter->index > iter->entries) {
        assert(!CRASH);
        return IOBTREE_ERR;
    }

    /* the timestamp should indicate that all modifications have been made
     * through this iterator */
    if (iter->timestamp != iter->btree->timestamp) {
        assert(!CRASH);
        return IOBTREE_ERR;
    }

    return IOBTREE_OK;
}

/* function to search a bucket for an entry equal to or next greater than given
 * target.  This should probably be made part of the bucket interface to make it
 * more efficient at some stage */
static void *locate(void *bucketmem, unsigned int bucketsize, int strategy, 
  const char *term, unsigned int termlen, unsigned int *veclen, 
  unsigned int *idx) {
    int cmp;
    void *data;
    unsigned int currtermlen = 0,
                 datalen,
                 entries;
    void *curr = bucket_search(bucketmem, bucketsize, strategy,
        term, termlen, veclen, idx);
    const char *currterm = bucket_term_at(bucketmem, bucketsize,
        strategy, *idx, &currtermlen, &data, &datalen);

    if ((cmp = str_nncmp(currterm, currtermlen, term, termlen)) >= 0) {
        /* have found our entry, stop */
        return curr;
    }
    assert(cmp < 0);

    entries = bucket_entries(bucketmem, bucketsize, strategy);

    if (*idx == entries - 1) {
        /* not in this bucket */
        return NULL;
    } else {
        /* advance to next term and return that */
        (*idx)++;
        currterm 
          = bucket_term_at(bucketmem, bucketsize, strategy, *idx, &currtermlen, 
            &data, veclen);
        assert(currterm);
        return data;
    }
}

struct iobtree_iter *iobtree_iter_new(struct iobtree *btree, 
  const char *term, unsigned int termlen) {
    struct page *curr;
    struct iobtree_iter *iter = NULL;
    unsigned int index,
                 datalen;

    if (traverse(btree, term, termlen, btree->root, &curr, NULL) 
      == IOBTREE_OK) {

        /* find index in found bucket */
        locate(BTBUCKET_BUCKET(curr->mem), 
            BTBUCKET_SIZE(curr->mem, btree->pagesize), btree->leaf_strategy,
            term, termlen, &datalen, &index);

        if ((iter = malloc(sizeof(*iter)))) {
            iter->btree = btree;
            iter->fileno = curr->h.fileno;
            iter->offset = curr->h.offset;
            iter->index = index;
            iter->timestamp = btree->timestamp;

            /* skip empty buckets */
            while (!(iter->entries = bucket_entries(BTBUCKET_BUCKET(curr->mem), 
                BTBUCKET_SIZE(curr->mem, btree->pagesize), 
                btree->leaf_strategy))) {

                btbucket_sibling(curr->mem, btree->pagesize, &iter->fileno, 
                  &iter->offset);

                if (iter->fileno == btree->leaf->h.fileno 
                  && iter->offset == btree->leaf->h.offset) {
                    /* off the end of the last page, without finding a 
                     * non-empty bucket */
                    return iter;
                } else if ((!btree->leaf->h.dirty
                    || (write_page(btree, btree->leaf) == IOBTREE_OK))
                  && read_page(btree, iter->fileno, iter->offset, btree->leaf, 
                      NULL, NULL)) {
                    /* successfully read node, do nothing */
                } else {
                    assert(0);
                    free(iter);
                    return NULL;
                }
            }
        }
    }

    assert(!iter || iobtree_iter_invariant(iter) == IOBTREE_OK);
    return iter;
}

void iobtree_iter_delete(struct iobtree_iter *iter) {
    free(iter);
    return;
}

enum iobtree_ret iobtree_iter_curr(struct iobtree_iter *iter,
  char *termbuf, unsigned int termbuflen, unsigned int *termlen, 
  unsigned int *datalen) {
    struct iobtree *btree = iter->btree;
    const char *term;
    unsigned int tlen,
                 dlen;
    void *data;
    enum iobtree_ret ret;

    assert(iobtree_iter_invariant(iter) == IOBTREE_OK);

    if (btree->leaf->h.fileno != iter->fileno
      || btree->leaf->h.offset != iter->offset) {
        /* page in the leaf we're on */
        if ((!btree->leaf->h.dirty
            || ((ret = write_page(btree, btree->leaf)) == IOBTREE_OK))
          && read_page(btree, iter->fileno, iter->offset, btree->leaf, 
              NULL, &ret)) {
            /* successfully read node, do nothing */
        } else {
            return ret;
        }
    }

    /* iterate across leaf nodes until we hit the next entry */
    while ((iter->index >= iter->entries) 
      || !(term = bucket_term_at(BTBUCKET_BUCKET(btree->leaf->mem), 
        BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize),
        btree->leaf_strategy, iter->index, &tlen, &data, &dlen))) {

        unsigned int prev_fileno = iter->fileno;
        unsigned long int prev_offset = iter->offset;

        btbucket_sibling(btree->leaf->mem, btree->pagesize, &iter->fileno, 
          &iter->offset);

        if (iter->fileno == prev_fileno 
          && iter->offset == prev_offset) {
            /* end of iteration */
            assert(iobtree_iter_invariant(iter) == IOBTREE_OK);
            return IOBTREE_ITER_FINISH;
        } else if ((!btree->leaf->h.dirty
            || ((ret = write_page(btree, btree->leaf)) == IOBTREE_OK))
          && read_page(btree, iter->fileno, iter->offset, btree->leaf, NULL, 
              &ret)) {
            /* successfully read node, do nothing */
        } else {
            return ret;
        }

        iter->index = 0;
        iter->entries 
          = bucket_entries(BTBUCKET_BUCKET(btree->leaf->mem), 
            BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize), 
            btree->leaf_strategy);
    }

    if (tlen > termbuflen) {
        memcpy(termbuf, term, termbuflen);
    } else {
        memcpy(termbuf, term, tlen);
    }

    *termlen = tlen;
    *datalen = dlen;
    assert(iobtree_iter_invariant(iter) == IOBTREE_OK);
    return IOBTREE_OK;
}

enum iobtree_ret iobtree_iter_next(struct iobtree_iter *iter, 
  char *termbuf, unsigned int termbuflen, unsigned int *termlen,
  const char *seekterm, unsigned int seektermlen) {
    struct iobtree *btree = iter->btree;
    enum iobtree_ret ret;

    /* load current leaf if necessary */
    if (btree->leaf->h.fileno != iter->fileno
      || btree->leaf->h.offset != iter->offset) {
        /* page in the leaf we're on */
        if ((!btree->leaf->h.dirty
            || ((ret = write_page(btree, btree->leaf)) == IOBTREE_OK))
          && read_page(btree, iter->fileno, iter->offset, btree->leaf, NULL, 
              &ret)) {
            /* successfully read node, do nothing */
        } else {
            return ret;
        }
    }

    if (seektermlen) {
        unsigned int entries,
                     currtermlen,
                     datalen,
                     index;
        const char *currterm;
        void *data;
        struct page *parent;
        enum iobtree_ret ret;
        int cmp;

        /* finger search toward selected location.  Finger searching was
         * originally (i believe) proposed in 'A new representation for linear
         * lists', Guibas, McCreight, Plass, Roberts.  The idea is that to
         * traverse from a leaf node to a new location in the tree, the
         * (theoretically) optimal way to do this is to ascend until we reach a
         * node that contains a range of keys including the one we want.  
         * We can then descend to the exact location we want */

        /* search current node */
        index = iter->entries - 1;
        currterm = locate(BTBUCKET_BUCKET(btree->leaf->mem), 
            BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize),
            btree->leaf_strategy, seekterm, seektermlen, &datalen, &index);
        assert(currterm || (index == iter->entries - 1));

        if (currterm) {
            /* have found our entry, stop */
            iter->index = index;
            assert(iobtree_iter_invariant(iter) == IOBTREE_OK);
            return IOBTREE_OK;
        }

        /* have to move to the next bucket, get a pointer to the parent node 
         * (we started at a leaf, so we don't this currently), which we'll 
         * need below */
        if ((ret = traverse(btree, seekterm, seektermlen, btree->root, NULL, 
            &parent)) != IOBTREE_OK) {

            return ret;
        }
 
        if (cmp < 0) {
            /* seekterm is (probably - it could just be absent) greater than 
             * our current bucket's boundaries.  Ascend, looking at right-most 
             * pointers until we encounter the range we want */
            while ((entries = bucket_entries(BTBUCKET_BUCKET(parent->mem), 
                BTBUCKET_SIZE(parent->mem, btree->pagesize), 
                btree->node_strategy))
              && (currterm = bucket_term_at(BTBUCKET_BUCKET(parent->mem), 
                  BTBUCKET_SIZE(parent->mem, btree->pagesize), 
                  btree->node_strategy, entries - 1, &currtermlen, &data, 
                  &datalen))
              && (str_nncmp(currterm, currtermlen, seekterm, seektermlen) < 0)
              /* stop when we reach root node */
              && (parent->h.parent && (parent = parent->h.parent))) ;
        } else {
            assert(cmp > 0);

            /* seekterm is (probably - it could just be absent) less than our 
             * current bucket's boundaries.  Ascend, looking at left-most 
             * pointers until we encounter the range we want */
            while ((currterm = bucket_term_at(BTBUCKET_BUCKET(parent->mem), 
                  BTBUCKET_SIZE(parent->mem, btree->pagesize), 
                  btree->node_strategy, 0, &currtermlen, &data, 
                  &datalen))
              && (str_nncmp(currterm, currtermlen, seekterm, seektermlen) > 0)
              && (parent = parent->h.parent)) ;

        }

        assert(parent);  /* shouldn't have iterated off the top of the tree in 
                          * either case */
        assert(entries); /* shouldn't have an empty internal node */

        /* now descend down to appropriate leaf (note that we traverse starting
         * at the parent we just found) */
        if ((ret = traverse(btree, seekterm, seektermlen, parent, NULL, NULL))
          != IOBTREE_OK) {
            return ret;
        }

        iter->fileno = btree->leaf->h.fileno;
        iter->offset = btree->leaf->h.offset;
        iter->entries = bucket_entries(BTBUCKET_BUCKET(btree->leaf->mem), 
            BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize), 
            btree->leaf_strategy);

        /* find the seek term in the bucket we just found */
        currterm = locate(BTBUCKET_BUCKET(btree->leaf->mem), 
            BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize),
            btree->leaf_strategy, seekterm, seektermlen, &datalen, 
            &iter->index);

        /* FIXME: what happens if this bucket doesn't contain an item greater
         * than the seekterm? */

        assert(iobtree_iter_invariant(iter) == IOBTREE_OK);
        return IOBTREE_OK; 
    } else {
        /* just increment to next term */
        if (iter->index >= iter->entries) {
            do {
                unsigned int prev_fileno = iter->fileno;
                unsigned long int prev_offset = iter->offset;

                btbucket_sibling(btree->leaf->mem, btree->pagesize, 
                  &iter->fileno, &iter->offset);

                if (iter->fileno == prev_fileno 
                  && iter->offset == prev_offset) {
                    /* end of iteration */
                    assert(iobtree_iter_invariant(iter) == IOBTREE_OK);
                    return IOBTREE_ITER_FINISH;
                } else if ((!btree->leaf->h.dirty
                    || ((ret = write_page(btree, btree->leaf)) == IOBTREE_OK))
                  && read_page(btree, iter->fileno, iter->offset, btree->leaf, 
                      NULL, &ret)) {
                    /* successfully read node, do nothing */
                } else {
                    return ret;
                }

                iter->index = 0;

                iter->entries 
                  = bucket_entries(BTBUCKET_BUCKET(btree->leaf->mem), 
                    BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize), 
                    btree->leaf_strategy);
            } while (iter->index >= iter->entries);
        } else {
            iter->index++;
        }
        assert(iobtree_iter_invariant(iter) == IOBTREE_OK);
        return IOBTREE_OK;
    }
}

void *iobtree_iter_alloc(struct iobtree_iter *iter,
  const char *term, unsigned int termlen, unsigned int size, int *toobig) {
    struct iobtree *btree = iter->btree;
    void *ret;
    unsigned int tmp;

    btree->timestamp++;               /* update timestamp */
    iter->timestamp++;                /* update iteration timestamp */
    assert(iobtree_invariant(btree) == IOBTREE_OK);
    assert(iobtree_iter_invariant(iter) == IOBTREE_OK);

    /* reject anything > 1/4 bucketsize */
    if (termlen + size > BIT_DIV2(btree->pagesize, 2)) {
        *toobig = 1;
        return NULL;
    }

    /* navigate to the correct bucket (this is necessary in some form even in 
     * the most trivial implementation of this function, because we require the
     * ability to insert before the current item, and we may have already
     * iterated to the next bucket) */
    if (iobtree_iter_next(iter, NULL, 0, &tmp, term, termlen) != IOBTREE_OK) {
        return NULL;
    }

    if ((ret = bucket_alloc(BTBUCKET_BUCKET(btree->leaf->mem), 
        BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize), btree->leaf_strategy, 
        term, termlen, size, toobig, &iter->index))) {

        /* allocation succeeded, return new space */
        btree->entries++;
        iter->entries++;
        btree->leaf->h.dirty = 1;

        assert(iobtree_invariant(btree) == IOBTREE_OK);
        assert(iobtree_iter_invariant(iter) == IOBTREE_OK);
        return ret;
    }
    assert(!*toobig); /* should never be too big, we limit them to 1/4 of a 
                       * bucket */

    ret = split_leaf(btree, btree->leaf, NULL, term, termlen, size, 
      SPLIT_ALLOC, NULL);

    /* ensure we end up at the right bucket, which is guaranteed to be in
     * btree->leaf */
    iter->fileno = btree->leaf->h.fileno;
    iter->offset = btree->leaf->h.offset;
    iter->entries = bucket_entries(BTBUCKET_BUCKET(btree->leaf->mem), 
                    BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize), 
                    btree->leaf_strategy);

    locate(BTBUCKET_BUCKET(btree->leaf->mem), 
        BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize),
        btree->leaf_strategy, term, termlen, &tmp, &iter->index);

    assert(iobtree_iter_invariant(iter) == IOBTREE_OK);
    return ret;
}

void *iobtree_iter_realloc(struct iobtree_iter *iter, 
  unsigned int newsize, int *toobig) {
    struct iobtree *btree = iter->btree;
    const char *term;
    unsigned int termlen = 0,
                 datalen;
    void *data,
         *ret;

    btree->timestamp++;               /* update timestamp */
    iter->timestamp++;                /* update iteration timestamp */
    assert(iobtree_iter_invariant(iter) == IOBTREE_OK);
    assert(iobtree_invariant(btree) == IOBTREE_OK);

    /* reject anything > 1/4 bucketsize */
    if (termlen + newsize > BIT_DIV2(btree->pagesize, 2)) {
        *toobig = 1;
        return NULL;
    }

    if ((ret = bucket_realloc_at(BTBUCKET_BUCKET(btree->leaf->mem), 
        BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize), btree->leaf_strategy, 
        iter->index, newsize, toobig))) {

        /* allocation succeeded, return new space */
        btree->leaf->h.dirty = 1;
        assert(iobtree_invariant(btree) == IOBTREE_OK);
        assert(iobtree_iter_invariant(iter) == IOBTREE_OK);
        return ret;
    }
    assert(!*toobig); /* should never be too big, we limit them to 1/4 of a 
                       * bucket */

    if ((term = bucket_term_at(BTBUCKET_BUCKET(btree->leaf->mem), 
        BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize), btree->leaf_strategy, 
        iter->index, &termlen, &data, &datalen))) {

        ret = split_leaf(btree, btree->leaf, NULL, term, termlen, 
            newsize, SPLIT_REALLOC, NULL);
    } else {
        return NULL;
    }

    /* ensure we end up at the right bucket, which is guaranteed to be in
     * btree->leaf */
    iter->fileno = btree->leaf->h.fileno;
    iter->offset = btree->leaf->h.offset;
    iter->entries = bucket_entries(BTBUCKET_BUCKET(btree->leaf->mem), 
                    BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize), 
                    btree->leaf_strategy);

    locate(BTBUCKET_BUCKET(btree->leaf->mem), 
        BTBUCKET_SIZE(btree->leaf->mem, btree->pagesize),
        btree->leaf_strategy, term, termlen, &datalen, &iter->index);

    assert(iobtree_iter_invariant(iter) == IOBTREE_OK);
    return ret;
}

/* internal function to return the number of leaves and nodes (and total pages
 * if you add them up) */
static int iobtree_page_stats(struct iobtree *btree, unsigned int *leaves, 
  unsigned int *nodes) {
    struct page_iter iter;
    struct page *page;
    enum iobtree_ret iterret;

    *leaves = *nodes = 0;

    if (!(iter.state = malloc(sizeof(*iter.state)))) {
        return 0;
    }

    iter.size = 1;
    iter.capacity = 1;
    iter.state[0].curr = btree->root;
    iter.state[0].state = 0;
    iter.state[0].term = 0;

    while ((iterret = page_iter_next(btree, &iter, FILTER_NONE, &page)) 
      == IOBTREE_OK) {
        if (BTBUCKET_LEAF(btree->root->mem, btree->pagesize)) {
            (*leaves)++;
        } else {
            (*nodes)++;
        }
    }

    free(iter.state);

    if (iterret == IOBTREE_ITER_FINISH) {
        return 1;
    } else {
        assert(!CRASH);
        return 0;
    }
}

unsigned int iobtree_size(struct iobtree *btree) {
    return btree->entries;
}

unsigned long int iobtree_overhead(struct iobtree *btree) {
    /* FIXME: handle errors */
    unsigned int leaves,
                 nodes;
    unsigned long int overhead;
    unsigned int state[3] = {0, 0, 0};
    const char *term;
    unsigned int termlen,
                 len;
    void *data;

    iobtree_page_stats(btree, &leaves, &nodes);

    overhead = (nodes + leaves) * btree->pagesize;

    while ((term = iobtree_next_term(btree, state, &termlen, &data, &len))) {
        overhead -= len + termlen;
    }

    return overhead;
}

unsigned long int iobtree_space(struct iobtree *btree) {
    /* FIXME: handle errors */
    unsigned int leaves,
                 nodes;

    iobtree_page_stats(btree, &leaves, &nodes);

    return (nodes + leaves);
}

unsigned long int iobtree_utilised(struct iobtree *btree) {
    unsigned int state[3] = {0, 0, 0};
    unsigned long int utilised = 0;
    const char *term;
    unsigned int termlen,
                 len;
    void *data;

    while ((term = iobtree_next_term(btree, state, &termlen, &data, &len))) {
        utilised += len;
    }

    return utilised;
}

void iobtree_root(struct iobtree *btree, unsigned int *fileno, 
  unsigned long int *offset) {
    *fileno = btree->root->h.fileno;
    *offset = btree->root->h.offset;
}

#include <stdio.h>

/* internal/debugging function to print a directory bucket.  Returns true on
 * success, 0 on failure. */
int iobtree_print_page(struct iobtree *btree, struct page *page, 
  unsigned int level) {
    unsigned int i,
                 state = 0,
                 len,
                 veclen,
                 fileno;
    const char *term;
    void *data;
    unsigned long int offset;
    struct page *child;

    for (i = 0; i < level * 3; i++) {
        putc(' ', stdout);
    }
    printf("%u %lu (%u entries at %p, parent %p): ", page->h.fileno, 
      page->h.offset, bucket_entries(BTBUCKET_BUCKET(page->mem), 
        BTBUCKET_SIZE(page->mem, btree->pagesize), 
        btbucket_leaf(page->mem, btree->pagesize) 
          ? btree->leaf_strategy : btree->node_strategy), (void *) page, 
      (void *) page->h.parent);

    if (BTBUCKET_LEAF(page->mem, btree->pagesize)) {
        state = 0;
        while ((term = bucket_next_term(BTBUCKET_BUCKET(page->mem), 
            BTBUCKET_SIZE(page->mem, btree->pagesize), btree->node_strategy, 
            &state, &len, &data, &veclen))) {

            putc('\'', stdout);
            for (i = 0; i < len; i++) {
                putc(term[i], stdout);
            }
            printf("' (size %u), ", veclen);
        }

        btbucket_sibling(page->mem, btree->pagesize, &fileno, &offset);

        printf("-> %u %lu\n", fileno, offset);
        return 1;
    }

    state = 0;
    while ((term = bucket_next_term(BTBUCKET_BUCKET(page->mem), 
        BTBUCKET_SIZE(page->mem, btree->pagesize), btree->node_strategy, 
        &state, &len, &data, &veclen))) {

        assert(veclen == BTBUCKET_ENTRY_SIZE());
        BTBUCKET_ENTRY(data, &fileno, &offset);
        assert((page->h.fileno != fileno) || (page->h.offset != offset));

        putc('\'', stdout);
        for (i = 0; i < len; i++) {
            putc(term[i], stdout);
        }
        printf("' (node %u %lu), ", fileno, offset);
    }
    putc('\n', stdout);

    child = new_page(btree->pagesize, NULL);
    assert(child);
    state = 0;
    while ((term = bucket_next_term(BTBUCKET_BUCKET(page->mem), 
        BTBUCKET_SIZE(page->mem, btree->pagesize), btree->node_strategy, 
        &state, &len, &data, &veclen))) {
        
        assert(veclen == BTBUCKET_ENTRY_SIZE());
        BTBUCKET_ENTRY(data, &fileno, &offset);
        assert((page->h.fileno != fileno) || (page->h.offset != offset));

        if (page->h.directory && page->h.directory[state - 1] 
          && (page->h.directory[state - 1] != LEAF_PTR(btree))) {
            if (iobtree_print_page(btree, page->h.directory[state - 1], 
                level + 1)) {

            } else {
                assert(!CRASH);
                return 0;
            }
        } else if ((fileno == btree->leaf->h.fileno) 
          && (offset == btree->leaf->h.offset)) {
            if (iobtree_print_page(btree, btree->leaf, level + 1)) {

            } else {
                assert(!CRASH);
                return 0;
            }
        } else {
            if (read_page(btree, fileno, offset, child, NULL, NULL) 
              && iobtree_print_page(btree, child, level + 1)) {

            } else {
                assert(!CRASH);
                return 0;
            }
        }
    }
    free(child);

    return 1;
}

int iobtree_print_index(struct iobtree *btree) {
    return iobtree_print_page(btree, btree->root, 0);
}

int iobtree_print_btree(struct iobtree *btree) {
    struct page_iter iter;
    struct page *page;
    int ret;

    /* need to print the pages in the btree by iterating over them */

    if (!(iter.state = malloc(sizeof(*iter.state)))) {
        return 0;
    }

    iter.size = 1;
    iter.capacity = 1;
    iter.state[0].curr = btree->root;
    iter.state[0].state = 0;
    iter.state[0].term = 0;

    page = btree->root;

    while ((ret = page_iter_next(btree, &iter, FILTER_NONE, &page)) 
      == IOBTREE_OK) {
        printf("page %u %lu level %u %s\n", page->h.fileno, page->h.offset, 
          iter.size, BTBUCKET_LEAF(page->mem, btree->pagesize) ? "leaf": "");
    }

    free(iter.state);

    if (ret == IOBTREE_ITER_FINISH) {
        return 1;
    } else {
        assert(!CRASH);
        return 0;
    }
}

unsigned int iobtree_pages(struct iobtree *btree, unsigned int *leaves, 
  unsigned int *nodes) {
    struct page_iter iter;
    struct page *page;
    unsigned int l = 0,
                 n = 0,
                 p = 0;
    enum iobtree_ret iterret;

    /* need to iterate over pages in the index */

    if (!(iter.state = malloc(sizeof(*iter.state)))) {
        return 0;
    }

    iter.size = 1;
    iter.capacity = 1;
    iter.state[0].curr = btree->root;
    iter.state[0].state = 0;
    iter.state[0].term = 0;

    page = btree->root;

    while ((iterret = page_iter_next(btree, &iter, FILTER_NONE, &page)) 
      == IOBTREE_OK) {
        p++;
        if (BTBUCKET_LEAF(page->mem, btree->pagesize)) {
            l++;
        } else {
            n++;
        }
    }

    free(iter.state);

    if (iterret == IOBTREE_ITER_FINISH) {
        if (leaves) {
            *leaves = l;
        }
        if (nodes) {
            *nodes = n;
        }
        return p;
    } else {
        assert(!CRASH);
        return 0;
    }
}

unsigned int iobtree_pagesize(struct iobtree *btree) {
    return btree->pagesize;
}

unsigned int iobtree_levels(struct iobtree *btree) {
    return btree->levels;
}

