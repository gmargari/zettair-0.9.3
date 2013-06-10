/* merge.c implements a final merging procedure that puts inverted
 * lists into a vocabulary b-tree, and a set of files and an
 * intermediate merging procedure that produces a large intermediate file from
 * small ones
 *
 * A problem that this module has to solve is the difference between
 * stream and block output, in that we want our inverted lists output
 * as (more or less) a stream and everything else as a block.  The
 * problem with this is that we have to know in advance where some of
 * our writes are going to end up.  Unfortunately, the maximum file
 * size limit means that we can't assume that everything is just going
 * to end up in one big stream.  There are only two approaches to
 * solve this problem: post-failure correction and prevention.  This
 * module implements prevention (a maximum file size limit) because
 * its simpler and works better with other limitations already in the
 * search engines (like a maximum offset of ULONG_MAX).
 *
 * written nml 2003-02-11
 *
 */

#include "firstinclude.h"

#include "merge.h"

#include "def.h"
#include "heap.h"
#include "bit.h"
#include "btbulk.h"
#include "bucket.h"
#include "btbucket.h"
#include "storagep.h"
#include "str.h"
#include "vec.h"
#include "vocab.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>  /* for memmove */
#include <stdlib.h>

/* this specifies the point where we are willing to perform an extra write to
 * avoid copying this amount of extra data */
#define WRITE_POINT (20 * 1024 * 1024)  
/* estimate 2GBps mem bandwidth and 10ms write times gives us about 20MB
 * tradeoff point (FIXME: experiment with me to find the best point) */

/* merge states */
enum merge_states {
    STATE_ERR = -1,
    STATE_START = 0,
    STATE_FINISHED = 1,
    STATE_READENTRY = 2,
    STATE_READENTRY_TERM = 3,
    STATE_READENTRY_DOCS = 4,
    STATE_READENTRY_OCCURS = 36,
    STATE_READENTRY_LAST = 5,
    STATE_READENTRY_SIZE = 6,
    STATE_READENTRY_FIRST = 7,
    STATE_SELECT_INPUTS = 8,
    STATE_PREFINISH = 9,
    STATE_WRITE_END = 10,
    STATE_ASSIGN_VOCAB = 11,
    STATE_WRITE_VOCAB_FIRST = 12,
    STATE_WRITE_VOCAB_VECTOR = 13,
    STATE_WRITE_VOCAB_END = 14,
    STATE_WRITE_FILE_FIRST = 19,
    STATE_WRITE_FILE_VECTOR = 20,
    STATE_WRITE_FILE_OVERALLOC = 21,
    STATE_WRITE_FILE_END = 22,
    STATE_WRITE_BTREE = 24,
    STATE_FLUSH_NEWFILE = 26,
    STATE_FLUSH_SWITCH = 27,
    STATE_PRE_WRITEENTRY = 28,
    STATE_WRITEENTRY = 29,
    STATE_WRITEENTRY_TERM = 30,
    STATE_WRITEENTRY_DOCS = 31,
    STATE_WRITEENTRY_OCCURS = 37,
    STATE_WRITEENTRY_LAST = 32,
    STATE_WRITEENTRY_SIZE = 33,
    STATE_WRITEENTRY_FIRST = 34,
    STATE_WRITEENTRY_VECTOR = 35 
};

/* structure to represent inputs with enough information read in to
 * determine sorting order of inputs */
struct input {
    struct merge_input *input;               /* user provided input that this 
                                              * is associated with */
    char *term;                              /* next merge term */
    unsigned int termlen;                    /* how long the next term is */
    unsigned int docs;                       /* number of documents next term 
                                              * occurs in */
    unsigned int occurs;                     /* number of times next term 
                                              * occurs */
    unsigned int last;                       /* last docno in the postings for 
                                              * next term */
    unsigned int size;                       /* size of postings for next term 
                                              * (note: *doesn't* include size 
                                              * of first value) */
    unsigned int first;                      /* first docno in the postings for 
                                              * the next term */
};

#define BUFSIZE 8

/* internal state of final merge.  The difference between this and the
 * intermediate merge is that the final merge needs to fill buckets and bulk
 * load a btree */
struct merge_final_state {
    enum merge_states state;             /* state of merge */
    enum merge_states next_state;        /* next state we'll traverse to */
    unsigned int index;                  /* index of input we're working with */
    unsigned int count;                  /* count of stuff we've done in state*/
    void *addr;                          /* current writing address */
    char buf[BUFSIZE];                   /* small buffer to hold vbytes */

    struct input *input;                 /* items for heap */
    struct input **iinput;               /* indirected input heap */
    unsigned int inputs;                 /* number of items on input heap */
    unsigned int finished;               /* number of inputs that have 
                                          * finished */

    void *out;                           /* internal output buffer */
    unsigned int outsize;                /* size of internal output buffer */

    struct btbulk btree;                 /* vocab, being bulk-loaded */

    struct storagep *storage;            /* storage parameters */

    struct vocab_vector vv;              /* vocab entry we're currently working 
                                          * with */

    void *opaque;                        /* opaque data for (de)allocation 
                                          * functions */
    void *(*allocfn)                     /* allocation function */
      (void *opaque, unsigned int size);
    void (*freefn)                       /* deallocation function */
      (void *opaque, void *mem); 

    unsigned int root_fileno;            /* final root file number */
    unsigned long int root_offset;       /* final root offset */
    unsigned long int dterms;            /* number of terms merged */
    unsigned int terms_high;             /* high word of total terms */
    unsigned int terms_low;              /* low word of total terms */
    unsigned int prev_index;             /* index before reloading */
};

/* internal state of the intermediate merge */
struct merge_inter_state {
    enum merge_states state;             /* state of merge */
    enum merge_states next_state;        /* next state we'll traverse to */
    unsigned int index;                  /* index of input we're working with */
    unsigned int count;                  /* count of stuff we've done in state*/
    void *addr;                          /* current writing address */
    char buf[BUFSIZE];                   /* small buffer to hold vbytes */

    struct input *input;                 /* items for heap */
    struct input **iinput;               /* indirected input heap */
    unsigned int inputs;                 /* number of items on input heap */
    unsigned int finished;               /* number of inputs that have 
                                          * finished */

    void *out;                           /* internal output buffer */
    unsigned int outsize;                /* size of internal output buffer */

    struct vocab_vector vv;              /* vocab entry we're currently working 
                                          * with */

    void *opaque;                        /* opaque data for (de)allocation 
                                          * functions */
    void *(*allocfn)                     /* allocation function */
      (void *opaque, unsigned int size);
    void (*freefn)                       /* deallocation function */
      (void *opaque, void *mem); 

    /* XXX: hack starts here :o( */
    void *opaque_newfile;
    void (*newfile)(void *opaque);       /* call when we need a new file */
    unsigned long int filesize;          /* maximum size of file */
    unsigned long int currsize;          /* size of current file */

    unsigned int prev_index;             /* index before reloading */
    unsigned int max_termlen;            /* maximum length of a term */
};

static int iinput_cmp(const void *one, const void *two) {
    const struct input * const *iione = one,
                       * const *iitwo = two,
                       *ione = *iione,
                       *itwo = *iitwo;
    unsigned int i;

    if (ione->termlen < itwo->termlen) {
        for (i = 0; i < ione->termlen; i++) {
            if (ione->term[i] != itwo->term[i]) {
                return ((int) ione->term[i]) - itwo->term[i];
            }
        }

        return -1;
    } else if (ione->termlen > itwo->termlen) {
        for (i = 0; i < itwo->termlen; i++) {
            if (ione->term[i] != itwo->term[i]) {
                return ((int) ione->term[i]) - itwo->term[i];
            }
        }

        return 1;
    } else {
        assert(ione->termlen == itwo->termlen);
        for (i = 0; i < itwo->termlen; i++) {
            if (ione->term[i] != itwo->term[i]) {
                return ((int) ione->term[i]) - itwo->term[i];
            }
        }

        if (ione->first < itwo->first) {
            return -1;
        } else if (ione->first > itwo->first) {
            return 1;
        } else {
            return 0;
        }
    }
}

static void *merge_malloc(void *ptr, unsigned int size) {
    return malloc(size);
}

static void merge_free(void *ptr, void *mem) {
    free(mem);
}

int merge_final_new(struct merge_final *merger, void *opaque,
  void *(*allocfn)(void *opaque, unsigned int size),
  void (*freefn)(void *opaque, void *mem), struct storagep *storage,
  void *outbuf, unsigned int outbufsz) {
    unsigned int i,
                 j;

    if (!opaque && !allocfn && !freefn) {
        allocfn = merge_malloc;
        freefn = merge_free;
    }

    if ((merger->state = allocfn(opaque, sizeof(*merger->state)))
      && (merger->state->input 
        = allocfn(opaque, sizeof(*merger->state->input) * merger->inputs))
      && (merger->state->iinput 
        = allocfn(opaque, sizeof(struct input *) * merger->inputs))
      && (btbulk_new(storage->pagesize, storage->max_filesize, 
        storage->btleaf_strategy, storage->btnode_strategy, 1.0, 
        0, &merger->state->btree))) {

        for (i = 0; i < merger->inputs; i++) {
            merger->state->input[i].input = &merger->input[i];
            merger->state->iinput[i] = &merger->state->input[i];
            if (!(merger->state->input[i].term 
              = allocfn(opaque, storage->max_termlen + 1))) {
                for (j = 0; j + 1 < i; j++) {
                    freefn(opaque, merger->state->input[i].term);
                }
                freefn(opaque, merger->state->input);
                freefn(opaque, merger->state);
                return MERGE_ERR;
            }
        }
        merger->state->inputs = merger->inputs;
        merger->state->finished = 0;
        merger->state->storage = storage;

        merger->state->vv.attr = VOCAB_ATTRIBUTES_NONE;
        merger->state->vv.type = VOCAB_VTYPE_DOCWP;

        assert(storage->max_filesize > storage->pagesize);

        merger->state->opaque = opaque;
        merger->state->freefn = freefn;
        merger->state->allocfn = allocfn;

        merger->state->state = STATE_START;
        merger->state->next_state = STATE_ERR;
        merger->state->index = 0;

        merger->out.buf_out = merger->state->out = outbuf;
        merger->out.size_out = 0;
        merger->state->outsize = outbufsz;
        merger->out.fileno_out = 0;
        merger->out.offset_out = 0;
        
        merger->out_btree.buf_out = NULL;
        merger->out_btree.size_out = 0;
        merger->out_btree.fileno_out = 0;
        merger->out_btree.offset_out = 0;

        merger->state->dterms = 0;
        merger->state->terms_high = 0;
        merger->state->terms_low = 0;

        return MERGE_OK;
    } else {
        if (merger->state) {
            if (merger->state->input) {
                if (merger->state->iinput) {
                    freefn(opaque, merger->state->iinput);
                }
                freefn(opaque, merger->state->input);
            }
            freefn(opaque, merger->state);
        }
        return MERGE_ERR;
    }
}

int merge_inter_new(struct merge_inter *merger, void *opaque,
  void *(*allocfn)(void *opaque, unsigned int size),
  void (*freefn)(void *opaque, void *mem), 
  void *outbuf, unsigned int outbufsz, unsigned int max_termlen,
  void *opaque_newfile, void (*newfile)(void *opaque_newfile), 
  unsigned long int filesize) {
    unsigned int i,
                 j;

    if (!opaque && !allocfn && !freefn) {
        allocfn = merge_malloc;
        freefn = merge_free;
    }

    if ((merger->state = allocfn(opaque, sizeof(*merger->state)))
      && (merger->state->input 
        = allocfn(opaque, sizeof(*merger->state->input) * merger->inputs))
      && (merger->state->iinput 
        = allocfn(opaque, sizeof(struct input *) * merger->inputs))) {

        for (i = 0; i < merger->inputs; i++) {
            merger->state->input[i].input = &merger->input[i];
            merger->state->iinput[i] = &merger->state->input[i];
            if (!(merger->state->input[i].term 
              = allocfn(opaque, max_termlen + 1))) {
                for (j = 0; j + 1 < i; j++) {
                    freefn(opaque, merger->state->input[i].term);
                }
                freefn(opaque, merger->state->input);
                freefn(opaque, merger->state);
                return MERGE_ERR;
            }
        }
        merger->state->inputs = merger->inputs;
        merger->state->finished = 0;

        merger->state->opaque = opaque;
        merger->state->freefn = freefn;
        merger->state->allocfn = allocfn;

        merger->state->next_state = STATE_ERR;
        merger->state->state = STATE_START;
        merger->state->index = 0;

        merger->buf_out = merger->state->out = outbuf;
        merger->size_out = 0;
        merger->state->outsize = outbufsz;

        merger->state->filesize = filesize;
        merger->state->newfile = newfile;
        merger->state->opaque_newfile = opaque_newfile;

        merger->state->currsize = 0;
        merger->state->max_termlen = max_termlen;

        return MERGE_OK;
    } else {
        if (merger->state) {
            if (merger->state->input) {
                freefn(opaque, merger->state->input);
            }
            freefn(opaque, merger->state);
        }
        return MERGE_ERR;
    }
}

int merge_final_finish(struct merge_final *merger, unsigned int *root_fileno, 
  unsigned long int *root_offset, unsigned long int *dterms, 
  unsigned int *terms_high, unsigned int *terms_low) {
    if (merger->state->state == STATE_FINISHED) {
        *root_fileno = merger->state->root_fileno;
        *root_offset = merger->state->root_offset;
        *dterms = merger->state->dterms;
        *terms_high = merger->state->terms_high;
        *terms_low = merger->state->terms_low;
        return MERGE_OK;
    } else {
        return MERGE_ERR;
    }
}

void merge_inter_delete(struct merge_inter *merger) {
    unsigned int i;
    for (i = 0; i < merger->inputs; i++) {
        merger->state->freefn(merger->state->opaque, 
          merger->state->input[i].term);
    }

    merger->state->freefn(merger->state->opaque, merger->state->iinput);
    merger->state->freefn(merger->state->opaque, merger->state->input);
    merger->state->freefn(merger->state->opaque, merger->state);
    return;
}

void merge_final_delete(struct merge_final *merger) {

    unsigned int i;
    for (i = 0; i < merger->inputs; i++) {
        merger->state->freefn(merger->state->opaque, 
          merger->state->input[i].term);
    }

    btbulk_delete(&merger->state->btree);

    merger->state->freefn(merger->state->opaque, merger->state->iinput);
    merger->state->freefn(merger->state->opaque, merger->state->input);
    merger->state->freefn(merger->state->opaque, merger->state);
    return;
}

/* a few macros to help us out in the state machines */

/* macro to jump to state specified by enum.  Note that we can't use seperate
 * JUMP statements for merge_final and merge_inter because the common macros
 * (GETVBYTE, PUTVBYTE) need to JUMP */
#define JUMP(state)                                                           \
    switch (state) {                                                          \
    case STATE_START: goto start_label;                                       \
    case STATE_FINISHED: goto finished_label;                                 \
    case STATE_READENTRY: goto readentry_label;                               \
    case STATE_READENTRY_TERM: goto readentry_term_label;                     \
    case STATE_READENTRY_DOCS: goto readentry_docs_label;                     \
    case STATE_READENTRY_OCCURS: goto readentry_occurs_label;                 \
    case STATE_READENTRY_LAST: goto readentry_last_label;                     \
    case STATE_READENTRY_SIZE: goto readentry_size_label;                     \
    case STATE_READENTRY_FIRST: goto readentry_first_label;                   \
    case STATE_SELECT_INPUTS: goto select_inputs_label;                       \
    case STATE_PREFINISH: goto prefinish_label;                               \
    case STATE_WRITE_END: goto write_end_label;                               \
    case STATE_ASSIGN_VOCAB: goto assign_vocab_label;                         \
    case STATE_WRITE_VOCAB_FIRST: goto write_vocab_first_label;               \
    case STATE_WRITE_VOCAB_VECTOR: goto write_vocab_vector_label;             \
    case STATE_WRITE_VOCAB_END: goto write_vocab_end_label;                   \
    case STATE_WRITE_FILE_FIRST: goto write_file_first_label;                 \
    case STATE_WRITE_FILE_VECTOR: goto write_file_vector_label;               \
    case STATE_WRITE_FILE_OVERALLOC: goto write_file_overalloc_label;         \
    case STATE_WRITE_FILE_END: goto write_file_end_label;                     \
    case STATE_WRITE_BTREE: goto write_btree_label;                           \
    case STATE_FLUSH_NEWFILE: goto flush_newfile_label;                       \
    case STATE_FLUSH_SWITCH: goto flush_switch_label;                         \
    case STATE_PRE_WRITEENTRY: goto pre_writeentry_label;                     \
    case STATE_WRITEENTRY: goto writeentry_label;                             \
    case STATE_WRITEENTRY_TERM: goto writeentry_term_label;                   \
    case STATE_WRITEENTRY_DOCS: goto writeentry_docs_label;                   \
    case STATE_WRITEENTRY_OCCURS: goto writeentry_occurs_label;               \
    case STATE_WRITEENTRY_LAST: goto writeentry_last_label;                   \
    case STATE_WRITEENTRY_SIZE: goto writeentry_size_label;                   \
    case STATE_WRITEENTRY_FIRST: goto writeentry_first_label;                 \
    case STATE_WRITEENTRY_VECTOR: goto writeentry_vector_label;               \
    default: goto err_label;                                                  \
    }

/* macro to get a vbyte from an input stream, even if its split across
 * read boundaries */
#define GETVBYTE(var, state_, finish_state)                                   \
    if (!merger->state->count) {                                              \
        vec.pos = input->input->next_in;                                      \
        vec.end = vec.pos + input->input->avail_in;                           \
    } else {                                                                  \
        /* fill buffer */                                                     \
        vec.pos = merger->state->buf;                                         \
        if (input->input->avail_in + merger->state->count > BUFSIZE) {        \
            memcpy(&merger->state->buf[merger->state->count],                 \
              input->input->next_in, BUFSIZE - merger->state->count);         \
            vec.end = vec.pos + BUFSIZE;                                      \
        } else {                                                              \
            memcpy(&merger->state->buf[merger->state->count],                 \
              input->input->next_in,                                          \
              input->input->avail_in - merger->state->count);                 \
            vec.end = vec.pos + input->input->avail_in + merger->state->count;\
        }                                                                     \
    }                                                                         \
                                                                              \
    if ((len = vec_vbyte_read(&vec, &tmp))) {                                 \
        /* we should always use all of the buffer, because we                 \
         * shouldn't have buffered if not necessary */                        \
        assert(len > merger->state->count);                                   \
        input->input->next_in += len - merger->state->count;                  \
        input->input->avail_in -= len - merger->state->count;                 \
        (var) = tmp;                                                          \
    } else if (1) {                                                           \
        /* read failed, have to buffer (shouldn't happen after buffering a    \
         * first time) */                                                     \
        assert(merger->state->count == 0);                                    \
        assert(input->input->avail_in < BUFSIZE);                             \
                                                                              \
        /* need to buffer input and ask for more */                           \
        memcpy(merger->state->buf, input->input->next_in,                     \
          input->input->avail_in);                                            \
        merger->state->count = input->input->avail_in;                        \
        input->input->next_in += input->input->avail_in;                      \
        input->input->avail_in = 0;                                           \
        *index = merger->state->iinput[merger->state->index]                  \
          - merger->state->input;                                             \
        *next_read = 0;                                                       \
        merger->state->state = (state_);                                      \
        return MERGE_INPUT;                                                   \
    } else 

/* macro to put a vbyte to the output stream, flushing if necessary */
#define PUTVBYTE(var, state_)                                                 \
    if (vec_vbyte_len(var) <= merger->state->outsize - merger->out.size_out) {\
        vec.pos = merger->out.buf_out + merger->out.size_out;                 \
        vec.end = vec.pos + (merger->state->outsize - merger->out.size_out);  \
                                                                              \
        len = vec_vbyte_write(&vec, var);                                     \
        assert(len);                                                          \
        merger->out.size_out += len;                                          \
    } else if (1) {                                                           \
        merger->state->state = state_;                                        \
        return MERGE_OUTPUT;                                                  \
    } else

/* the final merge state machine is a bit tricky, it proceeds something like 
 * this:
 *
 * - read entry(ies)
 * - select lowest input(s)
 *   - if going to files
 *     - for each input in lowest set
 *       - write to file 
 *       - reload if not last and proceed to next
 *   - write vocab entry using last term
 *   - for each input left in lowest set
 *     - write to vocab if applicable
 *     - reload and proceed to next
 *
 * the prime complication here is that we need to write the vocab entry 
 * *after* we write to the files because otherwise the vocab entry 
 * might change (and hence change its length) after we've allocated space for 
 * it.  However, when we put the entry in the vocab, we need to write it after
 * the vocab entry :o( */

int merge_final(struct merge_final *merger, unsigned int *index, 
  unsigned int *next_read) {
    unsigned long int tmp;
    unsigned int i,
                 len;
    struct vec vec = {NULL, 0};
    struct input *input = merger->state->iinput[merger->state->index],
                 *smallest,
                 *next,
                 **tmpinput;
    int ret;

    /* jump to correct state */
    JUMP(merger->state->state);

start_label:
    /* need to read the first entry from each of the inputs */
    if (merger->state->index < merger->state->inputs) {
        input = merger->state->iinput[merger->state->index];
        merger->state->count = 0;
        merger->state->next_state = STATE_START;
        goto readentry_label;
    }

    /* heapify inputs */
    heap_heapify(merger->state->iinput, merger->state->inputs, 
      sizeof(*merger->state->iinput), iinput_cmp);

    goto select_inputs_label;

readentry_label:
    GETVBYTE(input->termlen, STATE_READENTRY, merger->state->next_state); 

    assert(input->termlen <= merger->state->storage->max_termlen);
    assert(input->termlen);

    /* fallthrough to readentry_term_label */
    merger->state->count = 0;
readentry_term_label:
    if (input->input->avail_in >= input->termlen - merger->state->count) {
        len = input->termlen - merger->state->count;
        memcpy(&input->term[merger->state->count], input->input->next_in, len);
        input->term[input->termlen] = '\0';
        input->input->next_in += len;
        input->input->avail_in -= len;
    } else {
        len = input->input->avail_in;
        memcpy(&input->term[merger->state->count], input->input->next_in, len);
        input->input->next_in += len;
        input->input->avail_in = 0;
        merger->state->count += len;
        merger->state->state = STATE_READENTRY_TERM;
        *index = merger->state->iinput[merger->state->index]
          - merger->state->input;
        *next_read = input->termlen - len;
        return MERGE_INPUT;
    }

    /* shouldn't contain control characters (they're a pretty sure sign we're 
     * reading postings instead) */
    for (i = 0; i < input->termlen; i++) {
        assert(!iscntrl(input->term[i]));
    }

    merger->state->count = 0;
    /* fallthrough to readentry_docs_label */
readentry_docs_label:
    GETVBYTE(input->docs, STATE_READENTRY_DOCS, STATE_ERR);

    merger->state->count = 0;
    /* fallthrough to readentry_occurs_label */
readentry_occurs_label:
    GETVBYTE(input->occurs, STATE_READENTRY_OCCURS, STATE_ERR);

    merger->state->count = 0;
    /* fallthrough to readentry_last_label */
readentry_last_label:
    GETVBYTE(input->last, STATE_READENTRY_LAST, STATE_ERR);

    merger->state->count = 0;
    /* fallthrough to readentry_size_label */
readentry_size_label:
    GETVBYTE(input->size, STATE_READENTRY_SIZE, STATE_ERR);

    merger->state->count = 0;
    /* fallthrough to readentry_first_label */
readentry_first_label:
    GETVBYTE(input->first, STATE_READENTRY_FIRST, STATE_ERR);
    input->size -= len;      /* subtract length of first value from size */
    merger->state->index++;

    /* jump to next state */
    JUMP(merger->state->next_state);

select_inputs_label:
    /* select the smallest inputs from the heap */

    assert(merger->state->inputs == merger->inputs - merger->state->finished);
    while ((tmpinput = heap_pop(merger->state->iinput, 
        &merger->state->inputs, sizeof(*merger->state->iinput), iinput_cmp))) {
        smallest = *tmpinput;

        /* keep popping things off of stack until we get to an entry
         * that has a different term or run out of items */
        while (merger->state->inputs 
          && (next = *((struct input **) heap_peek(merger->state->iinput, 
            merger->state->inputs, sizeof(*merger->state->iinput))))
          && (smallest->termlen == next->termlen) 
          && !str_ncmp(smallest->term, next->term, smallest->termlen)
          && heap_pop(merger->state->iinput, &merger->state->inputs, 
              sizeof(*merger->state->iinput), iinput_cmp)) ;

        /* we now have the (merger->inputs -
         * merger->state->finished - merger->state->inputs) elements
         * at the back of merger->state->input array that need to be written to
         * the file, in reverse order of occurrance */

        merger->state->index = merger->state->inputs;

        /* swap the order of occurrance (have to do this because when we insert
         * them back onto the heap they will be absorbed in this order).
         * note that if len is odd middle element is already correct */
        len = merger->inputs - merger->state->finished - merger->state->index;
        tmp = len / 2;  /* only need to swap first half of array */
        for (i = 0; i < tmp; i++) {
            void *el = merger->state->iinput[merger->state->index + i];
            merger->state->iinput[merger->state->index + i] 
              = merger->state->iinput[merger->state->index + len - (i + 1)];
            merger->state->iinput[merger->state->index + len - (i + 1)] = el;
        }

        /* form a vocabulary entry for this term */
        input = merger->state->iinput[merger->state->index];
        merger->state->vv.header.doc.docs = input->docs;
        merger->state->vv.header.doc.occurs = input->occurs;
        merger->state->vv.size = input->size + vec_vbyte_len(input->first);

        /* loop over the rest of the entries, altering first entry to have 
         * correct delta-encoded value with respect to previous entry */
        for (i = 1; i < len; i++) {
            input = merger->state->iinput[i + merger->state->index];

            /* make sure that they're correctly ordered */
            assert(input->termlen 
              == merger->state->iinput[merger->state->index]->termlen);
            assert(!str_ncmp(input->term, 
              merger->state->iinput[merger->state->index]->term, 
              input->termlen));
            assert((input->first 
                > merger->state->iinput[i + merger->state->index - 1]->first));
            assert(input->last 
                > merger->state->iinput[i + merger->state->index - 1]->last);
            assert(input->first 
                > merger->state->iinput[i + merger->state->index - 1]->last);

            input->first 
              -= merger->state->iinput[i + merger->state->index - 1]->last + 1;
            merger->state->vv.header.doc.docs += input->docs;
            merger->state->vv.header.doc.occurs += input->occurs;
            merger->state->vv.size += input->size + vec_vbyte_len(input->first);
        }

        merger->state->vv.header.doc.last 
          = merger->state->iinput[merger->state->index + len - 1]->last;

        input = merger->state->iinput[merger->state->index];

        /* keep the term stats up to date */
        merger->state->dterms++;
        if (merger->state->terms_low + merger->state->vv.header.doc.occurs 
          < merger->state->terms_low) {
            merger->state->terms_high++;
        }
        merger->state->terms_low += merger->state->vv.header.doc.occurs;

        assert(merger->out.offset_out 
          <= merger->state->storage->max_filesize - merger->out.size_out);
        if (merger->state->vv.size < merger->state->storage->vocab_lsize) {
            merger->state->vv.location = VOCAB_LOCATION_VOCAB;
            goto assign_vocab_label;

        /* check if file output will overflow current file */
        } else if ((merger->state->vv.loc.file.capacity 
            = merger->state->vv.size)
          > merger->state->storage->max_filesize - merger->out.offset_out 
            - merger->out.size_out) {

            merger->state->vv.location = VOCAB_LOCATION_FILE;
            merger->state->vv.loc.file.fileno = merger->out.fileno_out + 1;
            merger->state->vv.loc.file.offset = 0;
            merger->state->next_state = STATE_WRITE_FILE_FIRST;
            merger->state->state = STATE_FLUSH_NEWFILE;
            return MERGE_OUTPUT;
        } else {
            /* won't exceed current file, just write it */
            merger->state->vv.location = VOCAB_LOCATION_FILE;
            merger->state->vv.loc.file.fileno = merger->out.fileno_out;
            merger->state->vv.loc.file.offset 
              = merger->out.offset_out + merger->out.size_out;
            goto write_file_first_label;
        }
    }

    goto prefinish_label;

prefinish_label:
    /* write out btree buckets */
    do {
        merger->state->btree.fileno = merger->out_btree.fileno_out;
        merger->state->btree.offset = merger->out_btree.offset_out;

        switch ((ret = btbulk_finalise(&merger->state->btree, 
          &merger->state->root_fileno, &merger->state->root_offset))) {
        case BTBULK_FINISH:
        case BTBULK_OK:
            /* finished, do nothing */
            ret = BTBULK_FINISH;
            break;

        case BTBULK_FLUSH:
            /* need to start a new file */
            merger->state->next_state = STATE_PREFINISH;
            merger->state->state = STATE_FLUSH_NEWFILE;
            return MERGE_OUTPUT;

        case BTBULK_WRITE:
            /* need to write bucket to output */

            merger->state->count = 0;
            merger->state->next_state = STATE_PREFINISH;
            goto write_btree_label;
            break;

        default:
            assert(!CRASH);
            goto err_label;
        }
    } while (ret != BTBULK_FINISH);

    /* merge finished */
    goto finished_label;

write_btree_label:
    /* watch for filesize limit */
    if (merger->out_btree.offset_out 
      > merger->state->storage->max_filesize 
        - merger->state->storage->pagesize) {

        merger->out_btree.fileno_out++;
        merger->out_btree.offset_out = 0;
    }

    merger->out_btree.buf_out = merger->state->btree.output.write.next_out;
    merger->out_btree.size_out = merger->state->btree.output.write.avail_out;
    merger->state->state = merger->state->next_state;
    return MERGE_OUTPUT_BTREE;

write_end_label:
    /* finished reading the entry, check to see if we need to put it
     * back on the heap */
    if (merger->state->prev_index != merger->state->index) {
        /* it didn't finish, put it back on the heap */
        void *heapret;

        heapret = heap_insert(merger->state->iinput, &merger->state->inputs, 
            sizeof(*merger->state->iinput), iinput_cmp, 
            &merger->state->iinput[merger->state->prev_index]);
    }

    /* finished, select next input */
    assert(merger->state->index == merger->inputs - merger->state->finished);
    goto select_inputs_label;

assign_vocab_label:
    /* write an entry into the vocab */
    merger->state->btree.term = input->term;
    merger->state->btree.termlen = input->termlen;
    merger->state->btree.datasize = vocab_len(&merger->state->vv);

    do {
        merger->state->btree.fileno = merger->out_btree.fileno_out;
        merger->state->btree.offset = merger->out_btree.offset_out;

        switch ((ret = btbulk_insert(&merger->state->btree))) {
        case BTBULK_OK:
            /* finished, do nothing */
            break;

        case BTBULK_FLUSH:
            /* need to start a new file */
            merger->state->next_state = STATE_ASSIGN_VOCAB;
            merger->state->state = STATE_FLUSH_NEWFILE;
            return MERGE_OUTPUT;

        case BTBULK_WRITE:
            /* need to write bucket to output */

            merger->state->count = 0;
            merger->state->next_state = STATE_ASSIGN_VOCAB;
            goto write_btree_label;
            break;

        default:
            assert(!CRASH);
            goto err_label;
        }
    } while (ret != BTBULK_OK);

    vec.pos = merger->state->btree.output.ok.data;
    vec.end = vec.pos + merger->state->btree.datasize;
    vocab_encode(&merger->state->vv, &vec);
    assert(!VEC_LEN(&vec));

    if (merger->state->vv.location == VOCAB_LOCATION_VOCAB) {
        /* a bit hacky, if the destination is the vocab we still need to write
         * it, otherwise we proceed straight to getting the next input. */
        merger->state->addr = (char *) merger->state->btree.output.ok.data 
          + vocab_len(&merger->state->vv) - merger->state->vv.size;
        goto write_vocab_first_label;
    }

    /* reload or finish input */
    merger->state->next_state = STATE_WRITE_END;
    /* save index in prev_index so we can tell if it finished or
     * continues in write_end_label */
    merger->state->prev_index = merger->state->index;
    merger->state->count = 0;
    goto readentry_label;

write_vocab_first_label:
    /* write first entry out from input entry into vocab */
    vec.pos = merger->state->addr;
    vec.end = vec.pos + merger->state->storage->pagesize;

    /* note: first is already re-encoded, and size doesn't include size of 
     * first value */
    vec_vbyte_write(&vec, input->first);
    merger->state->count = 0;

    merger->state->addr = vec.pos;

    /* fallthrough to write_vocab_vector_label */

write_vocab_vector_label:
    /* write rest of vector out from input entry into allocated space
     * (in vocab).  We don't have to worry about running out
     * of output space since we allocated enough in advance. */
    if (input->input->avail_in + merger->state->count >= input->size) {
        len = input->size - merger->state->count;
        memcpy(merger->state->addr, input->input->next_in, len);
        merger->state->addr = ((char *) merger->state->addr) + len;
        input->input->next_in += len;
        input->input->avail_in -= len;
    } else {
        memcpy(merger->state->addr, input->input->next_in, 
          input->input->avail_in);
        merger->state->addr = ((char *) merger->state->addr) 
          + input->input->avail_in;
        input->input->next_in += input->input->avail_in;
        merger->state->count += input->input->avail_in;
        input->input->avail_in = 0;
        merger->state->state = STATE_WRITE_VOCAB_VECTOR;
        *index = merger->state->iinput[merger->state->index]
          - merger->state->input;
        *next_read = input->size - merger->state->count;
        return MERGE_INPUT;
    }

    /* reload or finish input */
    merger->state->next_state = STATE_WRITE_VOCAB_END;
    /* save index in prev_index so we can tell if it finished or
     * continues in write_vocab_end_label */
    merger->state->prev_index = merger->state->index;
    merger->state->count = 0;
    goto readentry_label;

write_vocab_end_label:
    /* finished reading the entry, check to see if we need to put it
     * back on the heap */
    if (merger->state->prev_index != merger->state->index) {
        /* it didn't finish, put it back on the heap */
        void *heapret;

        heapret = heap_insert(merger->state->iinput, &merger->state->inputs, 
            sizeof(*merger->state->iinput), iinput_cmp, 
            &merger->state->iinput[merger->state->prev_index]);
    }

    if (merger->state->index < merger->inputs - merger->state->finished) {
        /* need to write another vector into the vocab entry */
        input = merger->state->iinput[merger->state->index];
        goto write_vocab_first_label;
    } else {
        /* finished, select next input */
        goto select_inputs_label;
    }

write_file_first_label:
    /* write the first value from an input into the output stream */
    PUTVBYTE(input->first, STATE_WRITE_FILE_FIRST);

    merger->state->count = 0;
    /* fallthrough to write_file_vector_label */

write_file_vector_label:
    len = merger->state->outsize - merger->out.size_out;
    /* have to write the rest of the vector to file output stream */
    if ((input->input->avail_in > WRITE_POINT) 
      && (input->size - merger->state->count > WRITE_POINT)) {
        /* we're transferring such a large amount of data that we should just
         * move the reference for input to output and let another write occur */

        /* flush the output if necessary */
        if (merger->out.size_out) {
            merger->state->state = STATE_WRITE_FILE_VECTOR;
            return MERGE_OUTPUT;
        }

        /* figure out how much stuff we can transfer out via this method */
        len = (input->input->avail_in 
              > input->size - merger->state->count) 
            ? input->size - merger->state->count 
            : input->input->avail_in;

        assert(len > WRITE_POINT);

        /* connect up the internal buffer to the output */
        merger->out.buf_out = input->input->next_in;
        merger->out.size_out = len;
        merger->state->count += len;
        input->input->next_in += len;
        input->input->avail_in -= len;

        /* arrange for it to be switched back out on return */
        merger->state->state = STATE_FLUSH_SWITCH;
        merger->state->next_state = STATE_WRITE_FILE_VECTOR;
        return MERGE_OUTPUT;
    } else if ((input->input->avail_in 
        >= input->size - merger->state->count)
      && (len >= input->size - merger->state->count)) {
        /* can just copy the entire vector from in to out */
        memcpy(&merger->out.buf_out[merger->out.size_out], 
          input->input->next_in, input->size - merger->state->count);
        input->input->next_in += input->size - merger->state->count;
        input->input->avail_in -= input->size - merger->state->count;
        merger->out.size_out += input->size - merger->state->count;
    } else if (len > input->input->avail_in) {
        /* in is the limiting factor */
        memcpy(&merger->out.buf_out[merger->out.size_out], 
          input->input->next_in, input->input->avail_in);
        merger->state->count += input->input->avail_in;
        input->input->next_in += input->input->avail_in;
        merger->out.size_out += input->input->avail_in;
        input->input->avail_in = 0;
        merger->state->state = STATE_WRITE_FILE_VECTOR;
        *index = merger->state->iinput[merger->state->index]
          - merger->state->input;
        *next_read = input->size - merger->state->count;
        return MERGE_INPUT;
    } else {
        /* out is the limiting factor */
        memcpy(&merger->out.buf_out[merger->out.size_out], 
          input->input->next_in, len);
        merger->state->count += len;
        input->input->next_in += len;
        input->input->avail_in -= len;
        merger->out.size_out += len;
        merger->state->state = STATE_WRITE_FILE_VECTOR;
        return MERGE_OUTPUT;
    }

    /* if its the last entry, write overallocation entry */
    if (merger->state->index + 1 < merger->inputs - merger->state->finished) {
        /* reload or finish input */
        merger->state->next_state = STATE_WRITE_FILE_END;
        /* save index in prev_index so we can tell if it finished or
         * continues in write_vocab_end_label */
        merger->state->prev_index = merger->state->index;
        merger->state->count = 0;
        goto readentry_label;
    } else {
        /* calculate how much overallocation we need to write out */
        merger->state->count = merger->state->vv.loc.file.capacity 
            - merger->state->vv.size;
    }
    /* fallthrough to write_file_overalloc_label */

write_file_overalloc_label:
    /* need to write out extra space to accomodate overallocation scheme */
    len = merger->state->outsize - merger->out.size_out;
    if (merger->state->count && (len >= merger->state->count)) {
        /* can output directly */
        memset(&merger->out.buf_out[merger->out.size_out], 0, 
          merger->state->count);
        merger->out.size_out += merger->state->count;
    } else if (merger->state->count) {
        memset(&merger->out.buf_out[merger->out.size_out], 0, len);
        merger->out.size_out = merger->state->outsize;
        merger->state->count -= len;
        merger->state->state = STATE_WRITE_FILE_OVERALLOC;
        return MERGE_OUTPUT;
    }

    /* finished, write vocab entry */
    goto assign_vocab_label;

write_file_end_label:
    /* finished reading the entry, check to see if we need to put it
     * back on the heap */
    if (merger->state->prev_index != merger->state->index) {
        /* it didn't finish, put it back on the heap */
        void *heapret;

        heapret = heap_insert(merger->state->iinput, &merger->state->inputs, 
            sizeof(*merger->state->iinput), iinput_cmp, 
            &merger->state->iinput[merger->state->prev_index]);
        input = merger->state->iinput[merger->state->index];
    }
    goto write_file_first_label;

flush_newfile_label:
    /* the output has been flushed and we're starting a new file */
    merger->out.fileno_out++;
    merger->out.offset_out = 0;
    JUMP(merger->state->next_state);

flush_switch_label:
    /* the output has been flushed and we need to place the original output
     * buffer back onto the output */
    merger->out.buf_out = merger->state->out;
    merger->out.size_out = 0;
    JUMP(merger->state->next_state);

finished_label:
    merger->state->state = STATE_FINISHED;
    if (merger->out.size_out) {
        return MERGE_OUTPUT;
    } else {
        return MERGE_OK;
    }

/* labels that don't exist in this state machine */
pre_writeentry_label:
writeentry_label:
writeentry_term_label:
writeentry_docs_label:
writeentry_occurs_label:
writeentry_last_label:
writeentry_size_label:
writeentry_first_label:
writeentry_vector_label:
    /* fallthrough to err_label */
    assert(!CRASH);

err_label:
    assert(!CRASH);
    merger->state->state = STATE_ERR;
    return MERGE_ERR;
}

int merge_final_input_finish(struct merge_final *merger, unsigned int input) {
    switch (merger->state->state) {
    case STATE_READENTRY:
        if (input 
          == (unsigned int) (merger->state->iinput[merger->state->index]
            - merger->state->input)) {

            /* input has finished and needs to be removed from the stack */
            memmove(&merger->state->iinput[merger->state->index],
              &merger->state->iinput[merger->state->index + 1],
              sizeof(*merger->state->iinput)
                * (merger->inputs - merger->state->index - 1));
            merger->state->finished++;

            /* finished this entry while priming it (zero length file) 
             * XXX: should probably be handled more elegantly, but this is
             * simplest */
            if (merger->state->next_state == STATE_START) {
                merger->state->inputs--;
            }

            merger->state->state = merger->state->next_state;
            return MERGE_OK;
        }
        /* fallthrough */
        assert(!CRASH);

    default:
        assert(!CRASH);
        merger->state->state = STATE_ERR;
        return MERGE_ERR;
    }
}

/* variation on previous putvbyte macro */
#undef PUTVBYTE
#define PUTVBYTE(var, state_)                                                 \
    if (vec_vbyte_len(var) <= merger->state->outsize - merger->size_out) {    \
        vec.pos = merger->buf_out + merger->size_out;                         \
        vec.end = vec.pos + (merger->state->outsize - merger->size_out);      \
                                                                              \
        len = vec_vbyte_write(&vec, var);                                     \
                                                                              \
        merger->size_out += len;                                              \
    } else if (1) {                                                           \
        merger->state->state = state_;                                        \
        return MERGE_OUTPUT;                                                  \
    } else

int merge_inter(struct merge_inter *merger, unsigned int *index, 
  unsigned int *next_read) {
    unsigned long int tmp;
    unsigned int i,
                 len;
    struct vec vec = {NULL, 0};
    struct input *input = merger->state->iinput[merger->state->index],
                 *smallest,
                 *next,
                 **tmpinput;

    /* jump to correct state */
    JUMP(merger->state->state);

start_label:
    /* need to read the first entry from each of the inputs */
    merger->state->next_state = STATE_START;
    if (merger->state->index < merger->state->inputs) {
        input = merger->state->iinput[merger->state->index];
        merger->state->count = 0;
        goto readentry_label;
    }

    /* heapify inputs */
    heap_heapify(merger->state->iinput, merger->state->inputs, 
      sizeof(*merger->state->iinput), iinput_cmp);

    merger->state->count = 0;
    goto select_inputs_label;

readentry_label:
    GETVBYTE(input->termlen, STATE_READENTRY, merger->state->next_state);

    assert(input->termlen <= merger->state->max_termlen);
    assert(input->termlen);

    merger->state->count = 0;
    /* fallthrough to readentry_term_label */
readentry_term_label:
    if (input->input->avail_in >= input->termlen - merger->state->count) {
        len = input->termlen - merger->state->count;
        memcpy(&input->term[merger->state->count], input->input->next_in, len);
        input->term[input->termlen] = '\0';
        input->input->next_in += len;
        input->input->avail_in -= len;
    } else {
        len = input->input->avail_in;
        memcpy(&input->term[merger->state->count], input->input->next_in, len);
        input->input->next_in += len;
        input->input->avail_in = 0;
        merger->state->count += len;
        merger->state->state = STATE_READENTRY_TERM;
        *index = merger->state->iinput[merger->state->index]
          - merger->state->input;
        *next_read = input->termlen - len;
        return MERGE_INPUT;
    }

    /* shouldn't contain control characters (they're a pretty sure sign we're 
     * reading postings instead) */
    for (i = 0; i < input->termlen; i++) {
        assert(!iscntrl(input->term[i]));
    }

    merger->state->count = 0;
    /* fallthrough to readentry_docs_label */
readentry_docs_label:
    GETVBYTE(input->docs, STATE_READENTRY_DOCS, STATE_ERR);

    merger->state->count = 0;
    /* fallthrough to readentry_occurs_label */

readentry_occurs_label:
    GETVBYTE(input->occurs, STATE_READENTRY_OCCURS, STATE_ERR);

    merger->state->count = 0;
    /* fallthrough to readentry_last_label */
readentry_last_label:
    GETVBYTE(input->last, STATE_READENTRY_LAST, STATE_ERR);

    merger->state->count = 0;
    /* fallthrough to readentry_size_label */
readentry_size_label:
    GETVBYTE(input->size, STATE_READENTRY_SIZE, STATE_ERR);

    merger->state->count = 0;
    /* fallthrough to readentry_first_label */
readentry_first_label:
    GETVBYTE(input->first, STATE_READENTRY_FIRST, STATE_ERR);
    input->size -= len;      /* subtract length of first value from size */
    merger->state->index++;

    /* jump to next state */
    JUMP(merger->state->next_state);

select_inputs_label:
    /* select the smallest inputs from the heap */

    assert(merger->state->inputs == merger->inputs - merger->state->finished);
    while ((tmpinput = heap_pop(merger->state->iinput, 
        &merger->state->inputs, sizeof(*merger->state->iinput), iinput_cmp))) {
        smallest = *tmpinput;

        assert(smallest->termlen 
          && (str_len(smallest->term) == smallest->termlen));

        /* keep popping things off of stack until we get to an entry
         * that has a different term or run out of items */
        while (merger->state->inputs 
          && (next = *((struct input **) heap_peek(merger->state->iinput, 
              merger->state->inputs, sizeof(*merger->state->iinput))))
          && (smallest->termlen == next->termlen) 
          && !str_ncmp(smallest->term, next->term, smallest->termlen)
          && heap_pop(merger->state->iinput, &merger->state->inputs, 
              sizeof(*merger->state->iinput), iinput_cmp)) ;

        /* we now have the (merger->inputs -
         * merger->state->finished - merger->state->inputs) elements
         * at the back of merger->state->input array that need to be written to
         * the file, in reverse order of occurrance */

        merger->state->index = merger->state->inputs;

        /* swap the order of occurrance (have to do this because when we insert
         * them back onto the heap they will be absorbed in this order).
         * note that if len is odd middle element is already correct */
        len = merger->inputs - merger->state->finished - merger->state->index;
        tmp = len / 2; 
        for (i = 0; i < tmp; i++) {
            void *tmp = merger->state->iinput[merger->state->index + i];
            merger->state->iinput[merger->state->index + i] 
              = merger->state->iinput[merger->state->index + len - (i + 1)];
            merger->state->iinput[merger->state->index + len - (i + 1)] = tmp;
        }

        /* form a vocabulary entry for this term */
        input = merger->state->iinput[merger->state->index];
        merger->state->vv.header.doc.docs = input->docs;
        merger->state->vv.header.doc.occurs = input->occurs;
        merger->state->vv.size = input->size + vec_vbyte_len(input->first);

        /* loop over the rest of the entries, altering first entry to have 
         * correct delta-encoded value with respect to previous entry */
        for (i = 1; i < len; i++) {
            input = merger->state->iinput[i + merger->state->index];

            /* make sure that they're correctly ordered */
            assert(input->termlen 
              == merger->state->iinput[merger->state->index]->termlen);
            assert(!str_ncmp(input->term, 
              merger->state->iinput[merger->state->index]->term, 
              input->termlen));
            assert((input->first 
                > merger->state->iinput[i + merger->state->index - 1]->first));
            assert(input->last 
                > merger->state->iinput[i + merger->state->index - 1]->last);
            assert(input->first 
                > merger->state->iinput[i + merger->state->index - 1]->last);

            input->first 
              -= merger->state->iinput[i + merger->state->index - 1]->last + 1;
            merger->state->vv.header.doc.docs += input->docs;
            merger->state->vv.header.doc.occurs += input->occurs;
            merger->state->vv.size += input->size + vec_vbyte_len(input->first);
        }

        merger->state->vv.header.doc.last 
          = merger->state->iinput[merger->state->index + len - 1]->last;

        input = merger->state->iinput[merger->state->index];

        goto pre_writeentry_label;
    }

    /* finished */
    goto finished_label;

pre_writeentry_label:
    /* XXX: check to make sure we're not overflowing a file.  This is a hack,
     * because we shouldn't care about file boundaries here (but we have to for
     * the moment) */
    len = merger->state->vv.size + input->termlen 
      + vec_vbyte_len(input->termlen) 
      + vec_vbyte_len(merger->state->vv.header.doc.docs) 
      + vec_vbyte_len(merger->state->vv.header.doc.occurs) 
      + vec_vbyte_len(merger->state->vv.header.doc.last) 
      + vec_vbyte_len(merger->state->vv.size);

    assert(len <= merger->state->filesize);
    assert(merger->state->currsize <= merger->state->filesize);
    if (merger->state->currsize >= merger->state->filesize - len) {
        if (merger->size_out) {
            /* flush output */
            merger->state->state = STATE_PRE_WRITEENTRY;
            return MERGE_OUTPUT;
        } else {
            /* XXX: call newfile function */
            merger->state->newfile(merger->state->opaque_newfile);
            merger->state->currsize = len;
        }
    } else {
        merger->state->currsize += len;
    }

    goto writeentry_label;

writeentry_label:
    PUTVBYTE(input->termlen, STATE_WRITEENTRY);

    /* fallthrough to writeentry_term_label */
writeentry_term_label:
    len = merger->state->outsize - merger->size_out;
    if (input->termlen <= len) {
        memcpy(merger->buf_out + merger->size_out, input->term, input->termlen);
        merger->size_out += input->termlen;
    } else {
        merger->state->state = STATE_WRITEENTRY_TERM;
        return MERGE_OUTPUT;
    }

    /* fallthrough to writeentry_docs_label */
writeentry_docs_label:
    PUTVBYTE(merger->state->vv.header.doc.docs, STATE_WRITEENTRY_DOCS);

    /* fallthrough to writeentry_occurs_label */
writeentry_occurs_label:
    PUTVBYTE(merger->state->vv.header.doc.occurs, STATE_WRITEENTRY_OCCURS);

    /* fallthrough to writeentry_last_label */
writeentry_last_label:
    PUTVBYTE(merger->state->vv.header.doc.last, STATE_WRITEENTRY_LAST);

    /* fallthrough to writeentry_size_label */
writeentry_size_label:
    PUTVBYTE(merger->state->vv.size, STATE_WRITEENTRY_SIZE);

    /* fallthrough to writeentry_first_label */
writeentry_first_label:
    PUTVBYTE(input->first, STATE_WRITEENTRY_FIRST);

    /* fallthrough to writeentry_vector_label */
    merger->state->count = 0;

writeentry_vector_label:
    len = merger->state->outsize - merger->size_out;
    /* have to write the rest of the vector to file output stream */
    if ((input->input->avail_in > WRITE_POINT) 
      && (input->size - merger->state->count > WRITE_POINT)) {
        /* we're transferring such a large amount of data that we should just
         * move the reference for input to output and let another write occur */

        /* flush the output if necessary */
        if (merger->size_out) {
            merger->state->state = STATE_WRITEENTRY_VECTOR;
            return MERGE_OUTPUT;
        }

        /* figure out how much stuff we can transfer out via this method */
        len = (input->input->avail_in 
              > input->size - merger->state->count) 
            ? input->size - merger->state->count 
            : input->input->avail_in;

        assert(len > WRITE_POINT);

        /* connect up the internal buffer to the output */
        merger->buf_out = input->input->next_in;
        merger->size_out = len;
        merger->state->count += len;
        input->input->next_in += len;
        input->input->avail_in -= len;

        /* arrange for it to be switched back out on return */
        merger->state->state = STATE_FLUSH_SWITCH;
        merger->state->next_state = STATE_WRITEENTRY_VECTOR;
        return MERGE_OUTPUT;
    } else if ((input->input->avail_in 
        >= input->size - merger->state->count)
      && (len >= input->size - merger->state->count)) {
        /* can just copy the entire vector from in to out */
        memcpy(&merger->buf_out[merger->size_out], input->input->next_in, 
          input->size - merger->state->count);
        input->input->next_in += input->size - merger->state->count;
        input->input->avail_in -= input->size - merger->state->count;
        merger->size_out += input->size - merger->state->count;
    } else if (len > input->input->avail_in) {
        /* in is the limiting factor */
        memcpy(&merger->buf_out[merger->size_out], input->input->next_in, 
          input->input->avail_in);
        merger->state->count += input->input->avail_in;
        input->input->next_in += input->input->avail_in;
        merger->size_out += input->input->avail_in;
        input->input->avail_in = 0;
        merger->state->state = STATE_WRITEENTRY_VECTOR;
        *index = merger->state->iinput[merger->state->index]
          - merger->state->input;
        *next_read = input->size - merger->state->count;
        return MERGE_INPUT;
    } else {
        /* out is the limiting factor */
        memcpy(&merger->buf_out[merger->size_out], input->input->next_in, len);
        merger->state->count += len;
        input->input->next_in += len;
        input->input->avail_in -= len;
        merger->size_out += len;
        merger->state->state = STATE_WRITEENTRY_VECTOR;
        return MERGE_OUTPUT;
    }

    /* finished copying the vector for this input, reload or finish input */
    merger->state->next_state = STATE_WRITE_END;
    /* save index in prev_index so we can tell if it finished or
     * continues in write_end_label */
    merger->state->prev_index = merger->state->index;
    merger->state->count = 0;
    goto readentry_label;

write_end_label:
    /* finished reading the entry, check to see if we need to put it
     * back on the heap */
    if (merger->state->prev_index != merger->state->index) {
        /* it didn't finish, put it back on the heap */
        void *heapret;

        heapret = heap_insert(merger->state->iinput, &merger->state->inputs, 
            sizeof(*merger->state->iinput), iinput_cmp, 
            &merger->state->iinput[merger->state->prev_index]);
    }

    if (merger->state->index < merger->inputs - merger->state->finished) {
        /* need to write another vector into the vocab entry */
        input = merger->state->iinput[merger->state->index];
        goto writeentry_first_label;
    } else {
        /* finished, select next input */
        goto select_inputs_label;
    }

flush_switch_label:
    /* the output has been flushed and we need to place the original output
     * buffer back onto the output */
    merger->buf_out = merger->state->out;
    merger->size_out = 0;
    JUMP(merger->state->next_state);

finished_label:
    merger->state->state = STATE_FINISHED;
    if (merger->size_out) {
        return MERGE_OUTPUT;
    } else {
        return MERGE_OK;
    }

/* labels that don't exist in this state machine */
prefinish_label:
assign_vocab_label:
write_vocab_first_label:
write_vocab_vector_label:
write_vocab_end_label:
write_file_first_label:
write_file_vector_label:
write_file_overalloc_label:
write_file_end_label:
write_btree_label:
flush_newfile_label:
    /* fallthrough to err_label */
    assert(!CRASH);

err_label:
    assert(!CRASH);
    merger->state->state = STATE_ERR;
    return MERGE_ERR;
}

int merge_inter_input_finish(struct merge_inter *merger, unsigned int input) {
    switch (merger->state->state) {
    case STATE_READENTRY:
        if (input 
          == (unsigned int) (merger->state->iinput[merger->state->index]
            - merger->state->input)) {

            /* input has finished and needs to be removed from the stack */
            memmove(&merger->state->iinput[merger->state->index],
              &merger->state->iinput[merger->state->index + 1],
              sizeof(*merger->state->iinput)
                * (merger->inputs - merger->state->index - 1));
            merger->state->finished++;

            /* finished this entry while priming it (zero length file) 
             * XXX: should probably be handled more elegantly, but this is
             * simplest */
            if (merger->state->next_state == STATE_START) {
                merger->state->inputs--;
            }

            merger->state->state = merger->state->next_state;
            return MERGE_OK;
        }
        /* fallthrough */
        assert(!CRASH);

    default:
        assert(!CRASH);
        merger->state->state = STATE_ERR;
        return MERGE_ERR;
    }
}

