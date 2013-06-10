/* btbulk.h declares an interface that allows efficient (time and
 * space) construction of a btree via bulk loading.  In order to do this you 
 * need to ensure that the keys you insert into the tree are sorted prior to
 * insertion.  All hell will break loose if this is not the case.
 *
 * This module only allows for complete construction of a new btree,
 * not bulk updates of a previous one.  I've also now included an efficient way
 * to bulk-read the leaf pages from an existing b-tree.
 *
 * written nml 2004-05-21
 *
 */

#ifndef BTBULK_H
#define BTBULK_H

#ifdef __cplusplus
extern "C" {
#endif

/* structure representing the state of the bulk loading algorithm */
struct btbulk {
    /* inputs (caller manipulates these, btbulk only reads them) */
    const char *term;                  /* term to be inserted */
    unsigned int termlen;              /* length of term (excluding NUL) */
    unsigned int datasize;             /* how large the data entry for this 
                                        * term is */
    unsigned int fileno;               /* what the current file number is */
    unsigned long int offset;          /* what the current offset within that 
                                        * file number is */

    /* outputs (btbulk manipulates these, caller only reads them) */
    union {
        /* outputs returned with BTBULK_WRITE return value */
        struct {
            char *next_out;            /* buffer that needs to be written */
            unsigned int avail_out;    /* size of buffer that needs to be 
                                        * written */
        } write;

        /* outputs returned with BTBULK_OK return value */
        struct {
            void *data;                /* pointer to memory area to write term 
                                        * data into, of size input datasize */
        } ok;

    } output;

    struct btbulk_state *state;        /* opaque state */
};

/* create a new bulk insertion object in memory pointed to by space,
 * that creates a btree with pages of size pagesize bytes, using
 * leaf_strategy bucket strategy for tree leaves and node_strategy
 * bucket strategy for tree internal nodes.  The b-tree will be filled
 * to a percentage roughly equal to fill_factor, which should be in
 * range (0.0, 1.0].  Anything outside this range will be considered
 * to be a fill factor of 1.0.  buffer_pages is the number of pages to buffer
 * before writing them out. Returns a pointer to the space on success, 
 * or NULL on failure. */
struct btbulk *btbulk_new(unsigned int pagesize, unsigned long int maxfilesize,
  int leaf_strategy, int node_strategy, float fill_factor, 
  unsigned int buffer_pages, struct btbulk *space);

/* delete a bulk insertion object */
void btbulk_delete(struct btbulk *bulk);

/* values that can be returned from btbulk_insert or btbulk_read */
enum btbulk_ret {
    BTBULK_ERR = -1,                   /* an unexpected error has occurred */
    BTBULK_OK = 0,                     /* operation succeeded */
    BTBULK_WRITE = 1,                  /* current buffer must be output 
                                        * before insertion can proceed */
    BTBULK_READ = 2,                   /* requested buffer must be read before 
                                        * operation can proceed */
    BTBULK_FINISH = 3,                 /* end of b-tree has been reached */
    BTBULK_FLUSH = 4                   /* a new file must be started before a 
                                        * bucket can be written out */
};

/* insert a term and associated data into the btree.  On successful
 * insertion a pointer to the new data area is returned so that you
 * can write data into it (data area is of size datasize) and
 * BTBULK_OK is returned.  If insertion requires an existing bucket to
 * be written out, BTBULK_WRITE is returned and the write elements
 * in the output structure indicate the memory location (next_out) and
 * size (avail_out) the write must be.  The bulk loading algorithm
 * assumes that the output will be written to the location specified by 
 * fileno,offset.  This means that they have to be accurate upon every entry to 
 * this function, and needs to be updated after you've written the
 * buffer indicated by a _WRITE call.  _FLUSH indicates that there isn't enough
 * room for the bucket in the current file, and that a new one should be
 * started. */
int btbulk_insert(struct btbulk *bulk);

/* finish up the bulk insert.  This call will write the remaining
 * btree buckets out to disk, so you need to keep calling it until it
 * returns _FINISH.  It behaves exactly like _insert, with respect to
 * input and returned values.  on _FINISH the btree root node fileno and offset
 * are written into *root_fileno and *root_offset. */
int btbulk_finalise(struct btbulk *bulk, unsigned int *root_fileno, 
  unsigned long int *root_offset);

/* structure representing the state of the bulk reading algorithm */
struct btbulk_read {
    /* inputs (caller manipulates these, btbulk only reads them) */
    const char *next_in;               /* current input buffer */
    unsigned int avail_in;             /* size of current input buffer */
    unsigned int fileno_in;            /* what the current file number is */
    unsigned long int offset_in;       /* what the current offset within that 
                                        * file number is */

    /* outputs (btbulk manipulates these, caller only reads them) */
    union {
        /* outputs returned with BTBULK_READ return value */
        struct {
            unsigned int fileno;       /* fileno of next buffer required */
            unsigned long int offset;  /* offset of next buffer required */
        } read;

        /* outputs returned with BTBULK_OK return value */
        struct {
            const char *term;          /* current term */
            unsigned int termlen;      /* length of current term */
            unsigned int datalen;      /* how large the data entry for this 
                                        * term is */
            const void *data;          /* data entry for current term */
        } ok;
    } output;

    struct btbulk_read_state *state;   /* opaque state */
};

/* create a new bulk reading object in memory pointed to by space.  pagesize
 * should be obvious, strategy is the leaf strategy of the b-tree in question
 * (no index nodes will be read).  first_page_fileno and first_page_offset
 * specify where, in the b-tree file, the leftmost leaf node occurs.  Returns
 * NULL on failure */
struct btbulk_read *btbulk_read_new(unsigned int pagesize, 
  int strategy, unsigned int first_page_fileno, 
  unsigned long int first_page_offset, struct btbulk_read *space);

/* delete a bulk reading object */
void btbulk_read_delete(struct btbulk_read *bulk);

/* read next term and associated data from the b-tree.  Returns BTBULK_OK if the
 * term is currently available in the output structure.  Returns BTBULK_FINISH
 * if the b-tree doesn't have any further terms.  Returns BTBULK_READ if the
 * input buffer does not contain the required data, with an indication of which
 * section of the file needs to be read.  In all cases where BTBULK_READ is
 * returned it is expected that the subsequent next_in buffer will be of at
 * least pagesize bytes.  If this is not the case BTBULK_READ will continue to
 * be read */
enum btbulk_ret btbulk_read(struct btbulk_read *bulk);

/* return the offset of the bucket currently being read */
unsigned long int btbulk_read_offset(struct btbulk_read *bulk);

#ifdef __cplusplus
}
#endif
 
#endif

