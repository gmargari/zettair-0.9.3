/* summarise.c declares methods for creating a query-biased textual summary 
 * of a document
 *
 * Calculation of scores is based on the paper
 * "Advantages of Query Biased Summaries in Information Retreival"
 * by A. Tombros and M. Sanderson.
 *
 * Each sentence in a docuement is given a score based on the number of
 * query terms in the sentence and the number of terms in the query:
 *
 * score = (square of the number of query terms in the sentence) 
 *   / (number of terms in the query)
 *
 * written nml 2006-03-30
 *
 */

#include "firstinclude.h"

#include "summarise.h"

#include "ascii.h"
#include "chash.h"
#include "def.h"
#include "fdset.h"
#include "heap.h"
#include "_index.h"
#include "index_querybuild.h"
#include "docmap.h"
#include "mlparse_wrap.h"
#include "psettings.h"
#include "str.h"
#include "stream.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct summarise {
    struct mlparse parser;
    struct index *idx;        
    struct fdset *fd;
    struct psettings *pset;
    struct docmap *map;
    unsigned int max_termlen;
    void (*stem)                   /* stemming function for terms */
      (void *opaque, char *term);
    void *stemmer;                 /* stemming object */
    char *buf;
    unsigned int bufsize;

    struct stream *last_stream;
    unsigned int last_fileno;
    off_t last_offset;
    off_t last_poffset;
};

struct persum {
    unsigned int term;             /* current term number */
    unsigned int summary_len;      /* length of summary to be generated */
    char *termbuf;                 /* buffer capable of holding one term */
    char *title;                   /* buffer for title */
    unsigned int title_len;        /* length of title */
    unsigned int title_size;       /* length of title buffer */
    struct psettings_type *ptype;  /* tags for given document type */
    struct chash *terms;           /* hashtable lookup for query terms */
    int index;                     /* current state */
    unsigned int stack;            /* state stack */
    int fd;                        /* source fd */
    unsigned int bytes_left;       /* bytes left in the document */
};

struct summarise *summarise_new(struct index *idx) {
    struct summarise *sum = malloc(sizeof(*sum));

    if (sum 
      && (sum->buf = malloc(BUFSIZ))
      && (mlparse_new(&sum->parser, idx->storage.max_termlen, LOOKAHEAD))) {

        sum->stem = index_stemmer(idx);
        sum->stemmer = idx->stem;
        sum->idx = idx;
        sum->fd = idx->fd;
        sum->pset = idx->settings;
        sum->map = idx->map;
        sum->max_termlen = idx->storage.max_termlen;
        sum->last_stream = NULL;
        sum->last_fileno = -1;
        sum->last_offset = -1;
        sum->bufsize = BUFSIZ;
    } else if (sum) {
        if (sum->buf) {
            free(sum->buf);
        }
        free(sum);
        sum = NULL;
    }

    return sum;
}

void summarise_delete(struct summarise *sum) {
    if (sum->last_stream) {
        stream_delete(sum->last_stream);
    }
    free(sum->buf);
    mlparse_delete(&sum->parser);
    free(sum);
}

struct sentence {
    char *buf;                    /* buffer */
    unsigned int buflen;          /* buffer length */
    unsigned int bufsize;         /* buffer capacity */
    unsigned int start_term;      /* ordinal number of initial term */
    unsigned int terms;           /* number of terms in sentence */
    unsigned int qterms;          /* number of query terms in sentence */
    float score;                  /* sentence score */
    struct sentence *next;        /* next sentence */
    struct sentence *prev;        /* previous sentence */
};

/* internal function to handle reallocation of sentence buffers */
static int ensure_space(struct sentence **sent, unsigned int space) {
    while ((*sent)->buflen + space >= (*sent)->bufsize) {
        void *ptr = realloc(*sent, sizeof(**sent) + (*sent)->bufsize * 2);

        if (ptr) {
            *sent = ptr;
            (*sent)->buf = (void *) (*sent + 1);

            if (DEAR_DEBUG) {
                memset((*sent)->buf + (*sent)->bufsize, 0, (*sent)->bufsize);
            }

            (*sent)->bufsize *= 2;
        } else {
            return 0;
        }
    }

    return 1;
}

/* internal function to finish up extraction of a sentence */
static struct sentence *extract_finish(struct sentence *sent, struct persum *ps,
  enum index_summary_type type, int highlight) {
    if (highlight && type == INDEX_SUMMARISE_TAG) {
        /* need to close the tag */
        if (sent->buflen + str_len("</b>") >= sent->bufsize 
          && !ensure_space(&sent, sent->buflen + str_len("</b>"))) {
            unsigned int space = 0;
            /* ran out of memory (damn!) at an inconvenient time,
             * erase terms from the buffer until we can fit in an
             * ending tag */

            while (!isspace(sent->buf[sent->buflen - 1]) 
              && (sent->buf[sent->buflen - 1] != '>')
              && sent->buflen
              && (space < str_len("</b>"))) {
                sent->buflen--;
            }

            /* end highlighting */
            str_cpy(sent->buf + sent->buflen, "</b>");
            sent->buflen += str_len("</b>");
        }
    }

    /* trim overly-long sentence term-by-term */
    while (sent->buflen > ps->summary_len) {
        sent->buflen--;
        while (sent->buf[sent->buflen - 1] != ' ') {
            sent->buflen--;
        }
    }

    /* remove superfluous whitespace from the end of the sentence */
    while (sent->buf[sent->buflen - 1] == ' ') {
        sent->buflen--;
    }

    sent->buf[sent->buflen] = '\0';
    return sent;
}

unsigned int html_cpy(struct sentence **sent, const char *term, 
  unsigned int len) {
    unsigned int extra = 0,
                 orig_len = len;
    const char *name = NULL;

/* maximum length of an escape sequence that we insert (max &quot; or &#123;) */
#define MAX_ESCAPE 6

    while (len) {
        while (len && ((*sent)->buflen + MAX_ESCAPE < (*sent)->bufsize)) {
            switch (*term) {
            case ASCII_CASE_CONTROL:
            case ASCII_CASE_EXTENDED:
            case '\'': /* note that &apos; is not standard */
            case '\0':
                /* numeric escape character */
                (*sent)->buf[(*sent)->buflen++] = '&';
                (*sent)->buf[(*sent)->buflen++] = '#';
                extra++;
                if (*term > 99) {
                    (*sent)->buf[(*sent)->buflen++] = ((*term) / 100) + '0';
                    extra++;
                }
                (*sent)->buf[(*sent)->buflen++] = (((*term) / 10) % 10) + '0';
                extra++;
                (*sent)->buf[(*sent)->buflen++] = ((*term) % 10) + '0';
                extra++;
                (*sent)->buf[(*sent)->buflen++] = ';';
                extra++;
                assert((*sent)->buflen < (*sent)->bufsize);
                break;

            case '<':
            case '>':
            case '"':
            case '&':
                /* named escape character */
                switch (*term) {
                case '<': name = "lt"; break;
                case '>': name = "gt"; break;
                case '"': name = "quot"; break;
                case '&': name = "amp"; break;
                }
                (*sent)->buf[(*sent)->buflen++] = '&';
                while (*name) {
                    (*sent)->buf[(*sent)->buflen++] = *name++;
                    extra++;
                }
                (*sent)->buf[(*sent)->buflen++] = ';';
                extra++;
                break;

            default:
                (*sent)->buf[(*sent)->buflen++] = *term;
                break;
            }
            term++;
            len--;
        }

        if (len && !ensure_space(sent, (*sent)->bufsize + len)) {
            return orig_len - len + extra;
        }
    }

    assert(!len);
    return orig_len + extra;
}

/* internal function to extract the next sentence */
static struct sentence *extract(struct summarise *sum, struct persum *ps,
  enum index_summary_type type, const struct query *q, 
  struct sentence *sent, unsigned int *occs) {
    unsigned int i,
                 len;
    int highlight = 0,          /* whether we're currently highlighting */
        title = 0;              /* whether we're currently in a title */
    void **found;
    struct conjunct *conj;      /* current query term, in query struct */
    enum mlparse_ret ret; 
    enum psettings_attr attr;

    /* note that because we potentially realloc sent, we *must* return it if we
     * modify it or else risk leaking memory */

    /* note that we try to replicate the parsing here
     * (actually we should refactor it somewhere else so that we can reuse 
     * it) */
 
    /* zero out occurrance table */
    for (i = 0; i < q->terms; i++) {
        occs[i] = 0;
    }
    sent->start_term = ps->term;
    sent->buflen = 0;
    sent->terms = 0;
    sent->qterms = 0;

    do {
        ret 
          = mlparse_parse(&sum->parser, ps->termbuf, &len, 0);
        switch (ret) {
        case MLPARSE_COMMENT:
        case MLPARSE_COMMENT | MLPARSE_END:
            if (ret & MLPARSE_END) {
                len = str_len("/sgmlcomment");
                assert(len < sum->max_termlen);
                memcpy(ps->termbuf, "/sgmlcomment", len);
            } else {
                len = str_len("sgmlcomment");
                assert(len < sum->max_termlen);
                memcpy(ps->termbuf, "sgmlcomment", len);
            }
            /* fallthrough to tag parsing */
        case MLPARSE_TAG:
            /* lookup tag, see if text can flow through this tag (otherwise
             * sentence ends) */
            ps->termbuf[len] = '\0';
            str_strip(ps->termbuf + (ps->termbuf[0] == '/'));
            attr = psettings_type_find(sum->pset, ps->ptype, ps->termbuf);
            title = 0;

            /* change state based on tag index attribute */
            if (attr & PSETTINGS_ATTR_INDEX 
              || attr & PSETTINGS_ATTR_CHECKBIN) {
                /* note that we assume that the binary check succeeded */
                ps->stack <<= 1;
                ps->stack |= !ps->index;
                ps->index = 1;
            } else if (attr & PSETTINGS_ATTR_POP) {
                ps->index = !(ps->stack & 1);
                ps->stack >>= 1;
            } else if (attr & PSETTINGS_ATTR_OFF_END) {
                /* need to search forward until we get the end tag */

                /* store end tag at the end of the current summary */
                if (sent->buflen + len + 1 < sent->bufsize 
                  || ensure_space(&sent, sent->buflen + len + 1)) {

                    str_cpy(sent->buf + sent->buflen, ps->termbuf);

                    do {
                        ret = mlparse_parse(&sum->parser, ps->termbuf, 
                            &len, 0);
                        switch (ret) {
                        case MLPARSE_ERR:
                        case MLPARSE_EOF:
                            if (sent->buflen) {
                                return extract_finish(sent, ps, type, 
                                    highlight);
                            } else {
                                /* can return NULL because we can't have 
                                 * realloc'd the buffer */
                                return NULL;
                            }
                            break;

                        case MLPARSE_TAG:
                            ps->termbuf[len] = '\0';
                            str_strip(ps->termbuf + (ps->termbuf[0] == '/'));
                            if ((sent->buf[sent->buflen] == '/' 
                                && !str_cmp(sent->buf + sent->buflen + 1, 
                                    ps->termbuf))
                              || (sent->buf[sent->buflen] != '/'
                                && ps->termbuf[0] == '/'
                                && !str_cmp(sent->buf + sent->buflen,
                                    ps->termbuf + 1))) {
                                /* found the end tag */
                                ps->index = 1;
                            }
                            break;

                        case MLPARSE_INPUT:
                            if (!ps->bytes_left) {
                                mlparse_eof(&sum->parser);
                            } else if (index_stream_read(sum->last_stream, 
                                ps->fd, sum->buf, sum->bufsize) == STREAM_OK) {

                                sum->parser.next_in 
                                  = sum->last_stream->curr_out;
                                if (ps->bytes_left 
                                  >= sum->last_stream->avail_out) {
                                    sum->parser.avail_in 
                                      = sum->last_stream->avail_out;
                                    ps->bytes_left 
                                      -= sum->last_stream->avail_out;
                                    sum->last_stream->avail_out = 0;
                                } else {
                                    sum->parser.avail_in = ps->bytes_left;
                                    sum->last_stream->avail_out 
                                      -= ps->bytes_left;
                                    sum->last_stream->curr_out 
                                      += ps->bytes_left;
                                    ps->bytes_left = 0;
                                }
                            } else {
                                return extract_finish(sent, ps, type, 
                                    highlight);
                            }
                            break;

                        default: /* ignore */ break;
                        }
                    } while (!ps->index);
                }

                /* note that we turn indexing back on (slightly dodgy, since 
                 * we don't know that's the action specified by that tag - 
                 * XXX) but it keeps things simple */
            } else if (attr & PSETTINGS_ATTR_TITLE) {
                title = 1;
            } else {
                /* indexing off */
                ps->stack <<= 1;
                ps->stack |= !ps->index;
                ps->index = 0;
            }

            if (sent->buflen && !(attr & PSETTINGS_ATTR_FLOW)) {
                /* sentence ends here, due to markup */
                ps->term++;  /* separate this sentence from next, since 
                              * a significant tag lies between them */
                return extract_finish(sent, ps, type, highlight);
            }
            break;

        case MLPARSE_WORD:
        case MLPARSE_WORD | MLPARSE_CONT:
        case MLPARSE_WORD | MLPARSE_END:
            if (title) {
                /* copy into title buffer, don't generate a sentence from it */
                if (ps->title_len + len + 2 < ps->title_size) {
                    memcpy(ps->title + ps->title_len, ps->termbuf, len);
                    ps->title_len += len;
                    ps->title[ps->title_len++] = ' ';
                } else if (ps->title_len + 1 < ps->title_size) {
                    unsigned int tlen = ps->title_size - ps->title_len - 1;
                    memcpy(ps->title + ps->title_len, ps->termbuf, tlen);
                    ps->title_len += tlen;
                    assert(ps->title_len + 1 <= ps->title_size);
                    if (ps->title_len + 1 < ps->title_size) {
                        ps->title[ps->title_len++] = ' ';
                    }
                    assert(ps->title_len + 1 == ps->title_size);
                }
                break;
            } else if (!ps->index) {
                break;
            }

            sent->terms++;
            ps->term++;

            /* ensure we have enough memory in buffer for the term */
            if (sent->buflen + len + 1 >= sent->bufsize 
              && !ensure_space(&sent, sent->buflen + len + 1)) {
                /* ran out of memory, just skip this term */
                assert(sent->buflen);
                return extract_finish(sent, ps, type, highlight);
            }

            /* copy term in, before we mangle it */
            if (type == INDEX_SUMMARISE_TAG) {
                unsigned int tmp = sent->buflen;

                /* need to escape HTML entities in the term */
                len = html_cpy(&sent, ps->termbuf, len);
                sent->buflen = tmp;
            } else {
                memcpy(sent->buf + sent->buflen, ps->termbuf, len);
            }

            /* strip and stem the term */
            ps->termbuf[len] = '\0';
            str_strip(ps->termbuf);
            sum->stem(sum->stemmer, ps->termbuf);

            if (chash_str_ptr_find(ps->terms, ps->termbuf, &found) 
              == CHASH_OK) {
                unsigned int taglen;

                /* it's a query term, retroactively highlight it in the 
                 * sentence buffer */
                switch (type) {
                case INDEX_SUMMARISE_PLAIN: /* do nothing */ break;
                case INDEX_SUMMARISE_CAPITALISE:
                    for (i = 0; i < len; i++) {
                        sent->buf[sent->buflen + i] 
                          = toupper(sent->buf[sent->buflen + i]);
                    }
                    break;

                case INDEX_SUMMARISE_TAG:
                    if (!highlight) {
                        taglen = str_len("<b>");
                        if (sent->buflen + taglen >= sent->bufsize 
                          && !ensure_space(&sent, sent->buflen + taglen)) {
                            /* ran out of memory, just skip this term */
                            assert(sent->buflen);
                            return extract_finish(sent, ps, type, highlight);
                        }

                        memmove(sent->buf + sent->buflen + taglen, 
                          sent->buf + sent->buflen, len);
                        memcpy(sent->buf + sent->buflen, "<b>", taglen);
                        sent->buflen += taglen;
                    }
                    break;

                default: assert("can't get here" && 0);
                }
                highlight = 1;

                /* increment counters */
                conj = *found;
                if (conj >= q->term && conj < q->term + q->terms) {
                    occs[conj - q->term]++;
                }
                sent->qterms++;
            } else {
                /* it's not a query term */
                if (highlight && type == INDEX_SUMMARISE_TAG) {
                    unsigned int taglen = str_len("</b>");
                    if (sent->buflen + taglen >= sent->bufsize 
                      && !ensure_space(&sent, sent->buflen + taglen)) {
                        /* ran out of memory, just skip this term */
                        assert(sent->buflen);
                        return extract_finish(sent, ps, type, highlight);
                    }

                    /* end highlighting */
                    memmove(sent->buf + sent->buflen + taglen, 
                      sent->buf + sent->buflen, len);
                    memcpy(sent->buf + sent->buflen, "</b>", taglen);
                    sent->buflen += taglen;
                }
                highlight = 0;
            }
            sent->buflen += len;

            if (ret & MLPARSE_END) {
                return extract_finish(sent, ps, type, highlight);
            } else if (!(ret & MLPARSE_CONT)) {
                sent->buf[sent->buflen++] = ' ';
            }
            break;

        case MLPARSE_INPUT:
            if (!ps->bytes_left) {
                mlparse_eof(&sum->parser);
            } else if (index_stream_read(sum->last_stream, ps->fd, sum->buf, 
                sum->bufsize) == STREAM_OK) {

                sum->parser.next_in = sum->last_stream->curr_out;
                if (ps->bytes_left > sum->last_stream->avail_out) {
                    sum->parser.avail_in = sum->last_stream->avail_out;
                    ps->bytes_left -= sum->last_stream->avail_out;
                } else {
                    sum->parser.avail_in = ps->bytes_left;
                    sum->last_stream->avail_out -= ps->bytes_left;
                    sum->last_stream->curr_out += ps->bytes_left;
                    ps->bytes_left = 0;
                }
            } else {
                return extract_finish(sent, ps, type, highlight);
            }
            break;

        case MLPARSE_ERR:
        case MLPARSE_EOF: 
            if (sent->buflen) {
                return extract_finish(sent, ps, type, highlight);
            } else {
                /* can return NULL because we can't have realloc'd the buffer */
                return NULL;
            }
            break;

        default:
            /* ignore */
            break;
        }
    } while ((ret != (MLPARSE_WORD | MLPARSE_END)) 
      && (sent->buflen < ps->summary_len));

    return extract_finish(sent, ps, type, highlight);
}

static void score(struct sentence *sent, const struct query *q) {
    sent->score = (sent->qterms * sent->qterms) / (float) q->terms;
}

/* internal function (for qsort) to order sentences by position in the 
 * document */
static int sum_pos_cmp(const void *vone, const void *vtwo) {
    const struct sentence *one = *((const struct sentence **) vone), 
                          *two = *((const struct sentence **) vtwo);

    return one->start_term - two->start_term;
}

/* internal function (for heap) to order sentences by score */
static int sum_score_cmp(const void *vone, const void *vtwo) {
    const struct sentence *one = *((const struct sentence **) vone), 
                          *two = *((const struct sentence **) vtwo);

    if (one->score < two->score) {
        return -1;
    } else if (one->score > two->score) {
        return 1;
    } else {
        return two->start_term - one->start_term;
    }
}

/* internal function (for heap) to order sentences by score */
static int rev_sum_score_cmp(const void *vone, const void *vtwo) {
    const struct sentence *one = *((const struct sentence **) vone), 
                          *two = *((const struct sentence **) vtwo);

    if (one->score < two->score) {
        return 1;
    } else if (one->score > two->score) {
        return -1;
    } else {
        return two->start_term - one->start_term;
    }
}

static void persum_delete(struct summarise *sum, struct persum *ps) {
    free(ps->termbuf);
    sum->last_poffset = lseek(ps->fd, 0, SEEK_CUR);
    fdset_unpin(sum->fd, sum->idx->repos_type, sum->last_fileno, ps->fd);
    chash_delete(ps->terms);
}

enum summarise_ret summarise(struct summarise *sum, unsigned long int docno,
  const struct query *query, enum index_summary_type type, 
  struct summary *result) {
    struct persum ps;                     /* per-sum elements */
    struct sentence **heap = NULL,        /* heap of candidate sentences */
                    *unused = NULL,       /* unused sentence buffers */
                    *prev = NULL,         /* previous sentence */
                    *min;                 /* minimum element on heap */
    unsigned int i,
                 heap_len = 0,            /* number of items in heap */
                 heap_size = 0,           /* capacity of heap */
                 heap_bytes = 0,          /* length of heap in total buffered 
                                           * bytes */
                 selected,                /* number of sentences selected */
                 *occs,                   /* array for counting query term 
                                           * occurrances */
                 fileno,                  /* repository of given document */
                 bytes;                   /* size of given document */
    off_t offset,                         /* repository offset of given doc */
          curroffset,
          physoffset;
    int finished = 0;                     /* continued iteration indicator */
    enum docmap_ret dmret;
    enum docmap_flag dmflags;
    enum mime_types mtype;
    struct stream_filter *filter;

    if (!(occs = malloc(sizeof(*occs) * query->terms))) {
        return SUMMARISE_ENOMEM;
    }

    /* find document, and reinitialise parser with fd for it */
    if (((dmret = docmap_get_location(sum->map, docno, &fileno, &offset, 
        &bytes, &mtype, &dmflags)) == DOCMAP_OK)) {

        mlparse_reinit(&sum->parser);
        if (dmflags & DOCMAP_COMPRESSED) {
            /* read from a compressed file */
            if (sum->last_fileno == fileno && sum->last_offset < offset) {
                /* can use previous stream */
                physoffset = sum->last_poffset;
                curroffset = sum->last_offset;
                sum->last_offset = offset + bytes;
            } else {
                /* have to initialise a new stream */
                curroffset = physoffset = 0;

                if (sum->last_stream) {
                    stream_delete(sum->last_stream);
                }

                if ((sum->last_stream = stream_new())
                  && (filter 
                    = (struct stream_filter *) gunzipfilter_new(BUFSIZ))) {

                    stream_filter_push(sum->last_stream, filter);
                    sum->last_fileno = fileno;
                    sum->last_offset = offset + bytes;
                } else {
                    if (sum->last_stream) {
                        stream_delete(sum->last_stream);
                        sum->last_stream = NULL;
                    }
                    return SUMMARISE_ENOMEM;
                }
            }
        } else {
            /* read from a regular file */
            if ((sum->last_stream = stream_new())) {
                sum->last_fileno = fileno;
                sum->last_offset = offset + bytes;
                curroffset = physoffset = offset;
            } else {
                return SUMMARISE_ENOMEM;
            }
        }
    } else {
        return SUMMARISE_EIO;
    }

    /* initialise ps */
    if ((ps.termbuf = malloc(sum->max_termlen + 1)) 
      && (ps.terms = chash_str_new(3, 0.5, str_nhash))
      && (ps.fd = fdset_pin(sum->fd, sum->idx->repos_type, fileno, physoffset, 
            SEEK_SET))
      && (psettings_type_tags(sum->pset, mtype, &ps.ptype) == PSETTINGS_OK)) {

        if (DEAR_DEBUG) {
            memset(ps.termbuf, 0, sum->max_termlen + 1);
        }

        ps.bytes_left = bytes;
        ps.index = 1;
        ps.stack = 0;
        ps.term = 0;
        ps.title = result->title;
        ps.summary_len = result->summary_len;
        ps.title_len = 0;
        ps.title_size = result->title_len;
    } else {
        free(occs);
        if (ps.termbuf) {
            free(ps.termbuf);
            if (ps.terms) {
                if (ps.fd >= 0) {
                    fdset_unpin(sum->fd, sum->idx->repos_type, fileno, ps.fd);
                }
                chash_delete(ps.terms);
                return SUMMARISE_ERR;
            }
        }
        return SUMMARISE_ENOMEM;
    }

    /* read until we hit the start of the document */
    assert(curroffset <= offset);
    while (curroffset + (off_t) sum->last_stream->avail_out < offset) {
        curroffset += sum->last_stream->avail_out;
        if (index_stream_read(sum->last_stream, ps.fd, sum->buf, sum->bufsize) 
          != STREAM_OK) {
            persum_delete(sum, &ps);
            return SUMMARISE_EIO;
        }
    }
    sum->last_stream->curr_out += offset - curroffset;
    sum->last_stream->avail_out -= offset - curroffset;

    /* stream now points to start of doc */
    sum->parser.next_in = sum->last_stream->curr_out;
    sum->parser.avail_in = sum->last_stream->avail_out;
    if (ps.bytes_left > sum->last_stream->avail_out) {
        ps.bytes_left -= sum->last_stream->avail_out;
    } else {
        sum->last_stream->curr_out += ps.bytes_left;
        sum->last_stream->avail_out -= ps.bytes_left;
        ps.bytes_left = 0;
    }

    /* load all query terms into hash */
    for (i = 0; i < query->terms; i++) {
        struct term *term = &query->term[i].term;

        do {
            /* FIXME: what about prefix phrases? */
            if (chash_str_ptr_insert(ps.terms, term->term, term) == CHASH_OK) {
                term = term->next;
            } else {
                free(occs);
                persum_delete(sum, &ps);
            }
        } while (term);
    }

    while (!finished) {
        /* get a free sentence buffer */
        struct sentence *space = unused,
                        *next;

        if (space) {
            unused = unused->next;
        } else if ((space 
            = malloc(sizeof(*space) + sum->max_termlen * 2))) {
            /* point buffer to the end of the struct within the malloc'd 
             * block */
            space->buf = (void *) (space + 1);
            space->bufsize = sum->max_termlen * 2;

            if (DEAR_DEBUG) {
                memset(space->buf, 0, space->bufsize);
            }
        } else {
            /* can't get memory for more sentences, finish up with what 
             * we have */
            finished = 1;
            break;
        }
        space->prev = prev;
        space->next = NULL;

        /* extract sentence */
        if ((next = extract(sum, &ps, type, query, space, occs))) {
            /* got a sentence, score it and figure out whether to heap it */
            score(next, query);

            if (!heap_len 
              || ((min = *((struct sentence **) 
                  heap_peek(heap, heap_len, sizeof(*heap))))
                && (min->score < next->score))
              || (heap_bytes < result->summary_len)) {

                /* insert next into heap */
                prev = next;

                /* try to make space in the array for it */
                if (heap_len + 1 >= heap_size) {
                    void *ptr = realloc(heap, 
                        sizeof(*heap) * (heap_size * 2 + 1));

                    if (ptr) {
                        heap = ptr;
                        heap_size *= 2;
                        heap_size++;
                    }
                }

                if (heap_len < heap_size) {
                    heap_insert(heap, &heap_len, sizeof(*heap), sum_score_cmp,
                      &next);
                    heap_bytes += next->buflen;

                    min = *((struct sentence **) 
                      heap_peek(heap, heap_len, sizeof(*heap)));
                    while (heap_bytes - min->buflen > result->summary_len) {
                        /* minimum element can't make it into the summary,
                         * remove it */
                        heap_pop(heap, &heap_len, sizeof(*heap), sum_score_cmp);
                        heap_bytes -= min->buflen;
                        min->next = unused;
                        min->prev = NULL;
                        unused = min;
                        min = *((struct sentence **) 
                          heap_peek(heap, heap_len, sizeof(*heap)));
                    }
                } else {
                    heap_bytes += next->buflen;
                    heap_replace(heap, heap_len, sizeof(*heap), sum_score_cmp, 
                      &next);
                    heap_bytes -= next->buflen;
                    next->next = unused;
                    next->prev = NULL;
                    unused = next;
                }
            } else {
                next->next = unused;
                next->prev = NULL;
                unused = next;
                prev = NULL;
            }
        } else {
            finished = 1;
            space->next = unused;
            space->prev = NULL;
            unused = space;
        }
    }

    /* free the unused entries */
    while (unused) {
        struct sentence *next = unused;
        unused = unused->next;
        free(next);
    }

    /* now figure out how many sentences to stuff into the buffer */
    if (!heap_len) {
        /* it's an empty document, don't know why we're summarising 
         * it, but... */
        result->summary[0] = '\0';
    } else {
        assert(heap_len);
        /* sort by score.  Note use of rev_sum_score_cmp to order with highest
         * scoring first */
        qsort(heap, heap_len, sizeof(*heap), rev_sum_score_cmp);
        heap_bytes = heap[0]->buflen;
        selected = 1;
        for (; selected < heap_len 
          && heap_bytes + heap[selected]->buflen + 5 < result->summary_len; 
          selected++) {
            heap_bytes += heap[selected]->buflen + 5;
        }
        assert(selected);

        /* copy them all into the buffer, and we're done */
        qsort(heap, selected, sizeof(*heap), sum_pos_cmp);
        for (heap_bytes = i = 0; i < selected; i++) {
            if (i) {
                if (heap[i - 1]->start_term + heap[i - 1]->terms 
                  != heap[i]->start_term) { 
                    if (result->summary[heap_bytes - 1] != '.'
                      || !isupper(heap[i]->buf[0])) {
                        result->summary[heap_bytes++] = ' ';
                        result->summary[heap_bytes++] = '.';
                    }
                    result->summary[heap_bytes++] = '.';
                    result->summary[heap_bytes++] = '.';
                    result->summary[heap_bytes++] = ' ';
                } else {
                    result->summary[heap_bytes++] = ' ';
                    result->summary[heap_bytes++] = ' ';
                }
            }
            memcpy(result->summary + heap_bytes, heap[i]->buf, heap[i]->buflen);
            heap_bytes += heap[i]->buflen;
            assert(heap_bytes < result->summary_len);
        }
        assert(heap_bytes < result->summary_len);
        result->summary[heap_bytes] = '\0';
    }

    for (i = 0; i < heap_len; i++) {
        free(heap[i]);
    }

    if (heap) {
        free(heap);
    }

    free(occs);
    persum_delete(sum, &ps);
    return SUMMARISE_OK;
}

