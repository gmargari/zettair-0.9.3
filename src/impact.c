/* impact_search.c implements impact ordered search as proposed by Anh and
 * Moffat
 * 
 * based on code by garcias
 *
 */

#include "firstinclude.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "chash.h"
#include "def.h"
#include "error.h"
#include "heap.h"
#include "impact_build.h"
#include "impact.h"
#include "index.h"
#include "_index.h"
#include "search.h"
#include "vec.h"
#include "vocab.h"
#include "index_querybuild.h"

struct disksrc {
    struct search_list_src src;      /* parent source structure */
    struct alloc alloc;              /* buffer allocation object */
    char *buf;                       /* available buffer */
    unsigned int bufsize;            /* size of stuff in buffer */
    unsigned int bufcap;             /* capacity of buffer */
    unsigned long int bufpos;        /* position buffer was read from */
    unsigned int size;               /* size of the list */
    unsigned int pos;                /* position in list */

    struct index *idx;               /* index we're using */
    int fd;                          /* pinned fd */
    unsigned int type;               /* the type of the fd */
    unsigned int fileno;             /* the file number of the fd */
    unsigned long int offset;        /* starting offset of the list */
};

struct term_data {
    unsigned int impact;                /* current impact */
    unsigned int w_qt;                  /* term weight */
    unsigned int blocksize;             /* number of docs remaining in block */
    unsigned long int docno;            /* current docno */
    struct vec v;                       /* vector of in-memory data */
    struct search_list_src *src;        /* list src */
};

/* internal function to compare phrase term pointers by frequency/estimated
   frequency */
static int term_data_cmp(const void *one, const void *two) {
    const struct term_data *tdone = one,
                           *tdtwo = two;

    return tdtwo->impact - tdone->impact;
}

/* internal function to compare phrase term pointers by frequency/estimated
   frequency */
static int f_t_cmp(const void *one, const void *two) {
    const struct conjunct *done = one,
                          *dtwo = two;

    if (done->f_t < dtwo->f_t) {
        return -1;
    } else if (done->f_t > dtwo->f_t) {
        return 1;
    } else {
        return 0;
    }
}

/* decode a block and add contributions into accumulators */
static void impact_decode_block(struct chash *accs, struct term_data *term,
  unsigned int blockfine) {
    unsigned int contrib = term->impact - blockfine;
    unsigned long int docno_d;
    int ret;

    while (term->blocksize && vec_vbyte_read(&term->v, &docno_d)) {
        unsigned long int *fw;
        int found;

        term->docno += docno_d + 1;

        /* should never fail */
        ret = chash_luint_luint_find_insert(accs, term->docno, &fw, 0, &found);
        assert(ret == CHASH_OK);
        *fw += contrib;
 
        term->blocksize--;
    }
}

/* decode a block and add contributions into accumulators, but don't create new
 * accumulators */
static void impact_decode_block_and(struct chash *accs, struct term_data *term,
  unsigned int blockfine) {
    unsigned int contrib = term->impact - blockfine;
    unsigned long int docno_d;

    while (term->blocksize && vec_vbyte_read(&term->v, &docno_d)) {
        unsigned long int *fw;

        term->docno += docno_d + 1;

        if (chash_luint_luint_find(accs, term->docno, &fw) == CHASH_OK) {
            *fw += contrib;
        }
        term->blocksize--;
    }
}

static void source_delete(struct term_data *term, unsigned int terms) {
    unsigned int i;

    for (i = 0; i < terms; i++) {
        if (term[i].src) {
            term[i].src->delet(term[i].src);
            term[i].src = NULL;
        }
    }
}

enum search_ret impact_ord_eval(struct index *idx, struct query *query, 
  struct chash *accumulators, unsigned int acc_limit, struct alloc *alloc, 
  unsigned int mem) {
    double norm_B;
    unsigned int i,
                 terms = 0,
                 blockfine,
                 blocks_read,
                 postings_read = 0,
                 postings = 0,
                 bytes = 0,
                 bytes_read = 0;
    struct term_data *term,
                     *largest;
    struct disksrc *dsrc;

    if (query->terms == 0) {
        /* no terms to process */
        return SEARCH_OK;
    /* allocate space for array */
    } else if (!(term = malloc(sizeof(*term) * query->terms))) {
        return SEARCH_ENOMEM;
    }

    /* sort by selectivity (by inverse t_f) */
    qsort(query->term, query->terms, sizeof(*query->term), f_t_cmp);

    norm_B = pow(idx->impact_stats.w_qt_max / idx->impact_stats.w_qt_min,
        idx->impact_stats.w_qt_min 
          / (idx->impact_stats.w_qt_max - idx->impact_stats.w_qt_min));

    /* initialise data for each query term */
    for (i = 0; i < query->terms; i++) {
        unsigned int termfine;
        double w_qt;

        /* initialise src/vec for term */
        term[i].v.pos = term[i].v.end = NULL;
        term[i].src = NULL;

        w_qt = (1 + log(query->term[i].f_qt)) *
          log(1 + (idx->impact_stats.avg_f_t / query->term[i].f_t));
        w_qt = impact_normalise(w_qt, norm_B, 
            idx->impact_stats.slope, idx->impact_stats.w_qt_max, 
            idx->impact_stats.w_qt_min);
        term[i].w_qt = impact_quantise(w_qt, 
            idx->impact_stats.quant_bits, idx->impact_stats.w_qt_max, 
            idx->impact_stats.w_qt_min);

        /* apply term fine to term impact */
        termfine = (i < 2) ? 0 : i - 2;
        if (termfine < term[i].w_qt) {
            term[i].w_qt -= termfine;
            /* initialise to highest impact, so we'll select and initialise this
             * term before real processing */
            term[i].impact = INT_MAX;
            terms++;
        } else {
            /* we won't use this term */
            term[i].w_qt = 0;
            term[i].impact = 0;
        }
        term[i].blocksize = 0;

        /* XXX */
        postings += query->term[i].f_t;
        bytes += query->term[i].term.vocab.size;
    }

    /* get sources for each term (do this in a seperate loop so we've already
     * excluded lists that we won't use) */
    for (i = 0; i < terms; i++) {
        unsigned int memsize = mem / (terms - i);

        if (memsize > query->term[i].term.vocab.size) {
            memsize = query->term[i].term.vocab.size;
        }

        if (!(term[i].src 
          = search_term_src(idx, &query->term[i].term, alloc, memsize))) {
            source_delete(term, terms);
            free(term);
            return SEARCH_EINVAL;
        }

        mem -= memsize;
    }

    blockfine = blocks_read = 0;
    heap_heapify(term, terms, sizeof(*term), term_data_cmp);

    do {
        largest = heap_pop(term, &terms, sizeof(*term), term_data_cmp);

        if (largest && (largest->impact > blockfine)) {
            postings_read += largest->blocksize;
            if (chash_size(accumulators) < acc_limit) {
                /* reserve enough memory for accumulators and decode */
                if (chash_reserve(accumulators, largest->blocksize) 
                  >= largest->blocksize) {
                    impact_decode_block(accumulators, largest, blockfine);
                } else {
                    assert(!CRASH); ERROR("impact_ord_eval()");
                    source_delete(term, terms);
                    free(term);
                    return SEARCH_EINVAL;
                }
            } else {
                impact_decode_block_and(accumulators, largest, blockfine);
            }

            if (VEC_LEN(&largest->v) < 2 * VEC_VBYTE_MAX) {
                /* need to read more data */
                unsigned int bytes;
                enum search_ret sret;

                if ((sret 
                  = largest->src->readlist(largest->src, VEC_LEN(&largest->v), 
                    (void **) &largest->v.pos, &bytes)) == SEARCH_OK) {

                    /* read succeeded */
                    largest->v.end = largest->v.pos + bytes;
                } else if (sret == SEARCH_FINISH) {
                    if (VEC_LEN(&largest->v) || largest->blocksize) {
                        /* didn't finish properly */
                        assert(!CRASH); ERROR("impact_ord_eval()");
                        source_delete(term, terms);
                        free(term);
                        return SEARCH_EINVAL;
                    }
                    /* otherwise it will be finished below */
                } else {
                    assert(!CRASH); ERROR("impact_ord_eval()");
                    source_delete(term, terms);
                    free(term);
                    return sret;
                }
            }

            if (!largest->blocksize) {
                /* need to read the start of the next block */
                unsigned long int tmp_bsize,
                                  tmp_impact;

                if (vec_vbyte_read(&largest->v, &tmp_bsize)
                  && (vec_vbyte_read(&largest->v, &tmp_impact) 
                    /* second read failed, rewind past first vbyte */
                    || ((largest->v.pos -= vec_vbyte_len(tmp_bsize)), 0))) {

                    blocks_read++;
                    if (blocks_read > terms) {
                        blockfine++;
                    }

                    largest->blocksize = tmp_bsize;
                    largest->impact = (tmp_impact + 1) * largest->w_qt;
                    largest->docno = -1;
                    heap_insert(term, &terms, sizeof(*term), term_data_cmp, 
                      largest);
                } else if (!VEC_LEN(&largest->v)) {
                    /* finished, don't put back on the heap */
                    dsrc = (void *) largest->src; bytes_read += dsrc->pos;
                    largest->src->delet(largest->src);
                    largest->src = NULL;
                } else if (largest->impact != INT_MAX) {
                    /* ensure that this vector is chosen next, as we need the
                     * next impact score */
                    largest->impact = INT_MAX;
                    assert(largest->blocksize == 0);
                    heap_insert(term, &terms, sizeof(*term), term_data_cmp, 
                      largest);
                } else {
                    /* huh? */
                    assert(!CRASH); ERROR("impact_ord_eval()");
                    source_delete(term, terms);
                    free(term);
                    return SEARCH_EINVAL;
                }
            } else {
                heap_insert(term, &terms, sizeof(*term), term_data_cmp, 
                  largest);
            }
        }
    } while (largest && (largest->impact > blockfine));

    for (i = 0; i < terms; i++) {
        dsrc = (void *) term[i].src; bytes_read += dsrc->pos;
    }

    if (largest) {
        largest->src->delet(largest->src);
        largest->src = NULL;
    }

    /* end of ranking */
    source_delete(term, terms);
    free(term);
    return SEARCH_OK;
}

