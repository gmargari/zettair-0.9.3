/* index_querybuild implements a function to build a query structure
 * from a query string.  This turned out to be surprisingly difficult,
 * as it turns out to be an optimisation problem when trying to select
 * words from the query.
 *
 * written nml 2003-06-02 (moved from index_search.c 2003-09-23)
 *
 */

#include "firstinclude.h"

#include "index_querybuild.h"

#include "_index.h"

#include "def.h"
#include "iobtree.h" 
#include "queryparse.h" 
#include "str.h" 
#include "stem.h" 
#include "stop.h" 
#include "vec.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef MT_ZET
#include <pthread.h>

static pthread_mutex_t vocab_mutex = PTHREAD_MUTEX_INITIALIZER;

#endif /* MT_ZET */

/* maximum length of an in-vocab vector. */
#define MAX_VOCAB_VECTOR_LEN 4096

/**
 *  Non-synchronized version of get_vocab_vector, i.e. we
 *  assume that, in a multi-threaded environment, the caller
 *  has already performed any necessary synchronisation.
 */
static int get_vocab_vector_locked(struct iobtree * vocab, 
  struct vocab_vector * entry_out, const char * term, unsigned int term_len,
  char * vec_buf, int vec_buf_len, int impact) {
    void * ve_data = NULL;
    unsigned int veclen = 0;
    struct vec v;

    ve_data = iobtree_find(vocab, term, term_len, 0, &veclen);
    if (!ve_data)
        return 0;
    v.pos = ve_data;
    v.end = v.pos + veclen;
    if (!impact) {
        /* select first non-impact-ordered vector */
        do {
            if (vocab_decode(entry_out, &v) != VOCAB_OK) {
                return -1; 
            }
        } while (entry_out->type == VOCAB_VTYPE_IMPACT);
    } else {
        /* select first impact-ordered vector */
        do {
            if (vocab_decode(entry_out, &v) != VOCAB_OK) {
                return -1; 
            }
        } while (entry_out->type != VOCAB_VTYPE_IMPACT);
    }
    if (entry_out->location == VOCAB_LOCATION_VOCAB) {
        assert(entry_out->size <= (unsigned int) vec_buf_len);
        memcpy(vec_buf, entry_out->loc.vocab.vec, entry_out->size);
        entry_out->loc.vocab.vec = vec_buf;
    }
    return 1;
}

/**
 *  Extract a vocab entry from a btree.
 *
 *  This function is synchronised on the vocab_mutex mutex.
 *  At the end of this call, the vocab entry is completely
 *  independent form the btree.
 *
 *  @param vec_buf buffer to store vector if the vocab entry
 *  has an in-vocab vector.  entry_out->loc.vocab.vec will be
 *  made to point to this.  You need to copy this before
 *  reusing the vector if you want to retain it.
 *
 *  @return 0 if the term does not exist in the vocab 
 *  (entry_out will be unchanged); 1 if the term exists in
 *  the vocab (entry_out will hold the vocab entry for the
 *  term); < 0 on error.
 */
static int get_vocab_vector(struct iobtree * vocab, 
  struct vocab_vector * entry_out, const char * term, unsigned int term_len,
  char * vec_buf, int vec_buf_len, int impact) {
    int retval = 0;
#ifdef MT_ZET
    pthread_mutex_lock(&vocab_mutex);
#endif /* MT_ZET */
    retval = get_vocab_vector_locked(vocab, entry_out, term, term_len,
      vec_buf, vec_buf_len, impact);
#ifdef MT_ZET
    pthread_mutex_unlock(&vocab_mutex);
#endif /* MT_ZET */
    return retval;
}

/* internal function to append a new word to a conjunct */
static int conjunct_append(struct query *query, 
  struct conjunct *conj, struct vocab_vector * sve,
  const char *term, unsigned int termlen, unsigned int *maxterms) {
    struct term *currterm;

    /* OPTIMISE: search existing AND conjunct for word */

    /* we're building a new phrase */
    if (query->terms < *maxterms) {
        /* iterate to the end of the phrase */
        for (currterm = &conj->term; currterm->next;
          currterm = currterm->next) ;

        /* add next word on */
        (*maxterms)--;
        currterm = currterm->next = &query->term[*maxterms].term;
        if (!(currterm->term = str_ndup(term, termlen))) {
            return 0;
        }
        memcpy(&currterm->vocab, sve, sizeof(*sve));

        /* allocate memory for vector part of vector */
        if (currterm->vocab.location == VOCAB_LOCATION_VOCAB) {
            if ((currterm->vecmem = malloc(currterm->vocab.size))) {
                memcpy(currterm->vecmem, 
                  currterm->vocab.loc.vocab.vec, currterm->vocab.size);
            } else {
                free(currterm->term);
                return 0;
            }
        } else {
            currterm->vecmem = NULL;
        }
        currterm->next = NULL;
        conj->terms++;
    }
    /* else we need to steal slots (OPTIMIZE) */

    return 1;
}

static struct conjunct *conjunct_find(struct query *query,
  struct vocab_vector *sve, const char *term, unsigned int termlen,
  int type) {
    unsigned int i;
    for (i = 0; i < query->terms; i++) {
        if ((query->term[i].type == type) 
          && (sve->size == query->term[i].term.vocab.size)
          && (sve->location == query->term[i].term.vocab.location)
          && (((sve->location == VOCAB_LOCATION_VOCAB) 
              && (query->term[i].term.term) 
              && !str_ncmp(term, query->term[i].term.term, termlen))
            || ((sve->location == VOCAB_LOCATION_FILE) 
              && (sve->loc.file.fileno 
                == query->term[i].term.vocab.loc.file.fileno)
              && (sve->loc.file.offset 
                == query->term[i].term.vocab.loc.file.offset)))) {

            return &query->term[i];
        }
    }
    return NULL;
}

/* internal function to copy a word into a new conjunct */
static struct conjunct *conjunct_add(struct query *query, 
  struct vocab_vector * sve, const char *term, 
  unsigned int termlen, int type, unsigned int *maxterms) {
    struct conjunct *ret = NULL;

    ret = conjunct_find(query, sve, term, termlen, type);

    if (ret != NULL) {
        ret->f_qt++;
    } else {
        /* couldn't find a match, might have to insert the word */
        if (query->terms < *maxterms) {
            ret = &query->term[query->terms];
            ret->type = type;
            ret->f_qt = 1;
            ret->f_t = sve->header.docwp.docs;
            ret->F_t = sve->header.docwp.occurs;
            ret->term.next = NULL;
            ret->vecmem = NULL;
            ret->terms = 1;
            ret->sloppiness = 0;
            ret->cutoff = 0;
            query->terms++;
            if (!(ret->term.term = str_ndup(term, termlen))) {
                query->terms--;
                ret = NULL;
            }
            memcpy(&ret->term.vocab, sve, sizeof(*sve));
            /* allocate memory for vector part of vector */
            if (ret->term.vocab.location == VOCAB_LOCATION_VOCAB) {
                if ((ret->term.vecmem = malloc(sve->size))) {
                    memcpy(ret->term.vecmem, sve->loc.vocab.vec, sve->size);
                } else {
                    free(ret->term.term);
                    return 0;
                }
            } else {
                ret->term.vecmem = NULL;
            }
        }
        /* else we might have to steal a slot (OPTIMIZE) */
    }

    return ret;
}

/* internal function to copy a conjunction and add an new word onto the end of 
 * it (convenience function) */
static struct conjunct *conjunct_copy(struct query *query, 
  struct conjunct *conj, unsigned int matches, struct vocab_vector * sve,
  const char *term, unsigned int termlen, unsigned int *maxterms) {
    struct conjunct *ret = NULL,
                    *next;
    struct term *currterm,
                *nextterm;

    if (!matches) {
        return NULL;
    }

    /* copy head of non-matching phrase */
    if (query->terms < *maxterms) {
        ret = next = &query->term[query->terms++];
        memcpy(&next->term.vocab, &conj->term.vocab, sizeof(conj->term.vocab));
        if (!(next->term.term = str_dup(conj->term.term))) {
            /* FIXME: need to cleanup properly here */
            return NULL;
        }

        /* allocate memory for vector part of vector */
        if (next->term.vocab.location == VOCAB_LOCATION_VOCAB) {
            if ((next->term.vecmem = malloc(conj->term.vocab.size))) {
                memcpy(next->term.vecmem, conj->term.vecmem, 
                  conj->term.vocab.size);
            } else {
                free(next->term.term);
                return NULL;
            }
        }

        next->term.next = NULL;
        next->terms = 1;
        next->f_qt = 1;
        next->type = conj->type;
        matches--;

        nextterm = &next->term;
        currterm = conj->term.next;
        while ((query->terms < *maxterms) && matches) {
            (*maxterms)--;
            matches--;
            nextterm->next = &query->term[*maxterms].term;
            nextterm = nextterm->next;
            memcpy(&nextterm->vocab, &currterm->vocab, sizeof(currterm->vocab));
            if (!(nextterm->term = str_dup(currterm->term))) {
                /* FIXME: need to cleanup properly here */
                return NULL;
            }

            /* allocate memory for vector part of vector */
            if (nextterm->vocab.location == VOCAB_LOCATION_VOCAB) {
                if ((nextterm->vecmem = malloc(currterm->vocab.size))) {
                    memcpy(nextterm->vecmem, currterm->vecmem, 
                      currterm->vocab.size);
                } else {
                    free(nextterm->term);
                    return NULL;
                }
            }
            next->terms++;
        }
        nextterm->next = NULL;

        /* append new term to phrase */
        if ((query->terms < *maxterms) && sve) {
            /* add next word on */
            (*maxterms)--;
            currterm = nextterm->next = &query->term[*maxterms].term;
            if (!(currterm->term = str_ndup(term, termlen))) {
                /* FIXME: need to cleanup properly here */
                return NULL;
            }
            memcpy(&currterm->vocab, sve, sizeof(*sve));
            /* allocate memory for vector part of vector */
            if (currterm->vocab.location == VOCAB_LOCATION_VOCAB) {
                if ((currterm->vecmem = malloc(currterm->vocab.size))) {
                    memcpy(currterm->vecmem, currterm->vocab.loc.vocab.vec,
                      currterm->vocab.size);
                } else {
                    free(nextterm->term);
                    return NULL;
                }
            }
            currterm->next = NULL;
            next->terms++;
        }
    }
    /* else we need to steal slots (OPTIMIZE) */

    return ret;
}

/* internal function to construct a query structure from a given string (query)
 * of length len.  At most maxterms will be read from the query. */ 
unsigned int index_querybuild(struct index *idx, struct query *query, 
  const char *querystr, unsigned int len, unsigned int maxterms, 
  unsigned int maxtermlen, int impacts) {
    struct queryparse *qp;           /* structure to parse the query */
    struct conjunct *current = NULL; /* pointer to a current conjunction */
    char word[TERMLEN_MAX + 1];      /* buffer to hold words */
    unsigned int i,                  /* counter */
                 wordlen,            /* length of word */
                 words = 0,          /* number of words parsed */
                 currmatch = 0;      /* number of matches against current 
                                      * entry */
                 /* veclen; */
    int state = CONJUNCT_TYPE_WORD,  /* state variable, can take on values 
                                      * from conjunct types enum */
        stopped = 0;                 /* whether the last word was stopped */
    /* void *ve;  */                 /* compressed vocabulary entry for a 
                                      * word */
    enum queryparse_ret parse_ret;   /* value returned by parser */
    /* last modifier seen; also, are we in a modifier */
    enum { MODIFIER_NONE, MODIFIER_SLOPPY, MODIFIER_CUTOFF } modifier 
      = MODIFIER_NONE;
    void (*stem)(void *, char *) = index_stemmer(idx);

    assert(maxtermlen <= TERMLEN_MAX);
    if (!(qp 
      = queryparse_new(maxtermlen, querystr, len))) {
        return 0;
    }

    query->terms = 0;

    /* This bit of code builds a structure that represents a query from an
     * array, where the array of words will be filled from 0 upwards, and
     * conjunctions that require a linked list of words (phrase, AND) take
     * additional words from the end of the array. */

    do {
        struct vocab_vector entry;
        int retval;
        char vec_buf[MAX_VOCAB_VECTOR_LEN];
        parse_ret = queryparse_parse(qp, word, &wordlen);
        switch (parse_ret) {
        case QUERYPARSE_WORD_EXCLUDE:
            /* this functionality not included yet, just ignore the word for 
             * now */

            /* look up word in vocab */
            /* ve = hFetch(word, idx->vocab, NULL); */

            /* OPTIMIZE: search existing conjunctions for word and remove 
             * them */

            /* FIXME: stop word */
            /* if (ve) {
                conjunct_add(query, ve, CONJUNCT_TYPE_EXCLUDE, 
                  &maxterms);
            } */

            current = NULL;   /* this can't be the start of a conjunction */
            words++;
            break;

        case QUERYPARSE_WORD_NOSTOP:
        case QUERYPARSE_WORD:
            if (modifier != MODIFIER_NONE) {
                /* this is not a query term, but an argument to a modifier */
                switch (modifier) {
                case MODIFIER_SLOPPY:
                    if (query->terms > 0)
                        query->term[query->terms - 1].sloppiness = atoi(word);
                    break;

                case MODIFIER_CUTOFF:
                    if (query->terms > 0)
                        query->term[query->terms - 1].cutoff = atoi(word);
                    break;

                default:
                    /* FIXME WARN */
                    break;
                }
                break;
            }
            /* look up word in vocab */
            /* FIXME: word needs to be looked up in in-memory postings as 
             * well */
            if (stem) {
                word[wordlen] = '\0';
                stem(idx->stem, word);
                wordlen = str_len(word);
            }
            retval = get_vocab_vector(idx->vocab, &entry, word, wordlen,
              vec_buf, sizeof(vec_buf), impacts);
            if (retval < 0) {
                return 0;
            } else if (retval == 0) {
                stopped = 0;   /* so we know that this term wasn't stopped */
                currmatch = 1; /* so that we know that phrases have started */
                if (current && (state == CONJUNCT_TYPE_AND)) {
                    /* need to remove current conjunction, as it contains a word
                     * that isn't in the collection */
                    if (current->f_qt > 1) {
                        current->f_qt--;
                    } else {
                        state = CONJUNCT_TYPE_WORD;   /* stop AND condition */
                        maxterms += current->terms - 1;
                        query->terms--;
                    }
                } else if (current && (state == CONJUNCT_TYPE_PHRASE)) {
                    /* same, except phrase continues until end-phrase */
                    if (current->f_qt > 1) {
                        current->f_qt--;
                    } else {
                        maxterms += current->terms - 1;
                        query->terms--;
                    }
                }
                current = NULL;
            } else if (state == CONJUNCT_TYPE_PHRASE) {
                /* OPTIMIZE: check word against excluded terms */
                struct term *currterm;

                /* processing a phrase */
                if (!currmatch) {
                    /* first word in phrase, match or add a conjunction */
                    current = conjunct_add(query, &entry,
                      /* ve, veclen,  */
                      word, wordlen, 
                      CONJUNCT_TYPE_PHRASE, &maxterms);
                    currmatch = 1;
                } else if (current && (current->f_qt > 1)) {
                    /* we're matching an existing phrase */
 
                    /* iterate to next term we need to match */
                    for (i = 0, currterm = &current->term; i < currmatch; 
                      i++, currterm = currterm->next) ;

                    if (currterm && !str_cmp(currterm->term, word)) {
                        /* matched */
                        currmatch++;
                    } else {
                        /* didn't match, copy non-matching phrase */
                        current->f_qt--;
                        current = conjunct_copy(query, current, currmatch, 
                          &entry, word, wordlen, &maxterms);
                        currmatch++;
                    }
                } else if (current) {
                    /* we're building a new phrase, add next word on */
                    conjunct_append(query, current, 
                      &entry,
                     /* ve, veclen, */
                      word, wordlen, 
                      &maxterms);
                    currmatch++;
                }
                /* otherwise we're ignoring this phrase (because it contains a
                 * word thats not in the vocab) */

            } else if (state == CONJUNCT_TYPE_AND) {
                /* we are constructing an AND conjunction */

                /* FIXME: stop word 
                stopped = 1;
                current = NULL; */

                /* OPTIMIZE: check word against excluded terms */

                if (current) {
                    if ((current->type == CONJUNCT_TYPE_AND) 
                      || (current->f_qt == 1)) {
                        /* add to current conjunct */
                        conjunct_append(query, current, &entry,
                          word, wordlen, &maxterms);
                        current->type = CONJUNCT_TYPE_AND;
                    } else {
                        /* copy matched word to new location for AND conjunct */
                        current->f_qt--;
                        current = conjunct_copy(query, current, 1, &entry,
                          word, wordlen, &maxterms);
                        current->type = CONJUNCT_TYPE_AND;
                    }
                } else if (stopped) { 
                    /* first word(s) in conjunct was stopped, so start a new
                     * one */
                    current = conjunct_add(query, &entry, word, wordlen,
                      CONJUNCT_TYPE_WORD, &maxterms);
                }

                state = CONJUNCT_TYPE_WORD;   /* stop AND condition */
            } else if (state == CONJUNCT_TYPE_WORD) {
                /* its a single word */
                stopped = 0;
                if (parse_ret != QUERYPARSE_WORD_NOSTOP) {
                    word[wordlen] = '\0';
                    if (idx->qstop 
                      && stop_stop(idx->qstop, word) == STOP_STOPPED) {
                        /* it is a stopword */
                        stopped = 1;
                        current = NULL;
                    }
                }

                if (!stopped) {
                    current = conjunct_add(query, &entry,
                      /* ve, veclen, */
                      word, wordlen,
                      CONJUNCT_TYPE_WORD, &maxterms);
                    currmatch = 1;
                }
            }

            words++;
            break;

        case QUERYPARSE_OR:
            state = CONJUNCT_TYPE_WORD;  /* or is the default mode anyway */
            break;

        case QUERYPARSE_AND:
            state = CONJUNCT_TYPE_AND;
            break;

        case QUERYPARSE_START_PHRASE: 
            /* phrase starts */
            state = CONJUNCT_TYPE_PHRASE;
            current = NULL;
            currmatch = 0;
            break;

        case QUERYPARSE_END_PHRASE:
            if (current && (current->terms != currmatch)) {
                /* partial match, need to copy phrase */
                current->f_qt--;
                current = conjunct_copy(query, current, currmatch, NULL,
                  NULL, 0, &maxterms);
            }

            /* treat single-word phrases as, well, words */
            if (current && (current->terms == 1)) {
                struct conjunct *ret;
                /* see if this single-word occurred previously */
                ret = conjunct_find(query, &current->term.vocab,
                  current->term.term, str_len(current->term.term), 
                  CONJUNCT_TYPE_WORD);
                if (ret == NULL) {
                    /* ok, this is the first occurence */
                    current->type = CONJUNCT_TYPE_WORD;
                } else {
                    /* there was a previous occurence: increment its f_qt,
                       and free this one */
                    ret->f_qt++;
                    assert(current == &query->term[query->terms - 1]);
                    free(current->term.term);
                    if (current->term.vocab.location == VOCAB_LOCATION_VOCAB) {
                        free(current->term.vecmem);
                    }
                    query->terms--;
                }
            }
            current = NULL;
            state = CONJUNCT_TYPE_WORD;
            break;

        case QUERYPARSE_END_SENTENCE: 
            /* we're ignoring this at the moment - it might be used later if
             * we don't want to match phrases across sentence boundaries */
            break;

        case QUERYPARSE_END_MODIFIER: 
            modifier = MODIFIER_NONE;
            break;
        case QUERYPARSE_START_MODIFIER: 
            if (str_casecmp(word, "sloppy") == 0) 
                modifier = MODIFIER_SLOPPY;
            else if (str_casecmp(word, "cutoff") == 0) 
                modifier = MODIFIER_CUTOFF;
            else
                /* FIXME WARN */
                modifier = MODIFIER_NONE;
            break;

        case QUERYPARSE_EOF: break; /* this will finish the parse */

        default:
            /* unexpected return code, error */
            queryparse_delete(qp);
            return 0;
        }
    } while ((parse_ret != QUERYPARSE_EOF)
      && (query->terms < maxterms));  /* FIXME: temporary stopping condition */

    queryparse_delete(qp);
    /* returning word count confuses errors with empty queries. */
    /* return words; */
    return 1;
}

