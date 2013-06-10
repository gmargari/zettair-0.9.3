/* merge.h declares a set of functions merge numbers of runs together
 * into either a single run or a single final index.  
 * Note that the final merge assumes that all full-text vectors contain 
 * mutually-exclusive and non-overlapping sets of document numbers.  
 * This assumption allows optimised merging to occur.  If some of the input 
 * runs do not meet these conditions you should merge into further intermediate
 * runs before final merging.
 *
 * written nml 2003-02-11
 *
 */

#ifndef MERGE_H
#define MERGE_H

#ifdef __cplusplus
extern "C" {
#endif
    
struct storagep;

/* return values from merge calls */
enum merge_ret {
    MERGE_ERR = 0,                   /* an error has occurred */
    MERGE_OK = 1,                    /* call was successful */
    MERGE_INPUT = 2,                 /* need to fill an input buffer */
    MERGE_OUTPUT = 3,                /* need to flush the output buffer */
    MERGE_OUTPUT_BTREE = 4           /* need to flush a btree bucket from 
                                      * output buffer */
};

/* struct to represent an input source to merge from */
struct merge_input {
    char *next_in;                   /* input buffer or NULL when finished */
    unsigned int avail_in;           /* size of input buffer */
};

/* Note that these structures below are *not* a next_out, avail_out 
 * arrangement ALA zlib. the output buffer is under the control of the merge 
 * module.  You just write buf_out out when indicated and set size_out to 0 
 * when you're done */

/* struct to represent the state of a final merge.  All elements other
 * than state should be initially set by the caller, and maintained as
 * necessary (input requires refilling when asked for, output requires flushing
 * when asked for) */
struct merge_final {
    struct merge_input *input;       /* array of input sources */
    unsigned int inputs;             /* number of input sources */

    /* output for vector files */
    struct {
        char *buf_out;                   /* output buffer */
        unsigned int size_out;           /* size of stuff in output buffer */
        unsigned int fileno_out;         /* output location (file number) */
        unsigned long int offset_out;    /* output location (file offset) */
    } out;
    
    /* output for vocab files */
    struct {
        char *buf_out;                   /* output buffer */
        unsigned int size_out;           /* size of stuff in output buffer */
        unsigned int fileno_out;         /* output location (file number) */
        unsigned long int offset_out;    /* output location (file offset) */
    } out_btree;

    struct merge_final_state *state; /* internal state */
};

/* struct to represent the state of an intermediate merge.  All elements other
 * than state should be initially set by the caller, and maintained as
 * necessary (input requires refilling when asked for, output requires flushing
 * when asked for) */
struct merge_inter {
    struct merge_input *input;       /* array of input sources */
    unsigned int inputs;             /* number of input sources */

    char *buf_out;                   /* output buffer */
    unsigned int size_out;           /* size of stuff in output buffer */

    struct merge_inter_state *state; /* internal state 
                                      * (merge_inter_state is what the fitzroy 
                                      * lions did ;o) */
};

/* initialise a final merge object.  Params are:
 *    - opaque: an opaque data handle for memory allocation functions
 *    - allocfn: a function through which to allocate memory
 *    - freefn: a function through which to free memory
 *    - storage: a set of storage paramters (see storagep.h)
 *    - outbuf: an output buffer
 *    - outbufsz: size of the output buffer 
 *
 * merge_new returns MERGE_OK on success.  Pass opaque, allocfn and
 * freefn as NULL to use malloc and free. */
int merge_final_new(struct merge_final *merger, void *opaque,
  void *(*allocfn)(void *opaque, unsigned int size),
  void (*freefn)(void *opaque, void *mem), struct storagep *storage,
  void *outbuf, unsigned int outbufsz);

/* perform a final merge.  Keep on calling this function until it returns
 * FMERGE_OK.  It will return FMERGE_INPUT when it requires more input (the
 * input that requires filling will be written into *index, a hint as to what
 * the next read will be will be written into *next_read, although this will be
 * 0 if it can't be predicted.  This function will return FMERGE_OUTPUT when it
 * requires the output buffer to be flushed to disk.  Don't fiddle with the
 * output buffer other than to write it and set size_out to 0, as the merger 
 * swaps inputs into the output to avoid copying large amounts of data.  Pay
 * attention to fileno_out and offset_out, as the final merger keeps the output
 * within the filesize limit by using these variables to place the output.
 * Other than that, the output will be contiguous. */
int merge_final(struct merge_final *merger, unsigned int *index, 
  unsigned int *next_read);

/* indicate that an input has reached the end of its data to the merger object.
 * input is the index of the finished input in the original inputs array.
 * Returns MERGE_OK on success */
int merge_final_input_finish(struct merge_final *merger, unsigned int input);

/* complete a final merge, obtaining the final btree root fileno and offset, 
 * the number of distinct terms merged and the total number of terms. */
int merge_final_finish(struct merge_final *merger, unsigned int *root_fileno, 
  unsigned long int *root_offset, unsigned long int *dterms, 
  unsigned int *terms_high, unsigned int *terms_low);

/* delete a final merge object */
void merge_final_delete(struct merge_final *merger);

/* initialise an intermediate merge object.  Params are:
 *    - opaque: an opaque data handle for memory allocation functions
 *    - allocfn: a function through which to allocate memory
 *    - freefn: a function through which to free memory
 *    - outbuf: an output buffer
 *    - outbufsz: size of the output buffer 
 *
 * merge_inter_new returns MERGE_OK on success.  Pass opaque, allocfn and
 * freefn as NULL to use malloc and free. */
int merge_inter_new(struct merge_inter *merger, void *opaque,
  void *(*allocfn)(void *opaque, unsigned int size),
  void (*freefn)(void *opaque, void *mem), 
  void *outbuf, unsigned int outbufsz, unsigned int max_termlen,

  /* XXX: newfile call is a blatant hack to get around the problem where we
   * still store intermediate merges in one file, which means we need a way to
   * limit them to < 2GB.  This should be removed when we start treating
   * intermediate files as collections of extents (about when we do in-place
   * merging) 
   *
   * anyway, basically newfile is called when the intermediate file needs to be
   * moved to a new file. */
  void *opaque_newfile, void (*newfile)(void *opaque_newfile), 
  unsigned long int filesize);

/* perform an intermediate merge.  works the same as the final merge method,
 * except that you don't have to worry about fileno_out and offset_out */
int merge_inter(struct merge_inter *merger, unsigned int *index, 
  unsigned int *next_read);

/* indicate that an input has reached the end of its data to the merger object.
 * input is the index of the finished input in the original inputs array.
 * Returns MERGE_OK on success */
int merge_inter_input_finish(struct merge_inter *merger, unsigned int input);

/* delete an intermediate merge object */
void merge_inter_delete(struct merge_inter *merger);

#ifdef __cplusplus
}
#endif

#endif

