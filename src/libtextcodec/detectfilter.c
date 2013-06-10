/* detectfilter.c implements a stream filter (see stream.h) that
 * detects encodings on the text stream and transparently inserts modules to 
 * undo them.  It currently only supports gzip, but this concept is
 * applicable to just about anything that can be hueristically detected
 * and dealt with.
 *
 * detectfilter.c doesn't have a header file because the only publicly 
 * accessible method is the constructor, which is declared in stream.h
 *
 * XXX: it would be really cool to use a run-time trie to do all of
 * this, so that we could dynamically configure the detectfilter to do
 * things.  Would also be nice if we built this file from something
 * like /etc/magic
 *
 * written nml 2004-08-23
 *
 */

#include "stream.h"

#include <assert.h>
#include <stdlib.h>

struct detectfilter {
    struct stream_filter filter; /* stream filter object (needs to be first) */
    unsigned int bufsize;        /* buffer size detected filters get initialised 
                                  * with */
    unsigned int limited;        /* whether we're limited in the number of 
                                  * filters we can detect */
    unsigned int limit;          /* if we're limited, what the current limit 
                                  * is */
};

#define MIN_INPUT 2

static enum stream_ret detectfilter_filter(struct stream_filter *filter, 
  enum stream_flush flush) {
    struct detectfilter *state = (void *) filter;
    char *ptr = filter->next_in;
    unsigned int len = filter->avail_in;

    if (filter->avail_in < MIN_INPUT) {
        /* if (flush == STREAM_FLUSH_NONE) {
             * FIXME: insert a collection filter component before us to relieve
             * the need to buffer/unbuffer *
            if ((filter->out.insert.insert 
              = (void *) collectorfilter_new(MIN_INPUT, MIN_INPUT))) {
                filter->out.insert.after = 0;
                return STREAM_INSERT;
            } else {
                return STREAM_ENOMEM;
            }
        } else { */

        if (!filter->avail_in) {
            return STREAM_INPUT;
        } else {
            /* have to delete this module to obey the flush command */
            filter->out.delet.after = 0;
            return STREAM_DELETE;
        }
    }

    /* delete this filter if we've hit our limit */
    if (state->limited && !state->limit) {
        filter->out.delet.after = 0;
        return STREAM_DELETE;
    }

    /* below this point we know we have as much input as we need */

    assert(filter->avail_in >= MIN_INPUT);

    len--;
    switch (*ptr++) {
    case '\037': 
        /* matched gzip encoding first byte, continue below */
        break;

    default: 
        /* no match, delete this module (note that we don't update
         * next_in/avail_in, so original input now goes to next filter) */
        filter->out.delet.after = 0;
        return STREAM_DELETE;
    }

    len--;
    switch (*ptr++) {
    case '\213': 
        /* matched both gzip magic bytes, now insert a gzip decoding
         * module into the stream before this module and repeat */
        if ((filter->out.insert.insert 
          = (void *) gunzipfilter_new(state->bufsize))) {
            state->limit--;
            filter->out.insert.after = 0;
            return STREAM_INSERT;
        } else {
            return STREAM_ENOMEM;
        }
        break;

    default: 
        /* no match, delete this module (note that we don't update
         * next_in/avail_in, so original input now goes to next filter) */
        filter->out.delet.after = 0;
        return STREAM_DELETE;
    }
}

static enum stream_ret detectfilter_delete(struct stream_filter *filter) {
    free(filter);
    return STREAM_OK;
}

static const char *detectfilter_id(struct stream_filter *filter) {
    return "detect";
}

struct detectfilter *detectfilter_new(unsigned int bufsize, 
  unsigned int limit) {
    struct detectfilter *filter;

    /* ensure bufsize is greater than MIN_INPUT so that subsequent detections
     * work */
    if (bufsize < MIN_INPUT) {
        bufsize = MIN_INPUT;
    } 

    if ((filter = malloc(sizeof(*filter)))) {
        filter->bufsize = bufsize;
        filter->limited = filter->limit = limit;
        filter->filter.filter = detectfilter_filter;
        filter->filter.deletefn = detectfilter_delete;
        filter->filter.idfn = detectfilter_id;
        assert((void *) &filter->filter == (void *) filter);
        return filter;
    } else {
        return NULL;
    }
}

