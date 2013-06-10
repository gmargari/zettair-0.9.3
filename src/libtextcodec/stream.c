/* stream.c implements a bunch of stuff to manipulate text in streams (FIXME)
 *
 * written nml 2004-08-19
 *
 */

#include "stream.h"
#include "def.h"

#include <assert.h>
#include <stdlib.h>

/* link in stream filter doubly linked list */
struct stream_link {
    struct stream_link *next;          /* next in linked list */
    struct stream_link *prev;          /* prev in linked list */
    struct stream_filter *filter;      /* data element */
    enum stream_flush flush;           /* what the current flush state of this 
                                        * link is */
};

struct stream_state {
    struct stream_link *first;         /* first in linked list */
    struct stream_link *last;          /* last in linked list */

    struct stream_link *curr;          /* filter we're currently 
                                        * trying to get output out of */
    unsigned int filters;              /* total number of filters */
    enum stream_flush flush;           /* what the current flush state is */
};

/* ensure that doubly linked list makes sense */
static int stream_invariant(struct stream *stream) {
    struct stream_link *curr,
                       *prev;
    unsigned int count;

    count = 0;
    for (prev = NULL, curr = stream->state->first; curr; 
      prev = curr, curr = curr->next) {
        if (curr->prev != prev) {
            assert(0);
            return 0;
        }
        count++;
    }

    if (count != stream->state->filters || (prev != stream->state->last)) {
        assert(0);
        return 0;
    }

    count = 0;
    for (prev = NULL, curr = stream->state->last; curr; 
      prev = curr, curr = curr->prev) {
        if (curr->next != prev) {
            assert(0);
            return 0;
        }
        count++;
    }

    if (count != stream->state->filters || (prev != stream->state->first)) {
        assert(0);
        return 0;
    }

    return 1;
}

struct stream *stream_new() {
    struct stream *stream;

    if ((stream = malloc(sizeof(*stream) + sizeof(*stream->state)))) {
        stream->state = (void *) &stream[1];
        stream->state->curr = stream->state->first = stream->state->last = NULL;
        stream->state->filters = 0;
        stream->state->flush = STREAM_FLUSH_NONE;
        stream->next_in = NULL;
        stream->avail_in = 0;
        stream->curr_out = NULL;
        stream->avail_out = 0;
        if (!stream_invariant(stream)) {
            free(stream);
            return NULL;
        }
    }

    return stream;
}

void stream_delete(struct stream *stream) {
    struct stream_link *curr = stream->state->first, 
                       *next;

    while (curr) {
        next = curr->next;
        curr->filter->deletefn(curr->filter);
        free(curr);
        curr = next;
    }

    free(stream);
}

enum stream_ret stream_filter_push(struct stream *stream, 
  struct stream_filter *filter) {
    struct stream_link *link;

    assert(stream_invariant(stream));

    if ((link = malloc(sizeof(*link)))) {
        link->filter = filter;
        link->filter->next_in = stream->curr_out;
        link->filter->avail_in = stream->avail_out;
        if (stream->state->last) {
            link->flush = stream->state->last->flush;
        } else {
            link->flush = stream->state->flush;
        }
        stream->curr_out = NULL;
        stream->avail_out = 0;

        /* append link to filters list */
        link->next = NULL;
        link->prev = stream->state->last;

        if (stream->state->last) {
            assert(stream->state->first);
            stream->state->last->next = link;
        } else {
            stream->state->first = link;
        }

        stream->state->last = link;
        stream->state->filters++;

        assert(stream_invariant(stream));
        return STREAM_OK;
    } else {
        return STREAM_ENOMEM;
    }
}

enum stream_ret stream_filter_push_current(struct stream *stream, 
  struct stream_filter *filter) {
    struct stream_link *link;

    assert(stream_invariant(stream));

    /* have to have a current module to push in front of */
    if (!stream->state->curr) {
        return STREAM_EINVAL;
    }

    if ((link = malloc(sizeof(*link)))) {
        link->filter = filter;
        link->filter->next_in = stream->state->curr->filter->next_in;
        link->filter->avail_in = stream->state->curr->filter->avail_in;

        if (stream->state->curr->prev) {
            link->flush = stream->state->curr->prev->flush;
        } else {
            link->flush = stream->state->flush;
        }

        stream->state->curr->filter->next_in = NULL;
        stream->state->curr->filter->avail_in = 0;

        link->next = stream->state->curr;
        link->prev = stream->state->curr->prev;
        if (stream->state->curr->prev) {
            stream->state->curr->prev = link;
        } else {
            stream->state->first = link;
        }
        stream->state->curr->prev = link;

        stream->state->filters++;

        assert(stream_invariant(stream));
        return STREAM_OK;
    } else {
        return STREAM_ENOMEM;
    }
}

enum stream_ret stream_flush(struct stream *stream, enum stream_flush flush) {
    if (stream->state->flush != STREAM_FLUSH_FINISH) {
        /* XXX: note last input value ala bzlib? (to prevent them slipping more
         * input onto the stream after its finished) */
        stream->state->flush = flush;
        return STREAM_OK;
    } else {
        return STREAM_EINVAL;
    }
}

unsigned int stream_filters(struct stream *stream) {
    return stream->state->filters;
}

enum stream_ret stream_filter(struct stream *stream, unsigned int pos, 
  const char **id) {
    if (pos < stream->state->filters) {
        struct stream_link *curr;

        /* iterate to specified filter */
        for (curr = stream->state->first; pos; pos--, curr = curr->next) {
            assert(curr);
        }

        assert(curr);

        *id = curr->filter->idfn(curr->filter);
        return STREAM_OK;
    } else {
        return STREAM_EEXIST;
    }
}

enum stream_ret stream(struct stream *stream) {
    struct stream_link *curr = stream->state->curr;
    enum stream_ret ret;
    void *ptr;
    unsigned int len;

    /* need to get some more input */

    do {
        while (curr) {
            /* synchronise current into the state, so that if filter() calls a
             * stateful stream function it will be correct */
            stream->state->curr = curr;

            switch ((ret = curr->filter->filter(curr->filter, 
                curr->flush))) {

            case STREAM_OK:
                /* call succeeded, propagate result back down chain */
                if (curr->filter->out.ok.avail_out) {
                    if (curr->next) {
                        curr->next->filter->next_in 
                          = curr->filter->out.ok.curr_out;
                        curr->next->filter->avail_in 
                          = curr->filter->out.ok.avail_out;

                        /* propagate flush value if current has used all of its
                         * input.  Technically we should propagate all of the
                         * time, but this would mean that filters that aren't
                         * capable of processing all of their input at once
                         * (e.g. a HTML filter that returns entity references in
                         * a seperate buffer than normal text) have problems
                         * (either have to play buffer games to process all text
                         * at once, or risk having flush spuriously propagated
                         * beyond them) */
                        if (!curr->filter->avail_in) {
                            curr->next->flush = curr->flush;
                        }
                        curr = curr->next;
                    } else {
                        /* reached end of chain */
                        stream->curr_out = curr->filter->out.ok.curr_out;
                        stream->avail_out = curr->filter->out.ok.avail_out;
                        stream->state->curr = stream->state->last;
                        return STREAM_OK;
                    }

                    if (!curr->prev) {
                        /* first filter, update input position so that it 
                         * remains current */
                        stream->next_in = curr->filter->next_in;
                        stream->avail_in = curr->filter->avail_in;
                    }
                }  
                /* otherwise got 0 length return, need to call again.  Note that
                 * there is some danger of infinite looping if the one component
                 * continually returns STREAM_OK with 0 length returned. */
                break;

            case STREAM_INPUT:
                /* propagate flush value forward through the chain */
                if (curr->next) {
                    curr->next->flush = curr->flush;
                }

                if (curr->flush == STREAM_FLUSH_FINISH) {
                    /* flushing, propagate forward */
                    curr = curr->next;
                } else {
                    /* more input needed, propagate backward to get it */
                    curr = curr->prev;
                }
                break;

            case STREAM_INSERT:
                /* need to insert a new filter */
                assert(stream_invariant(stream));
                if ((stream->state->curr 
                  = malloc(sizeof(*stream->state->curr)))) {
                    stream->state->curr->filter 
                      = curr->filter->out.insert.insert;
                } else {
                    stream->state->curr = curr;
                    return STREAM_ENOMEM;
                }

                if (curr->filter->out.insert.after) {
                    /* after this filter */
                    stream->state->curr->filter->next_in = NULL;
                    stream->state->curr->filter->avail_in = 0;
                    stream->state->curr->flush = STREAM_FLUSH_NONE;

                    stream->state->curr->prev = curr;
                    stream->state->curr->next = curr->next;
                    if (curr->next) {
                        /* next shouldn't have any input, because we've 
                         * iterated past it to get more */
                        assert(!curr->filter->avail_in);  
                        curr->next->prev = stream->state->curr;
                    } else {
                        stream->state->last = stream->state->curr;
                    }
                    curr->next = stream->state->curr;

                    /* have to iterate to next filter, in case it wants to 
                     * inject anything into the stream */
                    curr = curr->next; 
                } else {
                    /* before this filter */

                    /* new filter gets input leftover for this filter */
                    stream->state->curr->filter->next_in 
                      = curr->filter->next_in;
                    stream->state->curr->filter->avail_in 
                      = curr->filter->avail_in;
                    stream->state->curr->flush = STREAM_FLUSH_NONE;

                    /* also reset current filter's flush state, as it needs to
                     * accept new input from previous filter and be flushed
                     * again */
                    curr->flush = STREAM_FLUSH_NONE;

                    curr->filter->next_in = NULL;
                    curr->filter->avail_in = 0;

                    stream->state->curr->next = curr;
                    stream->state->curr->prev = curr->prev;
                    if (curr->prev) {
                        curr->prev->next = stream->state->curr;
                    } else {
                        stream->state->first = stream->state->curr;
                    }
                    curr->prev = stream->state->curr;

                    /* stay on current filter, in case it wants to produce
                     * anything now (even though its got no input left) */
                }
                stream->state->filters++;
                assert(stream_invariant(stream));
                break;

            case STREAM_DELETE:
                /* need to delete a filter */
                assert(stream_invariant(stream));
                if (curr->filter->out.delet.after) {
                    /* delete filter immediately after this one */

                    /* ensure that there is a next filter, and that it either
                     * doesn't have a subsequent filter, or that the filter
                     * after that doesn't have stored input from the current
                     * filter */
                    if (curr->next 
                      && (!curr->next->next 
                        || !curr->next->next->filter->avail_in)) {

                        /* iterate to next filter, and let code below do the
                         * work */
                        curr = curr->next;
                    } else {
                        if (curr->next) {
                            assert("trying to delete filter another filter "
                              "is trying to read from" && 0);
                        } else {
                            assert("trying to delete non-existant next filter" 
                              && 0);
                        }
                        stream->state->curr = curr;
                        return STREAM_EINVAL;
                    }
                } 

                /* delete current filter */
                ptr = curr->filter->next_in;
                len = curr->filter->avail_in;

                if (curr->prev) {
                    curr->prev->next = curr->next;
                } else {
                    stream->state->first = curr->next;
                }

                if (curr->next) {
                    /* next filter gets the input that was meant for this
                     * filter */
                    curr->next->filter->next_in = curr->filter->next_in;
                    curr->next->filter->avail_in = curr->filter->avail_in;
                    curr->next->prev = curr->prev;
                    curr->next->flush = curr->flush;

                    /* delete the filter */
                    stream->state->curr = curr->next;
                    stream->state->filters--;
                    assert(stream_invariant(stream));
                    curr->filter->deletefn(curr->filter);
                    free(curr);
                    curr = stream->state->curr;

                } else {
                    stream->state->curr = stream->state->last = curr->prev;
                    stream->curr_out = ptr;
                    stream->avail_out = len;

                    /* delete the filter, using previous as next starting point
                     * (since we have no next) */
                    stream->state->curr = curr->prev;
                    stream->state->filters--;
                    assert(stream_invariant(stream));
                    curr->filter->deletefn(curr->filter);
                    free(curr);
                    curr = stream->state->curr;

                    if (stream->avail_out) {
                        return STREAM_OK;
                    }
                }
                break;

            case STREAM_OOB:
                stream->state->curr = curr;
                stream->curr_out = curr->filter->out.oob.curr_out;
                stream->avail_out = curr->filter->out.oob.avail_out;
                stream->id = curr->filter->out.oob.id;
                return STREAM_OOB;

            default:
                /* error, find out what number this module is and stuff that 
                 * into id */
                for (stream->id = 0; curr; (curr = curr->prev), stream->id++) ;
                stream->id--;
                stream->state->curr = curr;
                assert(!CRASH);
                return ret;
            }
        }

        /* need input from the outside */
        if (stream->avail_in) {
            if (stream->state->first) {
                /* pass input into first filter */
                curr = stream->state->first;
                curr->filter->next_in = stream->next_in;
                curr->filter->avail_in = stream->avail_in;
                stream->avail_in = 0;
                stream->next_in = NULL;
                curr->flush = stream->state->flush;
                if (stream->state->flush != STREAM_FLUSH_FINISH) {
                    stream->state->flush = STREAM_FLUSH_NONE;
                }
            } else {
                /* pass input directly to output */
                stream->state->curr = stream->state->first;
                stream->curr_out = stream->next_in;
                stream->avail_out = stream->avail_in;
                stream->next_in = NULL;
                stream->avail_in = 0;
                return STREAM_OK;
            }
        } else if (stream->state->flush != STREAM_FLUSH_FINISH) {
            stream->state->curr = NULL;
            return STREAM_INPUT;
        } else if (!stream->state->first 
          || (stream->state->first && stream->state->last->flush)) {
            /* no filters, or last filter has been flushed, we have finished */
            assert(curr == NULL);
            stream->state->curr = curr;
            return STREAM_END;
        } else {
            /* propagate flush back down chain */
            curr = stream->state->first;
            curr->flush = stream->state->flush;
        }
    } while (1);
}

