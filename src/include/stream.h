/* stream.h declares an interface for objects that transparently process
 * text (or any stream of bytes) to adhere to such that they can be placed in 
 * a chain of cooperating objects, providing transparent buffering where 
 * necessary, but minimising the amount of copying that is done.
 *
 * stream.h also declares constructors for a bunch of stream filters, since
 * most of them don't have a seperate interface, apart from a constructor.
 *
 * stream.h embodies a philosophy on how to deal with text in a general way,
 * one that was inspired by the interfaces of zlib and bzlib (thanks to the
 * authors of those excellent packages).  The philosophy that i've adopted is 
 * to try to keep the text manipulation code as free from assumptions 
 * as possible.  For this reason, the stream interface works with 
 * pointer/length descriptions of text, instead of accepting file pointers, 
 * file descriptors, or some object representing a text stream.  
 * The benefit of this approach is that code conforming to these
 * interface can be applied to all situations with some code to perform
 * adaptation to the particular circumstances.  Input can be passed to the
 * stream in any size, and control flow can pass to other code in between
 * successive reads, but no synchronisation or other concurrency control 
 * is necessary.  This represents a burden to the code within the
 * stream, which has to keep sufficient state to be able to process input in
 * any form, at any time (although this burden can be eased with appropriate
 * buffering).  This will mean that in practice they will have to be some form
 * of state machine.  However, i believe that this is justified, because this
 * is the manner in which text is delivered from input sources such as disks
 * and network interfaces to programs.  Text is delivered in arbitrarily-sized
 * chunks, with some, often long, intervals in between successive chunks.  It
 * seems reasonable to pass this requirement along to code that has to
 * deal with text, especially since there are difficult issues in situations
 * where the responsibility for dealing with these problems is delegated
 * elsewhere.  Ultimately however, the major benefit of this philosophy is 
 * that *you* don't have to follow it, or agree with it.  This approach is 
 * flexible enough that with a small amount of code and buffer space you can 
 * adapt it to multi-threaded, blocking situations, make it translate text 
 * between pipes, or read text directly from a file on disk.
 *
 * The model of how filters delete one another will probably need to be revised
 * some time soon, as i don't think it covers all likely needs.  There will
 * probably also be more need to transparency about what is on the filter
 * stream.
 *
 * written nml 2004-08-19
 *
 */

#ifndef STREAM_H
#define STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>

/* return values from stream functions */
enum stream_ret {
    STREAM_ENOMEM = -ENOMEM,         /* out of memory error */
    STREAM_EINVAL = -EINVAL,         /* filtering error */
    STREAM_EEXIST = -EEXIST,         /* specified something that doens't 
                                      * exist */

    STREAM_OK = 0,                   /* filtering succeeded, output is ready */
    STREAM_END = 1,                  /* stream has ended, no output is ready */
    STREAM_INPUT = 2,                /* require more input */

    STREAM_DELETE = 3,               /* delete a filter that is either this 
                                      * filter or immediatly after this filter, 
                                      * depending on the after variable */

    STREAM_INSERT = 4,               /* insert a new filter, in the insert 
                                      * output variable, before or after (as 
                                      * specified by the after variable) */

    STREAM_OOB = 5                   /* data returned 'out-of-band'.  This is 
                                      * used to implement a tee stream filter,
                                      * that transparently returns data from an 
                                      * arbitrary point in the stream to the 
                                      * caller */
};

/* values that can be passed to indicate whether filters should be flushed */
enum stream_flush {
    STREAM_FLUSH_NONE = 0,
    STREAM_FLUSH_FINISH = 3
};

struct stream_state;

struct stream {
    char *next_in;                   /* next input buffer */
    unsigned int avail_in;           /* size of next input buffer */

    char *curr_out;                  /* current output buffer */
    unsigned int avail_out;          /* size of text at current output buffer */

    int id;                          /* id of returning filter, 0 for normal 
                                      * returns, and the id of the returning
                                      * filter for out-of-band returns or 
                                      * errors */

    struct stream_state *state;      /* opaque data pointer */
};

/* the basic idea is that each element in the chain accepts next_in/avail_in
 * pointers, and produces curr_out/avail_out pointers, doing whatever they have
 * to do on the way.  curr_out/avail_out either point to sections of the input
 * buffer, or to an internal buffer in the filter, so that output buffering
 * stream_filter objects isn't necessary.  This arrangement allows both filters
 * that copy from one buffer to the next, and filters that pass pointers through
 * from one to the next. */
struct stream_filter {
    char *next_in;                        /* next input buffer */
    unsigned int avail_in;                /* size of next input buffer */

    union {
        /* stuff returned when ret is _OK */
        struct {
            char *curr_out;               /* current output */
            unsigned int avail_out;       /* size of current output */
        } ok;

        /* stuff returned when ret is _INSERT */
        struct {
            struct stream_filter *insert;
            int after;
        } insert;

        /* stuff returned when ret is _DELETE */
        struct {
            int after;
        } delet; /* can't call this delete, because of the c++ keyword */

        /* stuff returned when ret is _OOB */
        struct {
            char *curr_out;               /* out-of-bound output */
            unsigned int avail_out;       /* size of out-of-bound output */
            int id;                       /* id of returning filter */
        } oob;
    } out;

    /* function to filter the stream, aiming to produce output */
    enum stream_ret (*filter)(struct stream_filter *filter, 
      enum stream_flush flush);

    /* function to delete the stream object, including cleaning up whatever
     * resources were allocated by the stream_filter object.  Returns
     * stream_filter_OK on success, can indicate errors using other return
     * codes.  Some return codes don't make much sense, so don't return them. */
    enum stream_ret (*deletefn)(struct stream_filter *filter);

    /* function to return a string identifing this stream_filter module */
    const char *(*idfn)(struct stream_filter *filter);
};

/* create a new stream */
struct stream *stream_new();

/* flush output from a stream.  The exact behaviour depends on which flush value
 * is given.  Currently only FLUSH_FINISH is available, which flushes all output
 * from the stream and finishes the stream.  No further input should be given to
 * the stream after stream_flush has been called with FLUSH_FINISH. */
enum stream_ret stream_flush(struct stream *stream, 
  enum stream_flush flushtype);

/* push a filter onto stream.  The filter then belongs to the stream, which
 * will delete it when necessary.  Note that you can't delete a filter from a
 * stream, they should be self-deleting.  Also note that you need to update the
 * output position of the stream, as the new filter will take whatever is left
 * on the output as the beginning of its input. */
enum stream_ret stream_filter_push(struct stream *stream, 
  struct stream_filter *filter);

/* push a filter on the stream, same as stream_filter_push.  However, the
 * filter is pushed onto the stream immediately before the current position.
 * This function is intended to be used by filters that shouldn't necessarily
 * be aware that they are part of a stream, so that they can easily adapt to
 * being either the ultimate consumer of a stream, or merely one filter in a
 * stream. */
enum stream_ret stream_filter_push_current(struct stream *stream,
  struct stream_filter *filter);

/* number of filters on this stream */
unsigned int stream_filters(struct stream *stream);

/* return the identifing string for the filter on this stream at position pos,
 * by writing a pointer into it into id. */
enum stream_ret stream_filter(struct stream *stream, unsigned int pos, 
  const char **id);

/* get more output from stream.  If flush is true within the stream struct, 
 * filters aren't allowed to buffer text.  You should do this immediately 
 * prior to attempting to read the input to exhaustion, but be careful doing 
 * it in other circumstances, as filters may delete themselves in reponse 
 * to being ordered to flush. */
enum stream_ret stream(struct stream *stream);

/* delete a stream and all its filters */
void stream_delete(struct stream *stream);

/* ideas for filters:
   - compression/decompression methods (gzip, bzip, lzw, zip)
   - conversion between encoding methods (probably via iconv)
   - encryption/decryption methods
   - 'collector' filter to ensure minimum size output (buffering if necessary)
   - 'distributor' filter to pass out text in small configurable chunks, 
     for debugging
   - base64, base85, hex, percent-encoding etc etc
   - filters to remove text from different document types
   - compression/encryption recogniser
   - HTTP chunked encoding/decoding
   - '\0' terminator
   - string matcher with subsequent actions (primarily to automatically delete
     filters when finding end-of-data markers)
   - byte counter with subsequent action
   - 'spy', which prints to stdout (for debugging)
*/

/* note that all of these methods return different types of filters, but that a
 * pointer to each of them should be able to be (sort-of) safely cast into a
 * stream_filter pointer */

/* collectorfilter is a simple stream filter that performs buffering to ensure 
 * a minimum input size for subsequent elements.  size indicates the amount
 * that should be 'collected', bytes the number of bytes required to be
 * processed in chunks of size size.  After bytes number of bytes have been
 * processed, the collectorfilter will remove itself from the stream.  0
 * indicates no limit on the number of bytes processed in chunks. */
struct collectorfilter;
struct collectorfilter *collectorfilter_new(unsigned int size, 
  unsigned int bytes);

/* detectfilter recursively detects encodings such as gzip and bzip compression
 * on the input stream, and pushes filters onto the stream to remove them.
 * limit encodings at most will be undone, before the detectfilter will
 * refuse to process any more (this is a defense mechanism against
 * denial-of-service attacks by encoding text a near-infinite number of times).
 * A limit of 0 is considered to be unlimited.  bufsize is the size of the
 * buffer that each decoding filter will be initialised with. */
struct detectfilter;
struct detectfilter *detectfilter_new(unsigned int bufsize, unsigned int limit);
/* XXX: should add tar/zip detection to this, although it has consequences for
 * stripping of internal communication characters */

/* teefilter returns data that passes through it via the out-of-bounds
 * mechanism.  id is the id that will be returned with the out-of-bounds 
 * data. */
struct teefilter;
struct teefilter *teefilter_new(int id);
/* XXX: could add limit to this if needed */

/* gunzipfilter is a stream filter module that is
 * capable of decompressing gzip files as described by RFC 1952 (GZIP file
 * format specification), using zlib to undo the deflate compression 
 * algorithm. */
struct gunzipfilter;
struct gunzipfilter *gunzipfilter_new(unsigned int bufsize);

#ifdef __cplusplus
}
#endif

#endif

