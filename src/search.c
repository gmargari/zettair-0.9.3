/* search.c implements operations needed to search an index
 *
 * written nml 2003-06-02
 *
 */

#include "firstinclude.h"
#include "search.h"

#include "_index.h"

#include "bit.h"
#include "binsearch.h"
#include "bucket.h"
#include "chash.h"
#include "_chash.h"
#include "def.h"
#include "heap.h"
#include "impact.h"
#include "index.h"
#include "index_querybuild.h"
#include "queryparse.h"
#include "str.h"
#include "vocab.h"
#include "objalloc.h"
#include "poolalloc.h"
#include "docmap.h"
#include "vec.h"
#include "fdset.h"
#include "metric.h"
#include "postings.h"
#include "summarise.h"
#include "error.h"

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

/* number of significant digits in estimated results */
#define RESULTS_SIGDIGITS 3

/* functions to return sources from terms and conjuncts */
struct search_list_src *memsrc_new_fromdisk(struct index *idx, 
  unsigned int type, unsigned int fileno, unsigned long int offset, 
  unsigned int size, struct alloc *alloc, void **retbuf);
struct search_list_src *search_term_src(struct index *idx, struct term *term,
  struct alloc *alloc, unsigned int memlimit);
struct search_list_src *search_conjunct_src(struct index *idx, 
  struct conjunct *conj, struct alloc *alloc, unsigned int memlimit);

/* internal function to compare phrase term pointers by frequency/estimated
 * frequency */
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

/* internal function to compare phrase term pointers by frequency/estimated
 * frequency */
static int F_t_cmp(const void *one, const void *two) {
    const struct conjunct *done = one,
                          *dtwo = two;

    if (done->F_t < dtwo->F_t) {
        return -1;
    } else if (done->F_t > dtwo->F_t) {
        return 1;
    } else {
        return 0;
    }
}

/* internal function to compare accumulators based on accumulated weight */
static int accumulator_cmp(const void *vone, const void *vtwo) {
    const struct search_acc *one = vone,
                            *two = vtwo;

    if (one->weight < two->weight) {
        return -1;
    } else if (one->weight > two->weight) {
        return 1;
    } else {
        /* sub-rank by docid, so that at least we impose a clear, deterministic
         * order upon the results.  Two accumulators with the same score are
         * unlikely anyway. */
        if (one->docno < two->docno) {
            return -1;
        } else if (one->docno > two->docno) {
            return 1;
        } else {
            /* shouldn't get two accumulators with the same docno */
            assert(0);
            return 0;
        }
    }
}

/* internal functions to sort accumulators in a list into an array */
static void sort_list(struct search_acc *heap, unsigned int heapsize, 
  struct search_acc_cons *acc) {
    unsigned int i;
    struct search_acc *lowest;
    float lowest_weight;

    /* fill heap with accumulators in the order they occur */
    for (i = 0; i < heapsize; i++, acc = acc->next) {
        assert(acc);
        heap[i] = acc->acc;
    }

    /* heapify heap, so the we know what the lowest accumulator in it is */
    heap_heapify(heap, heapsize, sizeof(*heap), accumulator_cmp);
    lowest = heap_peek(heap, heapsize, sizeof(*heap));
    lowest_weight = lowest->weight;

    /* continue traversing accumulators, replacing lowest element whenever we 
     * find one higher than it */
    while (acc) {
        if (acc->acc.weight > lowest_weight) {
            /* replace smallest element in heap with a larger one */
            heap_replace(heap, heapsize, sizeof(*heap), 
              accumulator_cmp, &acc->acc);
            lowest = heap_peek(heap, heapsize, sizeof(*heap));
            lowest_weight = lowest->weight;
        }
        acc = acc->next;
    }

    /* we now have all of the largest accumulators.  Continue heaping, taking
     * the smallest element from the back of the array and copying it to the
     * front. */
    while (heapsize > 1) {
        /* note: shrink heapsize by one, copy smallest element into space
         * we just past */
        heap_pop(heap, &heapsize, sizeof(*heap), accumulator_cmp);
    }
}

/* internal functions to sort accumulators in a list into an array */
static void sort_hash(struct search_acc *heap, unsigned int heapsize, 
  struct chash *acc) {
    unsigned int i,
                 j,
                 hashsize = chash_size(acc);
    struct chash_link *link = NULL;
    struct search_acc *lowest,
                      tmp;
    float lowest_weight;

    /* fill heap with accumulators in the order they occur */
    for (i = 0, j = 0; j < heapsize; i++) {
        link = acc->table[i];
        while (link && (j < heapsize)) {
            heap[j].docno = link->key.k_luint;
            heap[j].weight = (float) link->data.d_luint;

            j++;
            link = link->next;
        }
    }

    /* heapify heap, so the we know what the lowest accumulator in it is */
    heap_heapify(heap, heapsize, sizeof(*heap), accumulator_cmp);
    lowest = heap_peek(heap, heapsize, sizeof(*heap));
    lowest_weight = lowest->weight;

    /* continue traversing hashtable, replacing lowest element whenever we find
     * one higher than it (note this loop finishes last chain as well) */
    do {
        while (link) {
            if (link->data.d_luint > lowest_weight) {
                /* replace smallest element in heap with a larger one */
                tmp.docno = link->key.k_luint;
                tmp.weight = (float) link->data.d_luint;
                heap_replace(heap, heapsize, sizeof(*heap), 
                  accumulator_cmp, &tmp);
                lowest = heap_peek(heap, heapsize, sizeof(*heap));
                lowest_weight = lowest->weight;
            }
            j++;
            link = link->next;
        }
    } while ((j < hashsize)
      && ((link = acc->table[i++]), 1)); /* get next entry and cont */

    assert((j == chash_size(acc)) && (j == hashsize));

    /* we now have all of the largest accumulators.  Continue heaping, taking
     * the smallest element from the back of the array and copying it to the
     * front. */
    while (heapsize > 1) {
        /* note: shrink heapsize by one, copy smallest element into space
         * we just past */
        heap_pop(heap, &heapsize, sizeof(*heap), accumulator_cmp);
    }
}

/* internal function to select the top len results, starting from startdoc, 
 * into the results array from accumulators, either hashed or in a list. */
static unsigned int index_heap_select(struct index *idx, 
  struct index_result *results, unsigned int startdoc, unsigned int len, 
  struct search_acc_cons *acc, unsigned int accs, struct chash *hashacc) {
    struct search_acc *heap;
    unsigned int i,
                 numdocs = startdoc + len,
                 heapsize;

    if (accs <= startdoc) {
        /* not enough accumulators to get to desired result */
        return 0;
    } else if (numdocs > accs) {
        /* not enough accumulators to get all of desired results */
        numdocs = accs;
    }

    /* allocate a heap of startdoc + len elements */
    if (!(heap = malloc(sizeof(*heap) * numdocs))) {
        return 0;
    }
    heapsize = numdocs;

    if (!hashacc) {
        /* accumulators as list */
        sort_list(heap, heapsize, acc);
    } else {
        /* accumulators as hash table */
        sort_hash(heap, heapsize, hashacc);
    }

    /* copy relevant accumulators into results (this loop form works properly 
     * if less than startdoc + len documents match the query - think before 
     * changing it) */ 
    for (i = startdoc; i < numdocs; i++) {
        unsigned aux_len = 0;
        enum docmap_ret ret;

        results[i - startdoc].docno = heap[i].docno;
        results[i - startdoc].score = heap[i].weight;

        /* lookup document auxilliary */
        ret = docmap_get_trecno(idx->map, heap[i].docno, 
          results[i - startdoc].auxilliary, INDEX_AUXILIARYLEN, &aux_len);
        if (ret != DOCMAP_OK) {
            return 0;
        }
        if (aux_len > INDEX_AUXILIARYLEN) {
            aux_len = INDEX_AUXILIARYLEN;
        }
        results[i - startdoc].auxilliary[aux_len] = '\0';

        /* summary and title provided later */
        results[i - startdoc].summary[0] = '\0';
        results[i - startdoc].title[0] = '\0';
    }

    free(heap);                        /* free heap */
    return i - startdoc;
}

/* conjunct processing stuff */

/* structure to hold information to process phrases/conjuncts */
struct phrase_pos {
    unsigned long int docno;         /* document number */
    unsigned long int term;          /* term number of start of phrase in doc */
    unsigned long int f_dt;          /* number of occurances left in document */
    unsigned int term_offset;        /* offset of term from start of phrase */
    struct vec vec;                  /* currently available vector */
    struct search_list_src *src;     /* list source */
    unsigned int bytes;              /* total size of vector */
    unsigned int slop;               /* how exact the phrase matching has to 
                                      * be */
    struct term *src_term;            /* term that this pos refers to */
};

/* internal function to increment a phrase position to at least minpos offset
 * within mindoc document */
static int phrase_inc(struct phrase_pos *pp, unsigned long int mindoc, 
  unsigned long int minpos) {
    unsigned int bytes;
    unsigned long int tmp,
                      tmp2;
    void *startpos = pp->vec.pos;

    /* increment past docs we know we're not interested in */
    while ((pp->docno < mindoc) 
      /* read a docno if we haven't got one yet */
      || (pp->docno == -1)) {
        if (!pp->f_dt 
          || ((tmp = vec_vbyte_scan(&pp->vec, pp->f_dt, &bytes)) == pp->f_dt)) {
            pp->f_dt = 0;
            startpos = pp->vec.pos;   /* we can start from this point again */
        } else {
            /* need more data */
            pp->f_dt -= tmp;
            return 0;
        }

        assert(!pp->f_dt);
        if (vec_vbyte_read(&pp->vec, &tmp) 
          && vec_vbyte_read(&pp->vec, &pp->f_dt) 
          && vec_vbyte_read(&pp->vec, &pp->term)) {
            assert(pp->f_dt);
            pp->term += pp->term_offset;
            pp->docno += tmp + 1;        /* + 1 to negate encoding */
            pp->f_dt--;
        } else {
            /* need more data */
            pp->f_dt = 0;
            pp->vec.pos = startpos;
            return 0;
        }
    }

    /* don't increment offsets if we've passed the minimum document needed */
    if (pp->docno > mindoc) {
        return 1;
    }

    /* increment past offsets we know we're not interested in */
    assert(pp->docno == mindoc);
    while (pp->term < minpos) {
        startpos = pp->vec.pos;
        if (pp->f_dt && vec_vbyte_read(&pp->vec, &tmp)) {
            pp->term += tmp + 1;    /* + 1 to negate encoding */
            pp->f_dt--;
        } else if (!pp->f_dt && vec_vbyte_read(&pp->vec, &tmp) 
          && vec_vbyte_read(&pp->vec, &tmp2) 
          && vec_vbyte_read(&pp->vec, &pp->term)) {
            /* skipped to new document, so we're no longer interested in
             * incrementing offsets */
            pp->term += pp->term_offset;
            pp->docno += tmp + 1;        /* + 1 to negate encoding */
            pp->f_dt = tmp2 - 1;
            return 1;
        } else {
            /* need more data */
            pp->vec.pos = startpos;
            return 0;
        }
    }

    return 1;
}

/* internal function to compare phrase positions */
static int pp_cmp(const void *vone, const void *vtwo) {
    const struct phrase_pos *one = vone,
                            *two = vtwo;

    if (one->docno < two->docno) {
        return -1;
    } else if (one->docno > two->docno) {
        return 1;
    } else {
        return one->term - two->term;
    }
}

/* internal function to compare phrase positions with slop factor */
static int slop_cmp(const void *vone, const void *vtwo) {
    const struct phrase_pos *one = vone,
                            *two = vtwo;

    if (one->docno < two->docno) {
        return -1;
    } else if (one->docno > two->docno) {
        return 1;
    } else {
        if (one->term < two->term) {
            if (one->term < two->term - two->slop) {
                return -1;
            } else {
                return 0;
            }
        } else {
            if (one->term > two->term + two->slop) {
                return 1;
            } else {
                return 0;
            }
        }
    }
}

/* internal function to compare conjunctive AND positions */
static int and_cmp(const void *vone, const void *vtwo) {
    const struct phrase_pos *one = vone,
                            *two = vtwo;

    if (one->docno < two->docno) {
        return -1;
    } else if (one->docno > two->docno) {
        return 1;
    } else {
        return 0;
    }
}

/* internal function to compare phrase positions based on total list size */
static int pp_size_cmp(const void *vone, const void *vtwo) {
    const struct phrase_pos *one = vone,
                            *two = vtwo;

    return (int) (one->bytes - two->bytes);
}

static void phrase_cleanup(struct phrase_pos *pp, unsigned int len) {
    unsigned int i;

    for (i = 0; i < len; i++) {
        if (pp[i].src) {
            pp[i].src->delet(pp[i].src);
            pp[i].src = NULL;
        }
    }
    free(pp);
}

/* internal function to read more data into a phrase pos structure */
static enum search_ret phrase_read(struct phrase_pos *pp) {
    /* should never leave more than 3 vbytes (docno, f_dt, first offset) */
    if (VEC_LEN(&pp->vec) < 3 * VEC_VBYTE_MAX) {
        enum search_ret sret;
        void *buf;
        unsigned int remaining = VEC_LEN(&pp->vec),
                     bytes;

        if ((sret = pp->src->readlist(pp->src, remaining, &buf, &bytes)) 
          == SEARCH_OK) {
            /* buf read succeeded, reset vector to read from new stuff */
            pp->vec.pos = buf;
            pp->vec.end = pp->vec.pos + bytes;
            return SEARCH_OK;
        } else if ((sret == SEARCH_OK && !bytes && !remaining) 
          || sret == SEARCH_FINISH) {
            return SEARCH_FINISH;
        } else {
            /* failed to read next chunk */
            return sret;
        }
    } else {
        /* huh? vbyte overflowed, which is bad */
        return SEARCH_EINVAL;
    }
}

static enum search_ret phrase_write(struct conjunct *conj, struct vec *match, 
  unsigned long int match_docno, unsigned long int *last_docno, 
  unsigned long int f_dt) {
    /* ensure that we have enough memory to encode match_docno, f_dt and 
     * offsets */
    while (VEC_LEN(match) < 2 * VEC_VBYTE_MAX + f_dt) {
        unsigned int pos = match->pos - conj->vecmem;
        unsigned int len = pos + VEC_LEN(match);
        void *ptr = realloc(conj->vecmem, 2 * len);

        if (ptr) {
            conj->vecmem = match->pos = ptr;
            match->end = match->pos + 2 * len;
            match->pos += pos;
        } else {
            return SEARCH_ENOMEM;
        }
    }

    conj->f_t++;
    conj->F_t += f_dt;
    vec_vbyte_write(match, match_docno - (*last_docno + 1));
    *last_docno = match_docno;
    vec_vbyte_write(match, f_dt);
    /* just encode 0 offset gaps, as this is easy and we don't need the
     * offsets anywhere else.  In theory the offsets might be interesting
     * for phrase matches, but this is complex enough without worrying about
     * that as well.  We should really produce vectors without offsets, but we
     * can't handle that yet. */
    while (f_dt--) {
        vec_vbyte_write(match, 0);
    }

    conj->vecsize = match->pos - conj->vecmem;
    return SEARCH_OK;
}

/* internal function to return the maximum amount of memory required to
 * evaluate a conjunct */
unsigned int process_conjunct_mem(struct conjunct *conj) {
    struct term *currterm;
    unsigned int mem = 0;

    for (currterm = &conj->term; currterm; currterm = currterm->next) {
        mem += currterm->vocab.size;
    }

    return mem;
}

/* internal function to process a phrase into an in-memory return 
 * vector (retvec, of length retlen).  Returns SEARCH_OK on success or an error
 * indication.  alloc is an object used to allocate vector memory, and mem the
 * amount of memory available from that allocation source. */
static enum search_ret process_conjunct(struct index *idx, 
  struct conjunct *conj, struct alloc *alloc, unsigned int mem) {
    struct phrase_pos *pp;                 /* array of phrase terms */
    unsigned int i;
    struct vec match;                     /* vector that we are producing */
    unsigned long int match_docno,
                      last_docno = -1;
    unsigned int f_dt,
                 and_mask;
    int (*cmp)(const void *one, const void *two);
    enum search_ret sret;
    struct term *currterm;

/* macro to perform reads into the phrase position structure buffers */
#define READ(pp, inmatch, arr)                                                \
    if (1) {                                                                  \
        sret = phrase_read(pp);                                               \
                                                                              \
        switch (sret) {                                                       \
        case SEARCH_OK:                                                       \
            /* read succeeded, continue on */                                 \
            break;                                                            \
                                                                              \
        case SEARCH_FINISH:                                                   \
            if (inmatch) {                                                    \
                if ((sret                                                     \
                  = phrase_write(conj, &match, match_docno, &last_docno,      \
                      f_dt))                                                  \
                    != SEARCH_OK) {                                           \
                    phrase_cleanup(arr, conj->terms);                         \
                    free(conj->vecmem);                                       \
                    conj->vecmem = NULL;                                      \
                    return sret;                                              \
                }                                                             \
            }                                                                 \
            phrase_cleanup(arr, conj->terms);                                 \
            return SEARCH_OK;                                                 \
                                                                              \
        default:                                                              \
            phrase_cleanup(arr, conj->terms);                                 \
            free(conj->vecmem);                                               \
            conj->vecmem = NULL;                                              \
            return sret;                                                      \
        }                                                                     \
    } else 

    /* shouldn't conjunctively process one term phrases */
    if (conj->terms == 1) {
        /* dodgy, but simplest fix is just to mark it as non-conjunctive */
        conj->type = CONJUNCT_TYPE_WORD;
        conj->f_t = conj->term.vocab.header.docwp.docs;
        conj->F_t = conj->term.vocab.header.docwp.occurs;
        return SEARCH_OK;
    }

    if (!conj->terms || (((conj->type != CONJUNCT_TYPE_PHRASE)) 
      && (conj->type != CONJUNCT_TYPE_AND))) {
        return SEARCH_EINVAL;
    } else if (!(pp = malloc(sizeof(*pp) * conj->terms)) 
      || !(match.pos = conj->vecmem = malloc(INITVECLEN))) {
        if (pp) {
            free(pp);
        }
        return SEARCH_ENOMEM;
    }

    if (!conj->cutoff) {
        /* translate 0 (no limit) to highest possible number */
        conj->cutoff = UINT_MAX;
    }

    conj->vecsize = 0;
    match.end = match.pos + INITVECLEN;
    conj->f_t = conj->F_t = 0;
    and_mask = 0;
    cmp = and_cmp;
    if (conj->type == CONJUNCT_TYPE_PHRASE) {
        and_mask = BIT_LMASK(BIT_FROM_BYTE(sizeof(unsigned long int)));
        cmp = pp_cmp;
        if (conj->sloppiness) {
            cmp = slop_cmp;
        }
    }

    /* initialise each of the phrasepos structures */
    for (currterm = &conj->term, i = 0; (i < conj->terms) && currterm; 
      i++, currterm = currterm->next) {
        pp[i].f_dt = 0;
        pp[i].docno = -1;   /* note: remove excess + 1 from decoding method */
        pp[i].term = 0;
        pp[i].term_offset = conj->terms - (i + 1);
        pp[i].bytes = currterm->vocab.size;
        pp[i].slop = conj->sloppiness;
        pp[i].src = NULL;
        pp[i].vec.pos = pp[i].vec.end = NULL;
        pp[i].src_term = currterm;
    }

    /* sort by list size */
    qsort(pp, conj->terms, sizeof(*pp), pp_size_cmp);

    /* allocate memory to individual members of the phrase on the basis that
     * lists should have approximately the same memory allocation (not
     * propotionally to size), except when they're too short to require it. */
    for (i = 0; i < conj->terms; i++) {
        unsigned int size = mem / (conj->terms - i);

        if (pp[i].bytes < size) {
            size = pp[i].bytes;
        }

        sret = SEARCH_ENOMEM;
        if ((pp[i].src = search_term_src(idx, pp[i].src_term, alloc, size)) 
          && (sret = pp[i].src->reset(pp[i].src->opaque)) == SEARCH_OK) {
            /* read data into the buffer */
            do {
                /* need more data */
                READ(&pp[i], 0, pp);
            } while (!phrase_inc(&pp[i], 0, 0));
            mem -= size;
        } else {
            /* failed, free resources */
            if (pp[i].src) {
                pp[i].src->delet(pp[i].src);
            }
            free(conj->vecmem); conj->vecmem = NULL;
            phrase_cleanup(pp, conj->terms);
            return sret;
        }
    }

    /* resolve the phrase by repeatedly find the highest element in the set and
     * incrementing all the other terms past this point.  Detect the case when
     * they're all equal.  We could use a heap to find the highest element, but
     * this doesn't seem useful, as we destroy and would be forced to rebuild
     * the heap for each iteration, at NlogN cost. */
    while (1) {
        struct phrase_pos *highest = &pp[0];
        unsigned int min_f_dt = pp[0].f_dt;
        int equal = 1;
        int cmpval;
        int cutoff = 0;

        /* don't accept matches over the cutoff */
        if (pp[0].term >= conj->cutoff) {
            equal = 0;
            cutoff = 1;
        }

        for (i = 1; i < conj->terms; i++) {
            if ((cmpval = cmp(highest, &pp[i])) < 0) {
                /* current element is new highest */
                equal = 0;
                highest = &pp[i];
            } else if (cmpval > 0) {
                equal = 0;
            } else {
                if (pp[i].f_dt < min_f_dt) {
                    /* keep the minimum f_dt value for AND conjuncts */
                    min_f_dt = pp[i].f_dt;
                }
                if (pp[i].term >= conj->cutoff) {
                    equal = 0;
                    cutoff = 1;
                }
            }
        }

        if (!equal || (conj->type == CONJUNCT_TYPE_AND)) {
            /* we either need to increment everything up to the highest current
             * docno/term, or we've matched an AND and need to increment
             * everything *over* the current docno so that we'll resume 
             * matching after this docno. */
            unsigned long int docno = match_docno = highest->docno;
            f_dt = min_f_dt + 1;

            if (equal) {
                assert(conj->type == CONJUNCT_TYPE_AND);
                /* we matched an AND - increment over this doc */
                docno++;

                /* have to write match out now, so that it occurs before
                 * incrementing */
                if ((sret 
                  = phrase_write(conj, &match, match_docno, &last_docno, f_dt)) 
                    != SEARCH_OK) {

                    phrase_cleanup(pp, conj->terms);
                    free(conj->vecmem);
                    conj->vecmem = NULL;
                    return sret;
                }
                equal = 0;  /* ensure we don't rewrite answer below */
            } else if (cutoff) {
                /* must go to next doc if we've reached the cutoff, else we 
                 * risk infinite looping */
                docno++;
            }

            /* increment all positions */
            for (i = 0; i < conj->terms; i++) {
                while (!phrase_inc(&pp[i], docno, highest->term & and_mask)) {
                    /* need more data */
                    READ(&pp[i], 0, pp);
                }
            }
        } else {
            /* matched a phrase conjunct, use the same approach as above to 
             * establish how many times the match occurs in the document */
            assert(equal && (conj->type != CONJUNCT_TYPE_AND));
            match_docno = highest->docno;
            f_dt = 0;

            while (1) {
                unsigned long int term;

                highest = &pp[0];
                equal = 1;

                for (i = 0; i < conj->terms; i++) {
                    if (pp[i].docno != match_docno) {
                        /* finished match */
                        break;
                    } else if ((cmpval = cmp(highest, &pp[i])) < 0) {
                        /* current element is new highest */
                        equal = 0;
                        highest = &pp[i];
                    } else if (cmpval > 0) {
                        equal = 0;
                    }
                }

                if (i != conj->terms) {
                    /* broke during loop, finished match, now break outer 
                     * loop */
                    break;
                }

                if (!equal) {
                    term = highest->term;
                } else {
                    f_dt++;
                    term = highest->term + 1;
                }

                /* increment all positions up to the current highest */
                for (i = 0; i < conj->terms; i++) {
                    while (!phrase_inc(&pp[i], match_docno, term)) {
                        /* need more data */
                        READ(&pp[i], 1, pp);
                    }
                }
            }
        }

        if (equal) {
            /* now we know the match document number and f_dt of the match, 
             * just have to encode it */
            if ((sret 
              = phrase_write(conj, &match, match_docno, &last_docno, f_dt)) 
                != SEARCH_OK) {

                phrase_cleanup(pp, conj->terms);
                free(conj->vecmem);
                conj->vecmem = NULL;
                return sret;
            }
        }
    }
    /* note that the loop above terminates only when the end of one of the 
     * sources is reached */
#undef READ
}

struct termsrc {
    struct conjunct *term;
    struct search_list_src *src;
};

/* internal function to compare phrase term pointers by frequency/estimated
 * frequency */
static int loc_cmp(const void *one, const void *two) {
    const struct termsrc *done = one,
                         *dtwo = two;

    /* order terms that have already been read toward the bottom */
    if (done->term->vecmem || dtwo->term->vecmem) {
        if (done->term->vecmem) {
            return 1;
        } else {
            return -1;
        }
    }

    assert(done->term->term.vocab.location == VOCAB_LOCATION_FILE);
    assert(dtwo->term->term.vocab.location == VOCAB_LOCATION_FILE);

    if (done->term->term.vocab.loc.file.fileno 
      == dtwo->term->term.vocab.loc.file.fileno) {
        if (done->term->term.vocab.loc.file.offset 
          < dtwo->term->term.vocab.loc.file.offset) {
            return -1;
        } else if (done->term->term.vocab.loc.file.offset 
          > dtwo->term->term.vocab.loc.file.offset) {
            return 1;
        } else {
            assert("can't get here" && 0);
            return 0;
        }
    } else {
        return dtwo->term->term.vocab.loc.file.fileno
          - done->term->term.vocab.loc.file.fileno;
    }
}

static int term_cmp(const void *one, const void *two) {
    const struct termsrc *done = one,
                         *dtwo = two;
    return done->term - dtwo->term;
}

static struct search_list_src *memsrc_new_from_disk(struct index *idx, 
  unsigned int type, unsigned int fileno, unsigned long int offset, 
  unsigned int size, void *mem);

/* internal function to evaluate a query structure using document ordered
 * inverted lists and place the results into an accumulator linked list */
enum search_ret doc_ord_eval(struct index *idx, struct query *query,
  struct poolalloc *list_alloc, unsigned int list_mem_limit,
  struct search_metric_results *results,
  int opts, struct index_search_opt *opt) {
    unsigned int i,
                 small,
                 memsum;
    enum search_ret ret;
    const struct search_metric *sm;
    struct search_list_src *src;
    struct alloc alloc = {NULL, (alloc_mallocfn) poolalloc_malloc, 
                          (alloc_freefn) poolalloc_free};
    int (*selectivity_cmp)(const void *one, const void *two) = f_t_cmp;
    struct termsrc *srcarr = malloc(sizeof(*srcarr) * query->terms);
    struct index_search_opt spareopt;

    if (!srcarr) {
        return SEARCH_ENOMEM;
    }

    alloc.opaque = list_alloc;

    /* choose metric */
    if (opts & INDEX_SEARCH_DIRICHLET_RANK) {
        selectivity_cmp = F_t_cmp;
        sm = dirichlet();
    } else if (opts & INDEX_SEARCH_OKAPI_RANK) {
        sm = okapi_k3();
    } else if (opts & INDEX_SEARCH_PCOSINE_RANK) {
        sm = pcosine();
    } else if (opts & INDEX_SEARCH_COSINE_RANK) {
        sm = cosine();
    } else if (opts & INDEX_SEARCH_HAWKAPI_RANK) {
        sm = hawkapi();
    } else {
        /* default is dirichlet with mu = 1500.  Fiddle with opt structure to
         * ensure everything works ok even if people pass a NULL */
        selectivity_cmp = F_t_cmp;
        sm = dirichlet();
        if (!opt) {
            opt = &spareopt;
            opts |= INDEX_SEARCH_DIRICHLET_RANK;
        }
        opt->u.dirichlet.mu = 1500;
    }

    if ((ret = sm->pre(idx, query, opts, opt)) != SEARCH_OK) {
        free(srcarr);
        return ret;
    }

    /* sort by selectivity */
    qsort(query->term, query->terms, sizeof(*query->term), selectivity_cmp);

    /* see how many will fit simultaneously in the amount of memory we have */
    for (small = 0, memsum = 0; small < query->terms; small++) {
        srcarr[small].term = &query->term[small];
        srcarr[small].src = NULL;
        if (query->term[small].type == CONJUNCT_TYPE_WORD 
          && query->term[small].term.vocab.location == VOCAB_LOCATION_FILE) {
            if (memsum + query->term[small].term.vocab.size > list_mem_limit) {
                break;
            }
            memsum += query->term[small].term.vocab.size;
        }
    }

    /* initialise the rest of the sources to NULL */
    for (i = small; i < query->terms; i++) {
        srcarr[i].term = NULL;
        srcarr[i].src = NULL;
    }

    /* sort small vectors by disk location */
    qsort(srcarr, small, sizeof(*srcarr), loc_cmp);

    /* read them all off of disk, in location order.  This speeds disk transfer,
     * by minimising the amount of seeking that needs to be done. */
    for (i = 0; i < small; i++) {
        void *mem;

        if ((srcarr[i].term->type == CONJUNCT_TYPE_WORD)
          && (srcarr[i].term->term.vocab.location == VOCAB_LOCATION_FILE)
          && (mem 
            = poolalloc_malloc(list_alloc, srcarr[i].term->term.vocab.size))
          && (srcarr[i].src = memsrc_new_from_disk(idx, idx->index_type, 
              srcarr[i].term->term.vocab.loc.file.fileno, 
              srcarr[i].term->term.vocab.loc.file.offset, 
              srcarr[i].term->term.vocab.size, mem))) {
            /* succeeded, do nothing */
        } else if ((srcarr[i].term->type == CONJUNCT_TYPE_WORD)
          && (srcarr[i].term->term.vocab.location == VOCAB_LOCATION_FILE)) {
            /* failed to allocate or read new source */
            free(srcarr);
            return SEARCH_ENOMEM;
        }
    }

    /* sort by term again */
    qsort(srcarr, small, sizeof(*srcarr), term_cmp);

    /* process terms that have no chance of overflowing the accumulator limit 
     * in OR mode */
    for (i = 0; (i < query->terms) 
        && (results->accs + query->term[i].f_t < results->acc_limit); i++) {

        /* reserve enough accumulators for the entire list */
        if (objalloc_reserve(results->alloc, query->term[i].f_t) 
          < query->term[i].f_t) {
            free(srcarr);
            return SEARCH_ENOMEM;
        }

        assert(srcarr[i].term == &query->term[i] || !srcarr[i].term);
        if (((src = srcarr[i].src) 
            || (src = search_conjunct_src(idx, &query->term[i], &alloc, 
                list_mem_limit)))
          && (ret = sm->or_decode(idx, query, i, SEARCH_DOCNO_START, results,
            src, opts, opt)) == SEARCH_OK) {
            src->delet(src);
            if (list_alloc && (i + 1 >= small)) {
                poolalloc_clear(list_alloc);
            }
        } else {
            if (src) {
                src->delet(src);
                free(srcarr);
                return ret;
            } else {
                free(srcarr);
                return SEARCH_ENOMEM;
            }
        }
    }
    assert((i == query->terms) 
      || (results->accs + query->term[i].f_t >= results->acc_limit));

    /* process terms that may overflow the accumulator limit in THRESH mode */
    ret = SEARCH_OK;
    for (; (i < query->terms) && (ret == SEARCH_OK); i++) {
        /* don't perform thresholding for a small number of 
         * accumulators... */
        if ((((results->acc_limit - results->accs) / (float) results->acc_limit)
            < SEARCH_SAMPLES_MIN)
          /* where it's also a small percentage of the list */
          && (((results->acc_limit - results->accs) 
              / (float) query->term[i].f_t) 
            < SEARCH_SAMPLES_MIN)) {
            break;
        }

        assert(srcarr[i].term == &query->term[i] || !srcarr[i].term);
        if (((src = srcarr[i].src) 
            || (src 
              = search_conjunct_src(idx, &query->term[i], &alloc, 
                  list_mem_limit)))
          && (((ret = sm->thresh_decode(idx, query, i, SEARCH_DOCNO_START, 
              results, src, query->term[i].f_t, opts, opt)) 
            == SEARCH_OK)
          || (ret == SEARCH_FINISH))) {
            src->delet(src);
            if (list_alloc && (i + 1 >= small)) {
                poolalloc_clear(list_alloc);
            }
        } else {
            if (src) {
                src->delet(src);
                free(srcarr);
                return ret;
            } else {
                free(srcarr);
                return SEARCH_ENOMEM;
            }
        }
    }

    /* process terms after accumulator limit has been reached in AND mode */
    for (; i < query->terms; i++) {
        assert(srcarr[i].term == &query->term[i] || !srcarr[i].term);
        if (((src = srcarr[i].src) 
            || (src 
              = search_conjunct_src(idx, &query->term[i], &alloc, 
                  list_mem_limit)))
          && (ret = sm->and_decode(idx, query, i, SEARCH_DOCNO_START, results,
            src, opts, opt)) 
          == SEARCH_OK) {
            src->delet(src);
            if (list_alloc) {
                poolalloc_clear(list_alloc);
            }
        } else {
            if (src) {
                src->delet(src);
                free(srcarr);
                return ret;
            } else {
                free(srcarr);
                return SEARCH_ENOMEM;
            }
        }
    }
    free(srcarr);

    /* post-process accumulators */
    if (sm->post 
      && (ret = sm->post(idx, query, results->acc, opts, opt)) != SEARCH_OK) {
        return ret;
    }

    if (results->estimated) {
        unsigned int lg 
          = (unsigned int) ceil(log10(results->total_results));

        /* remove superfluous significant digits from the estimate */
        if (lg > RESULTS_SIGDIGITS) {
            results->total_results = ((unsigned int) (results->total_results 
              / pow(10, lg - RESULTS_SIGDIGITS))) 
                * pow(10, lg - RESULTS_SIGDIGITS);
        }
    }

    return SEARCH_OK;
}

static int res_docno_cmp(const void *vone, const void *vtwo) {
    const struct index_result *one = vone, 
                              *two = vtwo;

    if (one->docno < two->docno) {
        return -1;
    } else if (one->docno > two->docno) {
        return 1;
    } else {
        return 0;
    }
}

static int res_score_cmp(const void *vone, const void *vtwo) {
    const struct index_result *one = vone, 
                              *two = vtwo;

    /* note reverse sorting */
    if (one->score < two->score) {
        return 1;
    } else if (one->score > two->score) {
        return -1;
    } else {
        return 0;
    }
}

int index_search(struct index *idx, const char *querystr, 
  unsigned long int startdoc, unsigned long int len, 
  struct index_result *result, unsigned int *results, 
  double *total_results, int *tr_est, int opts, struct index_search_opt *opt) {
    struct query query;                  /* list of query terms/phrases */
    struct objalloc *acc_alloc;          /* allocator for accumulators */
    struct alloc list_alloc;             /* memory allocator for query 
                                          * resolution */
    struct search_acc_cons *acc = NULL;  /* accumulators */
    struct chash *hashacc = NULL;        /* hashed accumulators */
    unsigned int accs = 0,               /* number of accumulators */
                 acc_limit,              /* accumulator limit */
                 mem,                    /* amount of memory required */
                 memsum = 0;             /* total memory required */
    int ret;                             /* return value */
    unsigned int i,                      /* counter */
                 query_words;            /* number of words allowed in query */
    int summary_type;

    /* variables needed for bucket processing */
    void *bucketmem = NULL;              /* memory for holding a bucket */

    /* find out what our query word limit is */
    query_words = QUERY_WORDS;
    if (opts & INDEX_SEARCH_WORD_LIMIT) {
        query_words = opt->word_limit;
    }
    
    if (opts & INDEX_SEARCH_SUMMARY_TYPE) {
        summary_type = opt->summary_type;
    } else {
        summary_type = INDEX_SUMMARISE_NONE;
    }

    if (!(query.term = malloc(sizeof(*query.term) * query_words))) {
        return 0;
    }

    if (opts & INDEX_SEARCH_ACCUMULATOR_LIMIT) {
        acc_limit = opt->accumulator_limit;
    } else {
        /* set default acc_limit (but assign more for deep queries) */
        acc_limit = ACCUMULATOR_LIMIT;   

        /* up accumulator limit for big collections */
        if (acc_limit < docmap_entries(idx->map) / 100) {
            acc_limit = docmap_entries(idx->map) / 100;
        }

        /* up accumulator limit for large requests (this is counter-intuitive,
         * in that the results set can depend on the number of results
         * requested, but is necessary to handle situations where
         * they request large numbers of documents) */
        if (BIT_DIV2(acc_limit, 1) < startdoc + len) {
            acc_limit = BIT_MUL2(startdoc + len, 1);
        }
    }

    /* construct a query structure */
    query.terms = 0;
    if (!(index_querybuild(idx, &query, querystr, str_len(querystr), 
        query_words, idx->storage.max_termlen, 
        opts & INDEX_SEARCH_ANH_IMPACT_RANK))) {

        /* query construction failed */
        free(query.term);
        ERROR1("building query '%s'", querystr);
        return 0;
    }

    /* calculate the amount of memory required and create an allocator */
    mem = 0;
    for (i = 0; i < query.terms; i++) {
        unsigned int tmpmem;

        switch (query.term[i].type) {
        case CONJUNCT_TYPE_PHRASE:
        case CONJUNCT_TYPE_AND:
            tmpmem = process_conjunct_mem(&query.term[i]);
            if (tmpmem > mem) {
                mem = tmpmem;
            }
            break;
            
        case CONJUNCT_TYPE_WORD:
            mem = query.term[i].term.vocab.size;
            break;

        default:
            assert("not implemented yet" && 0);
            free(query.term);
            return 0;
        }
        if (mem > UINT_MAX - memsum) {
            /* prevent overflow */
            memsum = UINT_MAX;
        } else {
            memsum += mem;
        }
    }
    /* limit memory usage */
    if (memsum > idx->params.memory) {
        mem = idx->params.memory;
    } else {
        mem = memsum;
    }

    /* initialise list allocator */
    list_alloc.opaque = NULL;
    if (!(list_alloc.opaque 
      = poolalloc_new(0, mem + poolalloc_overhead_first(), NULL))) {
        free(query.term);
        return 0;
    }
    list_alloc.malloc = (alloc_mallocfn) poolalloc_malloc;
    list_alloc.free = (alloc_freefn) poolalloc_free;

    /* process phrases and AND conjuncts */
    for (i = 0; i < query.terms; i++) {
        if (query.term[i].type == CONJUNCT_TYPE_PHRASE 
          || query.term[i].type == CONJUNCT_TYPE_AND) {
            /* assign custom vector and process terms into it */
            query.term[i].vecmem = NULL;
            query.term[i].vecsize = 0;
            /* note that we can't process conjuncts using impacts */
            if (!(opts & INDEX_SEARCH_ANH_IMPACT_RANK) 
              && process_conjunct(idx, &query.term[i], &list_alloc, mem) 
              == SEARCH_OK) {
                if (list_alloc.opaque) {
                    poolalloc_clear(list_alloc.opaque);
                }
            } else {
                /* phrase processing failed */
                if (bucketmem) {
                    free(bucketmem);
                }
                free(query.term);
                ERROR("processing phrase");
                return 0;
            }
        }
    }

    /* initialise accumulator memory allocator */
    if (!(acc_alloc 
      = objalloc_new(sizeof(struct search_acc_cons), 0, 0, 4096, NULL))) {
        if (bucketmem) {
            free(bucketmem);
        }
        free(query.term);
        return 0;
    }

    /* evaluate the query */
    if (opts & INDEX_SEARCH_ANH_IMPACT_RANK) {
        ret = SEARCH_EINVAL;
        if ((hashacc = chash_luint_new(bit_log2(acc_limit), 2.0)) 
          && ((ret = impact_ord_eval(idx, &query, hashacc, acc_limit, 
              &list_alloc, mem)) 
            == SEARCH_OK)) {
            /* impact ordered evaluation succeeded */
            accs = chash_size(hashacc);
        } else {
            if (hashacc) {
                chash_delete(hashacc);
                free(query.term);
                return 0;
            }
        }
        /* XXX: set total results because we don't do it in impact_ord_eval */
        *total_results = accs; 
        *tr_est = 1;
    } else {
        struct search_metric_results results 
          = {NULL, 0, 0, NULL, FLT_MIN, 0, 0.0};
        results.acc_limit = acc_limit;
        results.alloc = acc_alloc;
        ret = doc_ord_eval(idx, &query, list_alloc.opaque, mem, &results, 
            opts, opt);
        accs = results.accs;
        acc = results.acc;
        *total_results = results.total_results; 
        *tr_est = results.estimated;
    }
    if (list_alloc.opaque) {
        poolalloc_delete(list_alloc.opaque);
    }

    /* free all vectors produced by phrase/AND processing */
    for (i = 0; i < query.terms; i++) {
        if (query.term[i].vecmem
          && ((query.term[i].type == CONJUNCT_TYPE_PHRASE)
            || (query.term[i].type == CONJUNCT_TYPE_AND))) {

            free(query.term[i].vecmem);
        }
    }

    if (ret == SEARCH_OK) {
        /* select top accumulators as results */
        *results 
          = index_heap_select(idx, result, startdoc, len, acc, accs, hashacc);
        assert(*results <= len);
        if (hashacc) {
            chash_delete(hashacc);
        }

        /* summarise the selected results if necessary */
        if (summary_type != INDEX_SUMMARISE_NONE) {
            /* sort the results by docno, which sorts them by repository
             * location, improving the efficiency of summarisation */
            qsort(result, *results, sizeof(*result), res_docno_cmp);

            for (i = 0; i < *results; i++) {
                struct summary res;

                res.summary = result[i].summary;
                res.summary_len = INDEX_SUMMARYLEN;
                res.title = result[i].title;
                res.title_len = INDEX_TITLELEN;

                if (summarise(idx->sum, result[i].docno, &query, 
                    summary_type, &res) != SUMMARISE_OK) {

                    ERROR1("creating summary for document %ul", 
                      result[i].docno);
                }
            }

            /* resort the results by score */
            qsort(result, *results, sizeof(*result), res_score_cmp);
        }
    }

    /* free terms allocated */
    for (i = 0; i < query.terms; i++) {
        struct term *currterm = &query.term[i].term;

        while (currterm) {
            if (currterm->term) {
                free(currterm->term);
            }
            currterm = currterm->next;
        }
    }

    /* get rid of the accumulators */
    objalloc_delete(acc_alloc);

    /* free dynamic memory */
    free(query.term);
    if (bucketmem) {
        free(bucketmem);
    }

    return ret == SEARCH_OK;
}

/* structure to allow sourcing of a list from a single, contiguous
 * location in memory */
struct memsrc {
    struct search_list_src src;
    void *mem;
    unsigned int len;
    int returned;
};

static enum search_ret memsrc_reset(struct search_list_src *src) {
    struct memsrc *msrc = src->opaque;

    msrc->returned = 0;
    return SEARCH_OK;
}

static enum search_ret memsrc_read(struct search_list_src *src, 
  unsigned int leftover, void **retbuf, unsigned int *retlen) {
    struct memsrc *msrc = src->opaque;

    if (leftover) {
        return SEARCH_EINVAL;
    }

    if (msrc->returned || !msrc->len) {
        return SEARCH_FINISH;
    }

    msrc->returned = 1;
    *retbuf = msrc->mem;
    *retlen = msrc->len;
    return SEARCH_OK;
}

static void memsrc_delete(struct search_list_src *src) {
    free(src);
}

static struct search_list_src *memsrc_new(void *mem, unsigned int len) {
    struct memsrc *msrc = malloc(sizeof(*msrc));

    if (msrc) {
        msrc->mem = mem;
        msrc->len = len;
        msrc->returned = 0;
        msrc->src.opaque = msrc;
        msrc->src.delet = memsrc_delete;
        msrc->src.reset = memsrc_reset;
        msrc->src.readlist = memsrc_read;
    }
    return &msrc->src;
}

static struct search_list_src *memsrc_new_from_disk(struct index *idx, 
  unsigned int type, unsigned int fileno, unsigned long int offset, 
  unsigned int size, void *mem) {
    unsigned int bytes = size;
    char *pos;
    int fd = fdset_pin(idx->fd, type, fileno, offset, SEEK_SET);
    ssize_t read_bytes;

    if ((fd >= 0) && (pos = mem)) {
        do {
            read_bytes = read(fd, pos, bytes);
        } while ((read_bytes != -1) 
          && (pos += read_bytes, bytes -= read_bytes));

        fdset_unpin(idx->fd, type, fileno, fd);
        if (!bytes) {
            return memsrc_new(mem, size);
        }
    } else {
        if (fd >= 0) {
            fdset_unpin(idx->fd, type, fileno, fd);
        }
    }
    return NULL;
}
 
/* FIXME: structure to allow sourcing of a list from a bucket on disk */

/* structure to allow sourcing of a list from an fd */
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

static enum search_ret disksrc_reset(struct search_list_src *src) {
    struct disksrc *dsrc = src->opaque;
    dsrc->pos = 0;
    return SEARCH_OK;
}

static enum search_ret disksrc_read(struct search_list_src *src, 
  unsigned int leftover, void **retbuf, unsigned int *retlen) {
    struct disksrc *dsrc = src->opaque;

    /* this routine is somewhat complex because we'd like to handle cases where
     * an initial read, a reset, followed by another read incur only one seek
     * and one read. */

    if (leftover > dsrc->bufsize || leftover >= dsrc->bufcap 
      /* if we're requesting leftover bytes, we should just have read a
       * bufferload */
      || (leftover && dsrc->bufpos + dsrc->bufsize != dsrc->pos)) {
        assert("can't get here" && 0);
        return SEARCH_EINVAL;
    }

    if (dsrc->pos >= dsrc->bufpos 
      && (dsrc->pos < dsrc->bufpos + dsrc->bufsize)) {
        unsigned int len;
        /* buffer can be used to answer request */
        assert(!leftover);

        len = dsrc->pos - dsrc->bufpos;
        *retbuf = dsrc->buf + len;
        *retlen = dsrc->bufsize - len;
        dsrc->pos += *retlen;
        return SEARCH_OK;
    } else {
        ssize_t bytes,
                cap = dsrc->size - dsrc->pos;

        if (dsrc->pos != dsrc->bufpos + dsrc->bufsize) {
            /* need to seek before reading */
            off_t newoff;
            assert(!leftover);

            dsrc->bufsize = 0;
            if ((newoff = lseek(dsrc->fd, dsrc->offset + dsrc->pos, SEEK_SET)) 
              != (off_t) dsrc->offset) {
                return SEARCH_EINVAL;
            }
        }

        dsrc->bufpos = dsrc->pos - leftover;

        if (leftover) {
            /* preserve leftover bytes */
            memmove(dsrc->buf, dsrc->buf + dsrc->bufsize - leftover, leftover);
        }
        dsrc->bufsize = leftover;

        if (!cap) {
            if (leftover) {
                *retbuf = dsrc->buf;
                *retlen = dsrc->bufsize;
                return SEARCH_OK;
            } else {
                return SEARCH_FINISH;
            }
        }

        /* limit amount we try to read to available buffer size */
        if ((unsigned int) cap > dsrc->bufcap - dsrc->bufsize) {
            cap = dsrc->bufcap - dsrc->bufsize;
        }

        while ((bytes = read(dsrc->fd, dsrc->buf + dsrc->bufsize, cap)) == -1) {
            switch (errno) {
            case EINTR: /* do nothing, try again */ break;
            case EIO: return SEARCH_EIO;
            default: return SEARCH_EINVAL;
            }
        }
        dsrc->bufsize += bytes;
        dsrc->pos += bytes;

        *retbuf = dsrc->buf;
        *retlen = dsrc->bufsize;

        if (dsrc->bufsize) {
            return SEARCH_OK;
        } else {
            return SEARCH_FINISH;
        }
    }
}

static void disksrc_delete(struct search_list_src *src) {
    struct disksrc *dsrc = src->opaque;

    dsrc->alloc.free(dsrc->alloc.opaque, dsrc->buf);
    fdset_unpin(dsrc->idx->fd, dsrc->type, dsrc->fileno, dsrc->fd);
    free(src);
    return;
}

static struct search_list_src *disksrc_new(struct index *idx, 
  unsigned int type, unsigned int fileno, unsigned long int offset, 
  unsigned int size, struct alloc *alloc, unsigned int mem) {
    int fd = fdset_pin(idx->fd, type, fileno, offset, SEEK_SET);

    if (mem > size) {
        mem = size;
    }

    if (fd >= 0) {
        struct disksrc *dsrc = malloc(sizeof(*dsrc));

        if (dsrc && (dsrc->buf = alloc->malloc(alloc->opaque, mem))) {
            dsrc->src.opaque = dsrc;
            dsrc->src.delet = disksrc_delete;
            dsrc->src.reset = disksrc_reset;
            dsrc->src.readlist = disksrc_read;

            /* ensure that nothing gets served out of the buffer first time */
            dsrc->bufpos = -1;  
            dsrc->bufsize = 0;

            dsrc->alloc = *alloc;
            dsrc->bufcap = mem;
            dsrc->pos = 0;
            dsrc->fd = fd;
            dsrc->idx = idx;
            dsrc->type = type;
            dsrc->fileno = fileno;
            dsrc->offset = offset;
            dsrc->size = size;
            return &dsrc->src;
        } else {
            if (dsrc) {
                free(dsrc);
            }
            fdset_unpin(idx->fd, type, fileno, fd);
        }
    }

    return NULL;
}

/* structure to break up the reads from an underlying source into smaller 
 * chunks so that we can test behaviour for discontiguous reads.  This should
 * never be used in production code. */
struct debufsrc {
    struct search_list_src src;      /* parent source structure */
    struct search_list_src *srcsrc;  /* underlying source */
    char *pos;                       /* position in current read */
    unsigned int len;                /* remaining buffer */
    unsigned int debuflen;           /* maximuum return length */
};

static enum search_ret debufsrc_reset(struct search_list_src *src) {
    struct debufsrc *dsrc = src->opaque;
    dsrc->len = 0;
    return dsrc->srcsrc->reset(dsrc->srcsrc);
}

static enum search_ret debufsrc_read(struct search_list_src *src, 
  unsigned int leftover, void **retbuf, unsigned int *retlen) {
    struct debufsrc *dsrc = src->opaque;
    enum search_ret sret;

    assert(dsrc->debuflen);
    if (dsrc->len >= dsrc->debuflen) {
        *retbuf = dsrc->pos - leftover;
        *retlen = dsrc->debuflen + leftover;
        dsrc->pos += dsrc->debuflen;
        dsrc->len -= dsrc->debuflen;
        return SEARCH_OK;
    }

    assert(dsrc->len < dsrc->debuflen);
    /* read more from underlying source.  XXX: note that we're potentially
     * destroying current buffer on failure here */
    if ((sret = dsrc->srcsrc->readlist(dsrc->srcsrc, dsrc->len, 
        (void **) &dsrc->pos, &dsrc->len)) == SEARCH_OK) {

        *retbuf = dsrc->pos;
        if (dsrc->len > dsrc->debuflen) {
            *retlen = dsrc->debuflen;
        } else {
            *retlen = dsrc->len;
        }
        assert(*retlen);
        dsrc->pos += *retlen;
        dsrc->len -= *retlen;
        return SEARCH_OK;
    } else {
        return sret;
    }
}

static void debufsrc_delete(struct search_list_src *src) {
    struct debufsrc *dsrc = src->opaque;

    dsrc->srcsrc->delet(dsrc->srcsrc);
    free(src);
}

struct search_list_src *debufsrc_new(struct search_list_src *src, 
  unsigned int debuflen) {
    struct debufsrc *dsrc = malloc(sizeof(*dsrc));

    if (dsrc) {
        dsrc->src.readlist = debufsrc_read;
        dsrc->src.reset = debufsrc_reset;
        dsrc->src.delet = debufsrc_delete;
        dsrc->src.opaque = dsrc;

        dsrc->srcsrc = src;
        dsrc->pos = NULL;
        dsrc->len = 0;
        dsrc->debuflen = debuflen + !debuflen;
        return &dsrc->src;
    } else {
        return NULL;
    }
}

struct search_list_src *search_term_src(struct index *idx, struct term *term,
  struct alloc *alloc, unsigned int mem) {
    if (term->vecmem) {
        /* memory source */
        assert(term->vocab.location == VOCAB_LOCATION_VOCAB);
        return memsrc_new(term->vecmem, term->vocab.size);
    } else {
        /* disk source */
        assert(term->vocab.location == VOCAB_LOCATION_FILE);
        return disksrc_new(idx, idx->index_type, 
            term->vocab.loc.file.fileno, term->vocab.loc.file.offset, 
            term->vocab.size, alloc, mem);
    }
}

/* internal function to return the correct source for a given term */
struct search_list_src *search_conjunct_src(struct index *idx, 
  struct conjunct *conj, struct alloc *alloc, unsigned int memlimit) {
    if (conj->vecmem) {
        /* memory source */
        return memsrc_new(conj->vecmem, conj->vecsize);
    } else {
        /* source from term */
        return search_term_src(idx, &conj->term, alloc, memlimit);
    }
}

unsigned int search_qterms(struct query *q) {
    unsigned int i,
                 sum = 0;

    for (i = 0; i < q->terms; i++) {
        sum += q->term[i].f_qt;
    }

    return sum;
}

float search_qweight(struct query *q) {
    double weight = 0.0,
           fqt_log;
    unsigned int i;

    for (i = 0; i < q->terms; i++) {
        fqt_log = log(q->term[i].f_qt) + 1;
        weight += fqt_log * fqt_log;
    }

    return (float) sqrtf(weight);
}

