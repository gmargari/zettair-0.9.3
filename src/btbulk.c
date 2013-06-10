/* btbulk.c implements a bulk-loading algorithm for bulk b-tree
 * construction (technically a b+-tree).  Bulk loading is a fairly 
 * straight-forward affair in concept.  Since you accept sorted input 
 * entries they naturally form b-tree leaves by fitting them into buckets.  
 * The nodes can then be formed by pushing up the first entry in each
 * bucket into the next level to form index entries in the internal
 * nodes, and continuing this process until we form only one node,
 * which is the root.  A proper description of this process should be
 * given in any good database book, and Database Management Systems by
 * Ramakrishnan has a good section on it on page 104 (2nd ed).
 *
 * This algorithm also has the attractive property that it only
 * requires that one bucket for each level of the b-tree being created
 * be held in memory at any one time, which is essential for
 * scalability.  We can accomodate this fairly easily in the
 * algorithm, since we're only writing to one bucket on each level at
 * a time.  However, the main complication comes in that we want to
 * produce leaves that contain a pointer to its right sibling.  When
 * we're processing a stream of insertions, we want to write out each
 * b-tree leaf as it becomes full, but we don't know where its right
 * sibling is going to be placed until the next bucket becomes full as
 * well.  Our solution to that is to buffer them in memory until we can resolve
 * the threading.
 *
 * Another difficulty with bulk-loading is propagating a bucket
 * address and term up the index of the btree, since if we try to insert 
 * when the btree bucket is first created, the term is known but the
 * address isn't, and vice-versa if we try to insert when a bucket is
 * being written out.  The solution here is to allocate space in the
 * parent bucket when the child bucket is created, keep the pointer
 * around (the parent bucket has to be buffered until its full, which
 * won't happen until anther child bucket is created) and then fill in
 * the space once we decide to write out the child bucket, placing it
 * within the index. 
 *
 * Other problems with bulk loading come when considering additional
 * b-tree compression hueristics like prefix b-trees (determining the
 * optimal splitting point is a bit tricky while processing a stream
 * of updates).  We don't handle that yet, i'll talk about it when we
 * do.
 *
 * written nml 2003-05-21
 *
 */

#include "firstinclude.h"

#include "btbulk.h"

#include "bit.h"
#include "bucket.h"
#include "btbucket.h"
#include "_btbucket.h"
#include "mem.h"
#include "_mem.h"

#include <assert.h>
#include <stdlib.h>

#define NO_LAST_LEAF (-1)

/* structure representing one btree bucket held in memory during bulk
 * loading */
struct btbulk_bucket {
    struct btbulk_bucket *parent,     /* bucket that is next level up */
                         *child;      /* bucket that is next level down */
    void *parent_space;               /* space reserved in parent bucket for 
                                       * locaion of this bucket when we find 
                                       * out where it goes */
    unsigned int used;                /* number of bytes used in bucket */
    char mem[1];                      /* memory content of this bucket (struct 
                                       * hack) */
};

/* states that the bulk insertion can be in */
enum btbulk_states {
    STATE_ERR,                        /* an error has occurred */
    STATE_INSERT,                     /* ready to insert another entry */
    STATE_WRITE,                      /* output of bucket required */
    STATE_NEW,                        /* bucket needs to be initialised */
    STATE_FINISH                      /* algorithm is finishing */
};

struct btbulk_state {
    enum btbulk_states state;         /* current state of bulk construction */
    struct btbulk_bucket *curr;       /* current btree bucket */
    struct btbulk_bucket *btree;      /* pointer to bottom level of btree */
    unsigned int pagesize;            /* pagesize of btree buckets */
    unsigned long int maxfilesize;    /* maximum size of a file */
    float fill;                       /* btree fill factor */
    int leaf_strategy;                /* btree leaf node bucket strategy */
    int node_strategy;                /* btree internal node bucket strategy */
    unsigned int levels;              /* btree levels */
    unsigned int lastleaf;            /* pointer to last leaf stored in outbuf, 
                                       * so that we can thread it to the next 
                                       * leaf.  Contains NO_LAST_LEAF if 
                                       * absent. */

    float overhead;                   /* estimate of overhead for each bucket */
    double used;                      /* total bytes used in finished buckets */
    double total;                     /* total bytes in finished buckets */

    /* output buffer, so that we don't have to perform one write per bucket.
     * Also allows us to thread leaf buckets together.  Note that we need at
     * least as many pages in buffer as there are levels to do threading. */
    char *outbuf;                     /* circular output buffer */
    unsigned int outbuf_capacity;     /* number of b-tree pages that can be 
                                       * stored in outbuf */
    unsigned int outbuf_size;         /* number of b-tree pages that are 
                                       * currently stored in outbuf */
    unsigned int outbuf_start;        /* current start of buffer */
};

struct btbulk *btbulk_new(unsigned int pagesize, unsigned long int maxfilesize,
  int leaf_strategy, int node_strategy, float fill_factor, 
  unsigned int buffer_pages, struct btbulk *space) {
    unsigned int size;

    if (buffer_pages == 0) {
        buffer_pages = 1;
    }

    if ((space->state = malloc(sizeof(*space->state))) 
      && (space->state->curr = space->state->btree
        = malloc(sizeof(*space->state->curr) + pagesize))
      && (space->state->outbuf = malloc(pagesize * buffer_pages))) {

        space->state->pagesize = pagesize;
        if ((fill_factor > 0.0) && (fill_factor <= 1.0)) {
            space->state->fill = fill_factor;
        } else {
            space->state->fill = 1.0;
        }
        space->state->pagesize = pagesize;
        space->state->leaf_strategy = leaf_strategy;
        space->state->node_strategy = node_strategy;
        space->state->maxfilesize = maxfilesize;
        space->state->state = STATE_INSERT;
        space->state->btree->parent = NULL;
        space->state->btree->child = NULL;
        space->state->btree->parent_space = NULL;
        space->state->outbuf_capacity = buffer_pages;
        space->state->outbuf_size = space->state->outbuf_start = 0;
        space->state->lastleaf = NO_LAST_LEAF;
        space->state->levels = 1;
        space->state->overhead = 0.2F; /* initial estimate, will recalculate 
                                        * when we have information */
        space->state->used = space->state->total = 0;

        /* initialise current bucket */
        size = 0;
        btbucket_new(space->state->btree->mem, pagesize, -1, -1, 1, 
          "", &size);
        bucket_new(BTBUCKET_BUCKET(space->state->btree->mem), 
          BTBUCKET_SIZE(space->state->btree->mem, pagesize), 
          leaf_strategy);
        return space;
    } else {
        if (space->state) {
            if (space->state->curr) {
                free(space->state->curr);
            }
            free(space->state);
        }

        return NULL;
    }
}

void btbulk_delete(struct btbulk *bulk) {
    struct btbulk_bucket *bucket = bulk->state->btree,
                         *next;

    /* iterate to bottom of btree bucket list (shouldn't be necessary, but we'll
     * do it anyway) */
    while (bucket->child) {
        bucket = bucket->child;
    }

    /* iterate back up, freeing everything */
    while (bucket) {
        next = bucket->parent;
        free(bucket); 
        bucket = next; 
    }

    if (bulk->state->outbuf) {
        free(bulk->state->outbuf);
    }

    free(bulk->state);
    return;
}

/* internal function to calculate where a page will land given current position
 * and buffering information */
static unsigned long int location(unsigned int fileno, unsigned long int offset,
  unsigned long int maxfilesize, unsigned int pagesize, unsigned int pages, 
  unsigned int *dst_fileno) {

    /* predict where this page will land */
    assert(offset <= maxfilesize);
    while (offset + pagesize * pages > maxfilesize - pagesize) {
        fileno++;
        offset = 0;
        pages -= (maxfilesize - offset) / pagesize;
    }

    *dst_fileno = fileno;
    return offset + pagesize * pages;
}

/* internal function to try to make room in the buffer by shuffling it around.
 * Returns _OK if space was made, else returns _WRITE */
static enum btbulk_ret shuffle_buffer(struct btbulk *bulk) {
    /* if we're using shuffle_buffer regularly, the circular buffer should 
     * never wrap */
    assert(bulk->state->outbuf_size + bulk->state->outbuf_start 
      <= bulk->state->outbuf_capacity);

    if (bulk->state->outbuf_start) {
        /* shuffle circular buffer to ensure that it starts at the physical 
         * start.  This costs us some time moving memory around, but ultimately 
         * saves us file writes, and so should be an efficiency win */
        memmove(bulk->state->outbuf, &bulk->state->outbuf[
            bulk->state->pagesize * bulk->state->outbuf_start], 
          bulk->state->outbuf_size * bulk->state->pagesize);

        bulk->state->outbuf_start = 0;
        return BTBULK_OK;
    } else {
        return BTBULK_WRITE;
    }
}

/* internal function to decide how much stuff to output */
static enum btbulk_ret output(struct btbulk *bulk) {
    assert(bulk->state->outbuf_size);
    assert(!bulk->state->outbuf_start);  /* should call shuffle_buffer first */
    bulk->output.write.next_out 
      = &bulk->state->outbuf[bulk->state->outbuf_start * bulk->state->pagesize];

    /* note that avail_out currently contains number of *pages*, not bytes */
    bulk->output.write.avail_out = bulk->state->outbuf_size;

    /* check that output region doesn't exceed circular limit */
    if (bulk->state->outbuf_start + bulk->state->outbuf_size 
      > bulk->state->outbuf_capacity) {
        bulk->output.write.avail_out 
          = bulk->state->outbuf_capacity - bulk->state->outbuf_start;
    }

    /* check that output region doesn't cross files */
    if (bulk->offset > bulk->state->maxfilesize 
      - bulk->state->pagesize * bulk->output.write.avail_out) {
        /* note that this will set avail_out to 0 if we're at the end of the
         * file */
        bulk->output.write.avail_out 
          = (bulk->state->maxfilesize - bulk->offset) / bulk->state->pagesize;
    }

    /* check that we're not outputting the last leaf */
    if (bulk->state->lastleaf != NO_LAST_LEAF) {
        if (bulk->output.write.avail_out > bulk->state->lastleaf) {
            bulk->output.write.avail_out = bulk->state->lastleaf;
        }

        /* keep lastleaf as an offset number of pages from the start of the 
         * circular buffer */
        bulk->state->lastleaf -= bulk->output.write.avail_out;
    }

    bulk->state->outbuf_size -= bulk->output.write.avail_out;
    bulk->state->outbuf_start += bulk->output.write.avail_out;
    bulk->state->outbuf_start %= bulk->state->outbuf_capacity;
    bulk->output.write.avail_out *= bulk->state->pagesize; /* pages to bytes */

    if (bulk->output.write.avail_out) {
        return BTBULK_WRITE;
    } else {
        /* should be at the end of the file */
        assert(bulk->offset + bulk->state->pagesize > bulk->state->maxfilesize 
          - bulk->output.write.avail_out);
        return BTBULK_FLUSH;
    }
}

int btbulk_insert(struct btbulk *bulk) {
    struct btbulk_bucket *curr;
    int toobig;
    unsigned int size,
                 fileno,
                 pos;
    unsigned long int offset;

    /* jump to correct state */
    switch (bulk->state->state) {
    case STATE_INSERT: goto insert_label;
    case STATE_WRITE: goto write_label;
    case STATE_NEW: goto new_label;
    default: goto err_label;
    }

/* insert an entry into the bottom bucket */
insert_label:
    /* write an entry into the vocab, checking for appropriate fill rate */
    if (((bulk->state->fill < 1.0) 
      && ((bulk->state->btree->used / (float) bulk->state->pagesize) 
        + bulk->state->overhead > bulk->state->fill))
      || !(bulk->output.ok.data 
        = bucket_append(BTBUCKET_BUCKET(bulk->state->btree->mem), 
            BTBUCKET_SIZE(bulk->state->btree->mem, bulk->state->pagesize),
            bulk->state->leaf_strategy, bulk->term, 
            bulk->termlen, bulk->datasize, &toobig))) {

        /* have to page btree bucket out and start a new one */
        bulk->state->curr = bulk->state->btree;
        goto write_label;
    }
    bulk->state->btree->used += bulk->termlen + bulk->datasize;

    bulk->state->state = STATE_INSERT;
    return BTBULK_OK;

write_label:
    /* page out the current btree bucket and start a new one */
    curr = bulk->state->curr;

    /* lack of parent space allocation indicates that we need to push up a 
     * new level of the btree */
    if (!curr->parent_space) {
        assert(!curr->parent);
        if ((curr->parent 
          = malloc(sizeof(*curr->parent) + bulk->state->pagesize))) {
            curr->parent->parent = NULL;
            curr->parent->parent_space = NULL;
            curr->parent->used = btbucket_entry_size();
            size = 0;
            btbucket_new(curr->parent->mem, bulk->state->pagesize, 
              -1, -1, 0, "", &size);
            bucket_new(BTBUCKET_BUCKET(curr->parent->mem), 
              BTBUCKET_SIZE(curr->parent->mem, bulk->state->pagesize), 
              bulk->state->node_strategy);
            curr->parent->child = curr;

            /* insert NULL entry */
            if ((curr->parent_space 
              = bucket_alloc(BTBUCKET_BUCKET(curr->parent->mem),
                BTBUCKET_SIZE(curr->parent->mem, bulk->state->pagesize),
                bulk->state->node_strategy, "", 0, btbucket_entry_size(),  
                &toobig, NULL))) {

                /* allocation succeded, will fill it in below */
                bulk->state->levels++;
            } else {
                goto err_label;
            }
        } else {
            /* couldn't allocate */
            goto err_label;
        }

        /* ensure we've got enough buffer space for all levels of tree */
        if (bulk->state->outbuf_capacity <= bulk->state->levels) {
            unsigned int pgsize = bulk->state->pagesize;
            char *oldbuf = bulk->state->outbuf;
            char *newbuf 
              = malloc((bulk->state->levels + 1) * bulk->state->pagesize);

            /* note that we can't just realloc, because that may insert space
             * into the occupied portion of the circular buffer.  This is
             * probably the easiest way to deal with the problem */

            if (newbuf) {
                if (bulk->state->outbuf_start + bulk->state->outbuf_size 
                  > bulk->state->outbuf_capacity) {
                    unsigned int split = bulk->state->outbuf_capacity 
                      - bulk->state->outbuf_start;

                    /* circular buffer in two pieces */
                    memcpy(newbuf, &oldbuf[bulk->state->outbuf_start * pgsize], 
                      split * pgsize);
                    memcpy(&newbuf[split * pgsize], oldbuf, 
                      (bulk->state->outbuf_capacity - split) * pgsize);
                } else {
                    /* circular buffer in one piece */
                    memcpy(newbuf, &oldbuf[bulk->state->outbuf_start * pgsize], 
                      bulk->state->outbuf_size * pgsize);
                }
                bulk->state->outbuf_start = 0;
                free(oldbuf);
                bulk->state->outbuf = newbuf;
                bulk->state->outbuf_capacity = bulk->state->levels + 1;
            } else {
                /* couldn't allocate */
                goto err_label;
            }
        }
    }

    /* predict where this page will land */
    offset = location(bulk->fileno, bulk->offset, 
        bulk->state->maxfilesize, bulk->state->pagesize, 
        bulk->state->outbuf_size, &fileno);

    /* write address into current bucket parent space pointer */
    assert(((char *) curr->parent_space >= curr->parent->mem) 
      && ((char *) curr->parent_space 
        <= curr->parent->mem + bulk->state->pagesize));
    BTBUCKET_SET_ENTRY(curr->parent_space, fileno, offset);

    if (!curr->child && (bulk->state->lastleaf != NO_LAST_LEAF)) {
        /* need to thread last leaf to where this one will go */
        btbucket_set_sibling(&bulk->state->outbuf[
          ((bulk->state->outbuf_start + bulk->state->lastleaf) 
            % bulk->state->outbuf_capacity) * bulk->state->pagesize], 
          bulk->state->pagesize, fileno, offset);
        bulk->state->lastleaf = NO_LAST_LEAF;
    }

    /* ensure that there's space in the output buffer */
    shuffle_buffer(bulk);
    if (bulk->state->outbuf_size >= bulk->state->outbuf_capacity) {
        bulk->state->state = STATE_WRITE; /* note that redo'ing this state 
                                           * won't hurt */
        return output(bulk);
    }

    /* have recorded location, now write bucket into buffer */
    assert(bulk->state->outbuf_size < bulk->state->outbuf_capacity);
    pos = (bulk->state->outbuf_start + bulk->state->outbuf_size) 
      % bulk->state->outbuf_capacity;
    memcpy(&bulk->state->outbuf[pos * bulk->state->pagesize], 
        curr->mem, bulk->state->pagesize);
    if (!curr->child) {
        /* record last leaf position */
        bulk->state->lastleaf = bulk->state->outbuf_size; 
    }
    bulk->state->outbuf_size++;
    goto new_label;

/* old bucket has been paged out, initialise new bucket and get back to
 * insertion */
new_label:

    curr = bulk->state->curr;

    /* initialise new bucket, recalculating overhead statistic along the way */

    bulk->state->total += bulk->state->pagesize;
    curr->used = 0;
    if (BTBUCKET_LEAF(curr->mem, bulk->state->pagesize)) {
        /* its a leaf */
        bulk->state->used 
          += bulk->state->pagesize - bucket_unused(BTBUCKET_BUCKET(curr->mem), 
              BTBUCKET_SIZE(curr->mem, bulk->state->pagesize), 
              bulk->state->leaf_strategy);
        size = 0;
        btbucket_new(curr->mem, bulk->state->pagesize, 
          -1, -1, 1, "", &size);
        bucket_new(BTBUCKET_BUCKET(curr->mem), 
          BTBUCKET_SIZE(curr->mem, bulk->state->pagesize), 
          bulk->state->leaf_strategy);
    } else {
        /* its a node */
        bulk->state->used 
          += bulk->state->pagesize - bucket_unused(BTBUCKET_BUCKET(curr->mem), 
              BTBUCKET_SIZE(curr->mem, bulk->state->pagesize), 
              bulk->state->node_strategy);
        size = 0;
        btbucket_new(curr->mem, bulk->state->pagesize, 
          -1, -1, 0, "", &size);
        bucket_new(BTBUCKET_BUCKET(curr->mem), 
          BTBUCKET_SIZE(curr->mem, bulk->state->pagesize), 
          bulk->state->node_strategy);
    }
    bulk->state->overhead 
      = (float) (1 - bulk->state->used / bulk->state->total);
    assert(bulk->state->overhead >= 0.0 && bulk->state->overhead < 1.0);

    /* propagate split up the tree and allocation back down it (look
     * carefully, this code is a little mind-bending) */
    while (curr) {
        assert(curr->parent);
        if (((bulk->state->fill < 1.0) 
          && ((curr->parent->used / (float) bulk->state->pagesize) 
            + bulk->state->overhead > bulk->state->fill))
          || (!(curr->parent_space 
            = bucket_alloc(BTBUCKET_BUCKET(curr->parent->mem), 
              BTBUCKET_SIZE(curr->parent->mem, bulk->state->pagesize),
              bulk->state->node_strategy, bulk->term, bulk->termlen,
              btbucket_entry_size(), &toobig, NULL)))) {

            /* couldn't allocate space from parent bucket, have to
             * perform the same procedure on it */
            bulk->state->curr = curr->parent;
            goto write_label;
        }
        curr->parent->used += btbucket_entry_size() + bulk->termlen;
        curr = curr->child;
    }
    goto insert_label;

err_label:
    bulk->state->state = STATE_ERR;
    return BTBULK_ERR;
}

int btbulk_finalise(struct btbulk *bulk, unsigned int *root_fileno, 
  unsigned long int *root_offset) {
    struct btbulk_bucket *curr;
    unsigned int fileno,
                 pos;
    unsigned long int offset;

    if (bulk->state->state == STATE_INSERT) {
        /* need to initialise algorithm to the lowest level of btree */
        curr = bulk->state->curr = bulk->state->btree;

        /* predict where this page will land */
        offset = location(bulk->fileno, bulk->offset,
            bulk->state->maxfilesize, bulk->state->pagesize, 
            bulk->state->outbuf_size, &fileno);

        /* thread this final leaf page to itself */
        btbucket_set_sibling(curr->mem, bulk->state->pagesize, 
          fileno, offset);

        if (bulk->state->lastleaf != NO_LAST_LEAF) {
            /* thread previous leaf to this one */
            btbucket_set_sibling(&bulk->state->outbuf[
              ((bulk->state->outbuf_start + bulk->state->lastleaf) 
                % bulk->state->outbuf_capacity) * bulk->state->pagesize], 
              bulk->state->pagesize, fileno, offset);
            bulk->state->lastleaf = NO_LAST_LEAF;
        }
    } else if (bulk->state->state == STATE_FINISH) {
        /* pick up where we left off */
        curr = bulk->state->curr;
    } else {
        /* attempt to start from some weird state */
        return BTBULK_ERR;
    }

    bulk->state->state = STATE_FINISH;

    /* write out btree buckets */
    while (curr) {
         /* ensure that there's space in the output buffer */
         shuffle_buffer(bulk);
         if (bulk->state->outbuf_size >= bulk->state->outbuf_capacity) {
             return output(bulk);
         }

        /* predict where this page will land */
        offset = location(bulk->fileno, bulk->offset, 
            bulk->state->maxfilesize, bulk->state->pagesize, 
            bulk->state->outbuf_size, &fileno);

        if (curr->parent_space) {
            /* encode pointer into parent bucket */
            assert(((char *) curr->parent_space >= curr->parent->mem)
              && ((char *) curr->parent_space 
                <= curr->parent->mem + bulk->state->pagesize));
            BTBUCKET_SET_ENTRY(curr->parent_space, fileno, offset);
        } else {
            /* this is the root node */
            *root_fileno = fileno;
            *root_offset = offset;
        }

        /* write bucket into buffer */
        assert(bulk->state->outbuf_size < bulk->state->outbuf_capacity);
        pos = (bulk->state->outbuf_start + bulk->state->outbuf_size) 
          % bulk->state->outbuf_capacity;
        memcpy(&bulk->state->outbuf[pos * bulk->state->pagesize], 
            curr->mem, bulk->state->pagesize);
        bulk->state->outbuf_size++;

        /* iterate up to next btree bucket */
        curr = bulk->state->curr = curr->parent;
    }

    shuffle_buffer(bulk);
    if (bulk->state->outbuf_size) {
        return output(bulk);
    }
    assert(bulk->state->lastleaf == NO_LAST_LEAF);
    assert(bulk->state->outbuf_size == 0);
    return BTBULK_FINISH;
}

struct btbulk_read_state {
    unsigned int fileno;             /* fileno of current btree bucket */
    unsigned long int offset;        /* offset of current btree bucket */
    unsigned int term;               /* term index in current btree bucket */
    const char *btbucket;            /* cached pointer to current bucket */
    unsigned long int inpos;         /* input offset_in when we cached current 
                                      * btbucket pointer */
    unsigned int inlen;              /* input avail_in when we cached current 
                                      * btbucket pointer */
    unsigned int pagesize;           /* btree bucket size */
    int strategy;                    /* btree leaf node strategy */
};

struct btbulk_read *btbulk_read_new(unsigned int pagesize, 
  int strategy, unsigned int first_page_fileno, 
  unsigned long int first_page_offset, struct btbulk_read *space) {

    if ((space->state = malloc(sizeof(*space->state)))) {
        space->next_in = NULL;
        space->avail_in = 0;
        space->fileno_in = 0;
        space->offset_in = 0;
        space->output.ok.term = NULL;
        space->output.ok.data = NULL;
        space->output.ok.datalen = 0;
        space->output.ok.termlen = 0;

        space->state->fileno = first_page_fileno;
        space->state->offset = first_page_offset;
        space->state->btbucket = NULL;
        space->state->strategy = strategy;
        space->state->pagesize = pagesize;
        return space;
    } else {
        return NULL;
    }
}

void btbulk_read_delete(struct btbulk_read *bulk) {
    free(bulk->state);
}

enum btbulk_ret btbulk_read(struct btbulk_read *bulk) {
    unsigned int fileno;
    unsigned long int offset;

    /* ensure that we're got the correct page */
    do {
        if (!bulk->state->btbucket
          || (bulk->fileno_in != bulk->state->fileno)
          || (bulk->offset_in != bulk->state->inpos)
          || (bulk->avail_in != bulk->state->inlen)) {
            /* correct bucket not in pointer, search input buffer for it */
            if ((bulk->fileno_in == bulk->state->fileno) 
              && (bulk->offset_in <= bulk->state->offset)
              && (bulk->avail_in >= bulk->state->pagesize)
              && (bulk->offset_in + bulk->avail_in - bulk->state->pagesize 
                >= bulk->state->offset)) {

                /* bucket is in input buffer, save pointer to it */ 
                bulk->state->inpos = bulk->offset_in;
                bulk->state->inlen = bulk->avail_in;
                bulk->state->btbucket 
                  = bulk->next_in + bulk->state->offset - bulk->offset_in;
                bulk->state->term = 0;
            } else {
                bulk->output.read.fileno = bulk->state->fileno;
                bulk->output.read.offset = bulk->state->offset;
                return BTBULK_READ;
            }
        }

        /* should now have the correct leaf node at btbucket pointer */
        assert(bulk->state->btbucket);
        assert(BTBUCKET_LEAF(bulk->state->btbucket, bulk->state->pagesize));

        if ((bulk->output.ok.term 
          = bucket_term_at(BTBUCKET_BUCKET(bulk->state->btbucket), 
              BTBUCKET_SIZE(bulk->state->btbucket, bulk->state->pagesize), 
            bulk->state->strategy, bulk->state->term, &bulk->output.ok.termlen, 
            (void **) &bulk->output.ok.data, &bulk->output.ok.datalen))) {

            /* got next term, return */
            bulk->state->term++;
            return BTBULK_OK;
        } else {
            /* no more terms in this bucket, go to next leaf bucket */
            fileno = bulk->state->fileno;
            offset = bulk->state->offset;

            btbucket_sibling((void *) bulk->state->btbucket, 
              bulk->state->pagesize, &bulk->state->fileno, 
              &bulk->state->offset);
            
            /* invalidate saved pointer, since we need to move to a new 
             * bucket */
            bulk->state->btbucket = NULL;
        }
    } while ((fileno != bulk->state->fileno) 
      || (offset != bulk->state->offset));

    /* iteration finished */
    return BTBULK_FINISH;
}

unsigned long int btbulk_read_offset(struct btbulk_read *bulk) {
    return bulk->state->offset;
}

