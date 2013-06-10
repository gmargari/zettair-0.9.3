/* trec_eval.c implements standard information retrieval evaluation
 * measures.  This code originated in the trec_eval program written by
 * Chris Buckley and Gerard Salton.  They originally published under
 * this license:
 *
 * Copyright (c) 1991, 1990, 1984 - Gerard Salton, Chris Buckley.
 * Permission is granted for use of this file in unmodified form for
 * research purposes. Please contact the SMART project to obtain
 * permission for other uses. 
 *
 * This code was modified and redistributed with permission from Chris
 * Buckley.
 *
 */

#include "firstinclude.h"

#include "trec_eval.h"

#include "chash.h"
#include "str.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Defined constants that are collection/purpose dependent */

/* Number of cutoffs for recall,precision, and rel_precis measures. */

/* CUTOFF_VALUES gives the number of retrieved docs that these
 * evaluation measures are applied at. */
#define NUM_CUTOFF  9
#define CUTOFF_VALUES  {5, 10, 15, 20, 30, 100, 200, 500, 1000}

/* Maximum fallout value, expressed in number of non-rel docs retrieved. */

/* (Make the approximation that number of non-rel docs in collection */

/* is equal to the number of number of docs in collection) */
#define MAX_FALL_RET  142

/* Maximum multiple of R (number of rel docs for this query) to calculate */

/* R-based precision at */
#define MAX_RPREC 2.0

/* Defined constants that are collection/purpose independent.  If you
 * change these, you probably need to change comments and documentation,
 * and some variable names may not be appropriate any more! */
#define NUM_RP_PTS  11
#define THREE_PTS {2, 5, 8}
#define NUM_FR_PTS  11
#define NUM_PREC_PTS 11

typedef struct {
    int qid;                        /* query id (for overall average
                                       figures, this gives number of
                                       queries in run) */

    /* Summary Numbers over all queries */
    long num_rel;                   /* Number of relevant docs */
    long num_ret;                   /* Number of retrieved docs */
    long num_rel_ret;               /* Number of relevant retrieved docs */
    float avg_doc_prec;             /* Average of precision over all
                                       relevant documents (query
                                       independent) */

    /* Measures after num_ret docs */
    float exact_recall;             /* Recall after num_ret docs */
    float exact_precis;             /* Precision after num_ret docs */
    float exact_rel_precis;         /* Relative Precision (or recall) */
    /* Defined to be precision / max possible precision */

    /* Measures after each document */
    float recall_cut[NUM_CUTOFF];   /* Recall after cutoff[i] docs */

    float precis_cut[NUM_CUTOFF];   /* precision after cutoff[i] docs.
                                       If less than cutoff[i] docs
                                       retrieved, then assume an
                                       additional cutoff[i]-num_ret
                                       non-relevant docs are retrieved. */
    float rel_precis_cut            /* Relative precision after cutoff[i] */
      [NUM_CUTOFF];                 /* docs */

    /* Measures after each rel doc */
    float av_recall_precis;         /* average(integral) of precision at
                                       all rel doc ranks */
    float int_av_recall_precis;     /* Same as above, but the precision
                                       values have been interpolated, so
                                       that prec(X) is actually MAX
                                       prec(Y) for all Y >= X */
    float int_recall_precis         /* interpolated precision at 0.1 */
      [NUM_RP_PTS];                 /* increments of recall */
    float int_av3_recall_precis;    /* interpolated average at 3 intermediate
                                       points */
    float int_av11_recall_precis;   /* interpolated average at NUM_RP_PTS
                                       intermediate points (recall_level) */

    /* Measures after each non-rel doc */
    float fall_recall[NUM_FR_PTS];  /* max recall after each non-rel
                                       doc, at 11 points starting at 0.0
                                       and ending at MAX_FALL_RET
                                       /num_docs */
    float av_fall_recall;           /* Average of fallout-recall, after
                                       each non-rel doc until fallout of
                                       MAX_FALL_RET / num_docs achieved */

    /* Measures after R-related cutoffs.  R is the number of relevant
     * docs for a particular query, but note that these cutoffs are
     * after R docs, whether relevant or non-relevant, have been
     * retrieved.  R-related cutoffs are really only applicable to a
     * situtation where there are many relevant docs per query (or lots
     * of queries). */
    float R_recall_precis;          /* Recall or precision after R docs
                                       (note they are equal at this
                                       point) */
    float av_R_precis;              /* Average (or integral) of
                                       precision at each doc until R
                                       docs have been retrieved */
    float R_prec_cut[NUM_PREC_PTS]; /* Precision measured after
                                       multiples of R docs have been
                                       retrieved.  11 equal points, with
                                       max multiple having value
                                       MAX_RPREC */
    float int_R_recall_precis;      /* Interpolated precision after R
                                       docs Prec(X) = MAX(prec(Y)) for
                                       all Y>=X */
    float int_av_R_precis;          /* Interpolated */
    float int_R_prec_cut            /* Interpolated */
      [NUM_PREC_PTS]; 
} TREC_EVAL;

typedef struct {
    long int did;                   /* document id */
    long int rank;                  /* Rank of this document */
    char action;                    /* what action a user has taken with
                                       doc */
    char rel;                       /* whether doc judged relevant(1) or
                                       not(0) */
    char iter;                      /* Number of feedback runs for this
                                       query */
    char trtup_unused;              /* Presently unused field */
    float sim;                      /* similarity of did to qid */
} TR_TUP;

typedef struct {
    int qid;                        /* query id */
    long int num_tr;                /* Number of tuples for tr_vec */
    TR_TUP *tr;                     /* tuples.  Invariant: tr sorted
                                       increasing did */
} TR_VEC;

static int init_tr_trec_eval(),
    tr_trec_eval(),
    close_tr_trec_eval();
static int trvec_trec_eval();
static int comp_tr_tup_did(const void *, const void *),
    comp_sim_docno(const void *, const void *);
static int get_trec_top();
static int form_and_eval_trvec();

typedef struct {
    char *docno;
    float sim;
    long int rank;
} TEXT_TR;

typedef struct {
    int qid;
    long int num_text_tr;
    long int max_num_text_tr;
    TEXT_TR *text_tr;
} TREC_TOP;

#define TR_INCR 250

#define MAX(A,B)  ((A) > (B) ? (A) : (B))
#define MIN(A,B)  ((A) > (B) ? (B) : (A))

#ifndef FALSE
#define FALSE   0
#endif
#ifndef TRUE
#define TRUE    1
#endif

#define UNDEF   -1

#define MAX_LINE_LENGTH   100

/* currently I'm assuming there are only 1000 queries _ever_ used at
 * TREC, which is probably unrealistic. This assumption makes things
 * easier, and if there are more queries used (ie higher query numbers
 * used) then the following number just needs to be increased. */

#define INTERPOLATED000     0
#define INTERPOLATED010     1
#define INTERPOLATED020     2
#define INTERPOLATED030     3
#define INTERPOLATED040     4
#define INTERPOLATED050     5
#define INTERPOLATED060     6
#define INTERPOLATED070     7
#define INTERPOLATED080     8
#define INTERPOLATED090     9
#define INTERPOLATED100    10
#define PRECISION_AT_5      0
#define PRECISION_AT_10     1
#define PRECISION_AT_15     2
#define PRECISION_AT_20     3
#define PRECISION_AT_30     4
#define PRECISION_AT_100    5
#define PRECISION_AT_200    6
#define PRECISION_AT_500    7
#define PRECISION_AT_1000   8

#define TREC_DOCUMENT_NUMBER_MAX_LENGTH 50

typedef struct {
    int query_id;                   /* Holds query ID */
    char trec_document_number[TREC_DOCUMENT_NUMBER_MAX_LENGTH];
                                    /* TREC document ID */
    float score;                    /* accumulator score of document */
} result_tuple;

struct treceval_qrels {
    struct chash *judgements;       /* Hash table that contains as keys
                                       all relevance judgements in the
                                       format of "QID TRECDOCNO", for
                                       instance "381 FBIS3-1" */
    int number_of_judged_queries;   /* This many queries have relevance
                                       judgements */
    int lowest_query_id;            /* The lowest query Id that has a
                                       relevance judgement */

    /* Each query contains this many judgements */
    int *number_of_judgements_for_query; 
};

struct treceval {
    result_tuple *tuples;           /* Stores unevaluated result tuples */
    unsigned int cached_results;    /* Number of tuples in cache */
    unsigned int cache_size;        /* Cache size in tuples */
};

static void evaluate_trec_results(unsigned int from_position,
  unsigned int to_position, const struct treceval *result_cache,
  const struct treceval_qrels *qrels,
  struct treceval_results *evaluated_results) {
    TREC_TOP trec_top;
    TREC_EVAL eval_accum;
    unsigned int up_to = from_position;

    init_tr_trec_eval(&eval_accum);

    trec_top.max_num_text_tr = 0;
    trec_top.text_tr = NULL;

    evaluated_results->queries = 0;
    while (get_trec_top(&trec_top, result_cache, &up_to, to_position) != 0) {
        form_and_eval_trvec(&eval_accum, &trec_top, qrels);
        evaluated_results->queries++;
    }

    /* Finish evaluation computation */
    close_tr_trec_eval(&eval_accum);

    evaluated_results->retrieved = eval_accum.num_ret;
    evaluated_results->relevant = eval_accum.num_rel;
    evaluated_results->relevant_retrieved = eval_accum.num_rel_ret;
    evaluated_results->interpolated_rp[INTERPOLATED000] =
      eval_accum.int_recall_precis[0];
    evaluated_results->interpolated_rp[INTERPOLATED010] =
      eval_accum.int_recall_precis[1];
    evaluated_results->interpolated_rp[INTERPOLATED020] =
      eval_accum.int_recall_precis[2];
    evaluated_results->interpolated_rp[INTERPOLATED030] =
      eval_accum.int_recall_precis[3];
    evaluated_results->interpolated_rp[INTERPOLATED040] =
      eval_accum.int_recall_precis[4];
    evaluated_results->interpolated_rp[INTERPOLATED050] =
      eval_accum.int_recall_precis[5];
    evaluated_results->interpolated_rp[INTERPOLATED060] =
      eval_accum.int_recall_precis[6];
    evaluated_results->interpolated_rp[INTERPOLATED070] =
      eval_accum.int_recall_precis[7];
    evaluated_results->interpolated_rp[INTERPOLATED080] =
      eval_accum.int_recall_precis[8];
    evaluated_results->interpolated_rp[INTERPOLATED090] =
      eval_accum.int_recall_precis[9];
    evaluated_results->interpolated_rp[INTERPOLATED100] =
      eval_accum.int_recall_precis[10];
    evaluated_results->average_precision = eval_accum.av_recall_precis;
    evaluated_results->precision_at[PRECISION_AT_5] 
      = eval_accum.precis_cut[0];
    evaluated_results->precision_at[PRECISION_AT_10] 
      = eval_accum.precis_cut[1];
    evaluated_results->precision_at[PRECISION_AT_15] 
      = eval_accum.precis_cut[2];
    evaluated_results->precision_at[PRECISION_AT_20] 
      = eval_accum.precis_cut[3];
    evaluated_results->precision_at[PRECISION_AT_30] 
      = eval_accum.precis_cut[4];
    evaluated_results->precision_at[PRECISION_AT_100] 
      = eval_accum.precis_cut[5];
    evaluated_results->precision_at[PRECISION_AT_200]
      = eval_accum.precis_cut[6];
    evaluated_results->precision_at[PRECISION_AT_500] 
      = eval_accum.precis_cut[7];
    evaluated_results->precision_at[PRECISION_AT_1000] 
      = eval_accum.precis_cut[8];
    evaluated_results->rprecision = eval_accum.R_recall_precis;

    if (trec_top.text_tr != NULL) {
        free(trec_top.text_tr);
    }
    return;
}

static int form_and_eval_trvec(TREC_EVAL * eval_accum, TREC_TOP * trec_top,
  struct treceval_qrels *qrels) {
    TR_TUP *tr_tup;
    long i;
    TR_TUP *start_tr_tup = NULL; /* Space reserved for output TR_TUP
                                    tuples */
    long max_tr_tup = 0;
    TR_VEC tr_vec;
    char judgement[MAX_LINE_LENGTH];
    void **dummy_pointer = NULL;

    /* Reserve space for output tr_tups, if needed */
    if (trec_top->num_text_tr > max_tr_tup) {
        if (max_tr_tup > 0) {
            (void) free((char *) start_tr_tup);
        }
        max_tr_tup += trec_top->num_text_tr;
        if ((start_tr_tup = (TR_TUP *) malloc((unsigned) max_tr_tup
          * sizeof(TR_TUP))) == NULL) {
            return (UNDEF);
        }
    }

    /* Sort trec_top by sim, breaking ties lexicographically using docno */
    qsort((char *) trec_top->text_tr, (int) trec_top->num_text_tr,
      sizeof(TEXT_TR), comp_sim_docno);

    /* Add ranks to trec_top (starting at 1) */
    for (i = 0; i < trec_top->num_text_tr; i++) {
        trec_top->text_tr[i].rank = i + 1;
    }

    /* for each tuple in trec_top check the hash table which documents *
     * are relevant. Once relevance is known, convert trec_top tuple *
     * into TR_TUP. */
    tr_tup = start_tr_tup;
    for (i = 0; i < trec_top->num_text_tr; i++) {
        sprintf(judgement, "%d %s", trec_top->qid,
          trec_top->text_tr[i].docno);
        if (chash_ptr_ptr_find(qrels->judgements, judgement,
          &dummy_pointer) != CHASH_OK) {
            /* Document is non-relevant */
            tr_tup->rel = 0;
        } else {
            /* Document is relevant */
            tr_tup->rel = 1;
        }
        tr_tup->did = i;
        tr_tup->rank = trec_top->text_tr[i].rank;
        tr_tup->sim = trec_top->text_tr[i].sim;
        tr_tup->action = 0;
        tr_tup->iter = 0;
        tr_tup++;
    }
    /* Form and output the full TR_VEC object for this qid */
    /* Sort trec_top tr_tups by did */
    qsort((char *) start_tr_tup, (int) trec_top->num_text_tr, sizeof(TR_TUP),
      comp_tr_tup_did);
    tr_vec.qid = trec_top->qid;
    tr_vec.num_tr = trec_top->num_text_tr;
    tr_vec.tr = start_tr_tup;

    /* Accumulate this tr_vec's eval figures in eval_accum */
    tr_trec_eval(&tr_vec, eval_accum,
      qrels->number_of_judgements_for_query[trec_top->qid -
        qrels->lowest_query_id]);

    free(start_tr_tup);

    return (1);
}

static int comp_sim_docno(const void *vptr1, const void *vptr2) {
    const TEXT_TR *ptr1 = vptr1,
                  *ptr2 = vptr2;

    if (ptr1->sim > ptr2->sim)
        return (-1);
    if (ptr1->sim < ptr2->sim)
        return (1);
    return (strcmp(ptr2->docno, ptr1->docno));
}

static int comp_tr_tup_did(const void *vptr1, const void *vptr2) {
    const TR_TUP *ptr1 = vptr1,
                 *ptr2 = vptr2;

    return (ptr1->did - ptr2->did);
}

/* Get entire trec_top vector for next query */
static int get_trec_top(TREC_TOP * trec_top, struct treceval *result_cache,
  unsigned int *up_to, unsigned int to_position) {
    int qid;

    /* have already processed all tuples in this result set */
    if (*up_to >= to_position) {
        return 0;
    }
    trec_top->num_text_tr = 0;
    qid = trec_top->qid =
      result_cache->tuples[trec_top->num_text_tr + *up_to].query_id;

    while (qid == trec_top->qid) {
        /* Make sure enough space is reserved for next tuple */
        if (trec_top->num_text_tr >= trec_top->max_num_text_tr) {
            if (trec_top->max_num_text_tr == 0) {
                if ((trec_top->text_tr = (TEXT_TR *) malloc((unsigned)
                  TR_INCR * sizeof(TEXT_TR))) == NULL) {
                    return (0);
                }
            } else if ((trec_top->text_tr = (TEXT_TR *) realloc((char *)
                  trec_top->text_tr, (unsigned) (trec_top->max_num_text_tr
                  + TR_INCR) * sizeof(TEXT_TR))) == NULL) {
                return (0);
            }
            trec_top->max_num_text_tr += TR_INCR;
        }
        trec_top->text_tr[trec_top->num_text_tr].docno =
          result_cache->tuples[trec_top->num_text_tr +
          *up_to].trec_document_number;
        trec_top->text_tr[trec_top->num_text_tr].sim =
          result_cache->tuples[trec_top->num_text_tr + *up_to].score;
        trec_top->num_text_tr++;

        /* are we at the end of the results? */
        if ((trec_top->num_text_tr + *up_to) >= to_position) {
            *up_to += trec_top->num_text_tr;
            return (1);
        }

        qid = result_cache->tuples[trec_top->num_text_tr + *up_to].query_id;
    }

    *up_to += trec_top->num_text_tr;
    return (1);
}

/********************   PROCEDURE DESCRIPTION   ************************
 *0 Given a tr file, evaluate it using trec, returning the evaluation in eval
 *2 tr_trec_eval (tr_file, eval, inst)
 *3   char *tr_file;
 *3   TREC_EVAL *eval;
 *3   int inst;
 *4 init_tr_trec_eval (spec, unused)
 *5   "eval.tr_file"
 *5   "eval.tr_file.rmode"
 *5   "eval.trace"
 *4 close_tr_trec_eval (inst)
 *7 Evaluate the given tr_file, returning the average over all queries of
 *7 each query's evaluation.  Eval->qid will contain the number of queries
 *7 evaluated. Tr_file is taken from the argument tr_file if
 *7 that is valid, else from the spec parameter eval.tr_file.
 *7 Return 1 if successful, 0 if no queries were evaluated, and UNDEF
 *7 otherwise
 *8 Call trvec_smeval for each query in tr_file, and simply average results.
 *9 Note: Only the max iteration over all queries is averaged.  Thus, if
 *9 query 1 had one iteration of feedback, and query 2 had two iterations of
 *9 feedback, query 1 will not be included in the final average (or counted
 *9 in eval->qid).
***********************************************************************/

static int init_tr_trec_eval(TREC_EVAL * eval) {
    memset(eval, 0, sizeof(*eval));
    return (0);
}

static int tr_trec_eval(TR_VEC * tr_vec, TREC_EVAL * eval,
  long int num_rel) {
    long i;
    int max_iter_achieved;
    TREC_EVAL query_eval;
    int max_iter = 0;

    /* Check that max_iter has not been exceeded.  If it has, then have
     * to throw away all previous results. Also check to see that
     * max_iter has been achieved.  If not, then no docs were retrieved
     * for this query on this iteration */
    max_iter_achieved = 0;
    for (i = 0; i < tr_vec->num_tr; i++) {
        if (tr_vec->tr[i].iter > max_iter) {
            memset((char *) eval, 0, sizeof(TREC_EVAL));
            max_iter = tr_vec->tr[i].iter;
        }
        if (tr_vec->tr[i].iter == max_iter) {
            max_iter_achieved++;
        }
    }
    if (max_iter_achieved == 0) {
        return (0);
    }

    /* Evaluate this query */
    if (1 == trvec_trec_eval(tr_vec, &query_eval, num_rel)) {
        if (query_eval.num_ret > 0) {
            eval->qid++;
            eval->num_rel += query_eval.num_rel;
            eval->num_ret += query_eval.num_ret;
            eval->num_rel_ret += query_eval.num_rel_ret;
            eval->avg_doc_prec += query_eval.avg_doc_prec;
            eval->exact_recall += query_eval.exact_recall;
            eval->exact_precis += query_eval.exact_precis;
            eval->exact_rel_precis += query_eval.exact_rel_precis;
            eval->av_recall_precis += query_eval.av_recall_precis;
            eval->int_av_recall_precis += query_eval.int_av_recall_precis;
            eval->int_av3_recall_precis += query_eval.int_av3_recall_precis;
            eval->int_av11_recall_precis
              += query_eval.int_av11_recall_precis;
            eval->av_fall_recall += query_eval.av_fall_recall;
            eval->R_recall_precis += query_eval.R_recall_precis;
            eval->av_R_precis += query_eval.av_R_precis;
            eval->int_R_recall_precis += query_eval.int_R_recall_precis;
            eval->int_av_R_precis += query_eval.int_av_R_precis;
            for (i = 0; i < NUM_CUTOFF; i++) {
                eval->recall_cut[i] += query_eval.recall_cut[i];
                eval->precis_cut[i] += query_eval.precis_cut[i];
                eval->rel_precis_cut[i] += query_eval.rel_precis_cut[i];
            }
            for (i = 0; i < NUM_RP_PTS; i++)
                eval->int_recall_precis[i]
                  += query_eval.int_recall_precis[i];
            for (i = 0; i < NUM_FR_PTS; i++)
                eval->fall_recall[i] += query_eval.fall_recall[i];
            for (i = 0; i < NUM_PREC_PTS; i++) {
                eval->R_prec_cut[i] += query_eval.R_prec_cut[i];
                eval->int_R_prec_cut[i] += query_eval.int_R_prec_cut[i];
            }
        }
    }

    return (0);
}

static int close_tr_trec_eval(TREC_EVAL * eval) {
    long i;

    /* Calculate averages (for those eval fields returning averages) */
    if (eval->qid > 0) {
        if (eval->num_rel > 0) {
            eval->avg_doc_prec /= (float) eval->num_rel;
        }
        eval->exact_recall /= (float) eval->qid;
        eval->exact_precis /= (float) eval->qid;
        eval->exact_rel_precis /= (float) eval->qid;
        eval->av_recall_precis /= (float) eval->qid;
        eval->int_av_recall_precis /= (float) eval->qid;
        eval->int_av3_recall_precis /= (float) eval->qid;
        eval->int_av11_recall_precis /= (float) eval->qid;
        eval->av_fall_recall /= (float) eval->qid;
        eval->R_recall_precis /= (float) eval->qid;
        eval->av_R_precis /= (float) eval->qid;
        eval->int_R_recall_precis /= (float) eval->qid;
        eval->int_av_R_precis /= (float) eval->qid;
        for (i = 0; i < NUM_CUTOFF; i++) {
            eval->recall_cut[i] /= (float) eval->qid;
            eval->precis_cut[i] /= (float) eval->qid;
            eval->rel_precis_cut[i] /= (float) eval->qid;
        }
        for (i = 0; i < NUM_RP_PTS; i++) {
            eval->int_recall_precis[i] /= (float) eval->qid;
        }
        for (i = 0; i < NUM_FR_PTS; i++) {
            eval->fall_recall[i] /= (float) eval->qid;
        }
        for (i = 0; i < NUM_PREC_PTS; i++) {
            eval->R_prec_cut[i] /= (float) eval->qid;
            eval->int_R_prec_cut[i] /= (float) eval->qid;
        }
    }

    return (0);
}

/* cutoff values for recall precision output */
const static int cutoff[NUM_CUTOFF] = CUTOFF_VALUES;
const static int three_pts[3] = THREE_PTS;

static int compare_iter_rank(const void *, const void *);

static int trvec_trec_eval(TR_VEC * tr_vec, TREC_EVAL * eval,
  long int num_rel) {
    /* cutoff values for recall precision output */
    double recall,
      precis,
      int_precis;                   /* current recall, precision and
                                       interpolated precision values */
    long i, j;
    long rel_so_far;
    long max_iter;

    long cut_rp[NUM_RP_PTS];        /* number of rel docs needed to be
                                       retrieved for each recall-prec
                                       cutoff */
    long cut_fr[NUM_FR_PTS];        /* number of non-rel docs needed to
                                       be retrieved for each fall-recall
                                       cutoff */
    long cut_rprec[NUM_PREC_PTS];   /* Number of docs needed to be
                                       retrieved for each R-based prec
                                       cutoff */
    long current_cutoff,
        current_cut_rp,
        current_cut_fr,
        current_cut_rprec;

    if (tr_vec == (TR_VEC *) NULL)
        return (UNDEF);

    /* Initialize everything to 0 */
    memset((char *) eval, 0, sizeof(TREC_EVAL));
    eval->qid = tr_vec->qid;

    /* If no relevant docs, then just return */
    if (tr_vec->num_tr == 0) {
        return (0);
    }

    eval->num_rel = num_rel;

    /* Evaluate only the docs on the last iteration of tr_vec */
    /* Sort the tr tuples for this query by decreasing iter and
     * increasing rank */
    qsort((char *) tr_vec->tr, (int) tr_vec->num_tr, sizeof(TR_TUP),
      compare_iter_rank);

    max_iter = tr_vec->tr[0].iter;
    rel_so_far = 0;
    for (j = 0; j < tr_vec->num_tr; j++) {
        if (tr_vec->tr[j].iter == max_iter) {
            eval->num_ret++;
            if (tr_vec->tr[j].rel)
                rel_so_far++;
        } else {
            if (tr_vec->tr[j].rel)
                eval->num_rel--;
        }
    }
    eval->num_rel_ret = rel_so_far;

    /* Discover cutoff values for this query */
    current_cutoff = NUM_CUTOFF - 1;
    while (current_cutoff > 0 && cutoff[current_cutoff] > eval->num_ret) {
        current_cutoff--;
    }
    for (i = 0; i < NUM_RP_PTS; i++) {
        cut_rp[i] = ((eval->num_rel * i) + NUM_RP_PTS - 2)
          / (NUM_RP_PTS - 1);
    }
    current_cut_rp = NUM_RP_PTS - 1;
    while (current_cut_rp > 0 && cut_rp[current_cut_rp]
      > eval->num_rel_ret) {
        current_cut_rp--;
    }
    for (i = 0; i < NUM_FR_PTS; i++) {
        cut_fr[i] = ((MAX_FALL_RET * i) + NUM_FR_PTS - 2) / (NUM_FR_PTS - 1);
    }
    current_cut_fr = NUM_FR_PTS - 1;
    while (current_cut_fr > 0
      && cut_fr[current_cut_fr] > eval->num_ret - eval->num_rel_ret) {
        current_cut_fr--;
    }
    for (i = 0; i < NUM_PREC_PTS; i++) {
        cut_rprec[i] = (long int) (((MAX_RPREC * eval->num_rel * i)
          + NUM_PREC_PTS - 2) / (NUM_PREC_PTS - 1));
    }
    current_cut_rprec = NUM_PREC_PTS - 1;
    while (current_cut_rprec > 0
      && cut_rprec[current_cut_rprec] > eval->num_ret) {
        current_cut_rprec--;
    }

    /* Note for interpolated precision values (Prec(X) = MAX (PREC(Y))
     * for all Y >= X) */
    int_precis = (float) rel_so_far / (float) eval->num_ret;
    for (j = eval->num_ret; j > 0; j--) {
        recall = (float) rel_so_far / (float) eval->num_rel;
        precis = (float) rel_so_far / (float) j;
        if (int_precis < precis) {
            int_precis = precis;
        }
        while (j == cutoff[current_cutoff]) {
            eval->recall_cut[current_cutoff] = (float) recall;
            eval->precis_cut[current_cutoff] = (float) precis;
            eval->rel_precis_cut[current_cutoff]
              = (float) (j > eval->num_rel ? recall : precis);
            current_cutoff--;
        }
        while (j == cut_rprec[current_cut_rprec]) {
            eval->R_prec_cut[current_cut_rprec] = (float) precis;
            eval->int_R_prec_cut[current_cut_rprec] = (float) int_precis;
            current_cut_rprec--;
        }

        if (j == eval->num_rel) {
            eval->R_recall_precis = (float) precis;
            eval->int_R_recall_precis = (float) int_precis;
        }

        if (j < eval->num_rel) {
            eval->av_R_precis += (float) precis;
            eval->int_av_R_precis += (float) int_precis;
        }

        if (tr_vec->tr[j - 1].rel) {
            eval->int_av_recall_precis += (float) int_precis;
            eval->av_recall_precis += (float) precis;
            eval->avg_doc_prec += (float) precis;
            while (rel_so_far == cut_rp[current_cut_rp]) {
                eval->int_recall_precis[current_cut_rp] = (float) int_precis;
                current_cut_rp--;
            }
            rel_so_far--;
        } else {
            /* Note: for fallout-recall, the recall at X non-rel docs is
             * used for the recall 'after' (X-1) non-rel docs. Ie.
             * recall_used(X-1 non-rel docs) = MAX (recall(Y)) for Y
             * retrieved docs where X-1 non-rel retrieved */
            while (current_cut_fr >= 0 &&
              j - rel_so_far == cut_fr[current_cut_fr] + 1) {
                eval->fall_recall[current_cut_fr] = (float) recall;
                current_cut_fr--;
            }
            if (j - rel_so_far < MAX_FALL_RET) {
                eval->av_fall_recall += (float) recall;
            }
        }
    }

    /* Fill in the 0.0 value for recall-precision (== max precision at
     * any point in the retrieval ranking) */
    eval->int_recall_precis[0] = (float) int_precis;

    /* Fill in those cutoff values and averages that were not achieved
     * because insufficient docs were retrieved. */
    for (i = 0; i < NUM_CUTOFF; i++) {
        if (eval->num_ret < cutoff[i]) {
            eval->recall_cut[i] = ((float) eval->num_rel_ret /
              (float) eval->num_rel);
            eval->precis_cut[i] = ((float) eval->num_rel_ret /
              (float) cutoff[i]);
            eval->rel_precis_cut[i] = (cutoff[i] < eval->num_rel) ?
              eval->precis_cut[i] : eval->recall_cut[i];
        }
    }
    for (i = 0; i < NUM_FR_PTS; i++) {
        if (eval->num_ret - eval->num_rel_ret < cut_fr[i]) {
            eval->fall_recall[i] = (float) eval->num_rel_ret /
              (float) eval->num_rel;
        }
    }
    if (eval->num_ret - eval->num_rel_ret < MAX_FALL_RET) {
        eval->av_fall_recall += ((MAX_FALL_RET -
            (eval->num_ret - eval->num_rel_ret))
          * ((float) eval->num_rel_ret / (float) eval->num_rel));
    }
    if (eval->num_rel > eval->num_ret) {
        eval->R_recall_precis = (float) eval->num_rel_ret /
          (float) eval->num_rel;
        eval->int_R_recall_precis = (float) eval->num_rel_ret /
          (float) eval->num_rel;
        for (i = eval->num_ret; i < eval->num_rel; i++) {
            eval->av_R_precis += (float) eval->num_rel_ret / (float) i;
            eval->int_av_R_precis += (float) eval->num_rel_ret / (float) i;
        }
    }
    for (i = 0; i < NUM_PREC_PTS; i++) {
        if (eval->num_ret < cut_rprec[i]) {
            eval->R_prec_cut[i] = (float) eval->num_rel_ret /
              (float) cut_rprec[i];
            eval->int_R_prec_cut[i] = (float) eval->num_rel_ret /
              (float) cut_rprec[i];
        }
    }

    /* The following cutoffs/averages are correct, since 0.0 should be
     * averaged in for the non-retrieved docs: av_recall_precis,
     * int_av_recall_prec, int_recall_prec, int_av3_recall_precis,
     * int_av11_recall_precis */

    /* Calculate other indirect evaluation measure averages. */
    /* average recall-precis of 3 and 11 intermediate points */
    eval->int_av3_recall_precis =
      (eval->int_recall_precis[three_pts[0]] +
      eval->int_recall_precis[three_pts[1]] +
      eval->int_recall_precis[three_pts[2]]) / 3.0F;
    for (i = 0; i < NUM_RP_PTS; i++) {
        eval->int_av11_recall_precis += eval->int_recall_precis[i];
    }
    eval->int_av11_recall_precis /= NUM_RP_PTS;


    /* Calculate all the other averages */
    if (eval->num_rel_ret > 0) {
        eval->av_recall_precis /= eval->num_rel;
        eval->int_av_recall_precis /= eval->num_rel;
    }

    eval->av_fall_recall /= MAX_FALL_RET;

    if (eval->num_rel) {
        eval->av_R_precis /= eval->num_rel;
        eval->int_av_R_precis /= eval->num_rel;
        eval->exact_recall = (float) eval->num_rel_ret / eval->num_rel;
        eval->exact_precis = (float) eval->num_rel_ret / eval->num_ret;
        if (eval->num_rel > eval->num_ret)
            eval->exact_rel_precis = eval->exact_precis;
        else
            eval->exact_rel_precis = eval->exact_recall;
    }

    return (1);
}

static int compare_iter_rank(const void *vptr1, const void *vptr2) {
    const TR_TUP *tr1 = vptr1, 
                 *tr2 = vptr2;

    if (tr1->iter > tr2->iter) {
        return (-1);
    }
    if (tr1->iter < tr2->iter) {
        return (1);
    }
    if (tr1->rank < tr2->rank) {
        return (-1);
    }
    if (tr1->rank > tr2->rank) {
        return (1);
    }
    return (0);
}

/* Based on Falk Scholer's implementation in awk of the Wilcoxon
 * Matched-Pairs Signed-Ranks Test or Wilcoxon Signed-Ranks Test */

/** Performs a Wilcoxon signed rank test for a paired-difference experiment
  * at the 0.05 and 0.10 significance levels.
  *
  * The null hypothesis (probability distributions of samples 1 and 2 
  * are identical) is rejected if 
  *     test statistic <= critical value for n = number of observations
  *
  * Note that this is a _two-tailed_ test, so that for a level of
  * significance, ALPHA, the null hypothesis is rejected if the
  * (absolute value of the) test statistic exceeds the critical value of
  * the t-distribution at ALPHA/2  */

#define WILCOXON_TABLE_SIZE 50
#define Z_SCORE_TABLE_SIZE 410
#define NEGATIVE -1
#define POSITIVE 1

struct data_point {
    float absolute_difference;
    int sign;
};

static int compare_data_points(struct data_point *data_point1,
  struct data_point *data_point2) {
    if (data_point1->absolute_difference
      > data_point2->absolute_difference) {
        return (1);
    }
    if (data_point1->absolute_difference
      < data_point2->absolute_difference) {
        return (-1);
    }
    return (0);
}

static int calculate_statistics(unsigned int statistic_id,
  struct treceval_statistics *stats, unsigned int number_of_queries,
  float *measurements[2]) {
    unsigned int i = 0;
    float difference = (float) 0;
    struct data_point *data_points = NULL;
    float *ranks;
    unsigned int non_zero_results_counter = 0;
    unsigned int counter = 0;
    float average = (float) 0;
    unsigned int old_position = 0;
    float number_of_positive_differences = (float) 0;
    float number_of_negative_differences = (float) 0;
    int number_of_queries_improved = 0;
    int number_of_queries_degraded = 0;
    float z = (float) 0;
    float rounded_z = (float) 0;
    int test_stat = 0;

    /* Wilcoxon's table of critical values of T at alpha = .1 and .05 */
    const int wilcoxon_critical_values[WILCOXON_TABLE_SIZE][2] = { 
      {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {1, -1},
      {2, 1}, {4, 2}, {6, 6}, {8, 6}, {11, 8},
      {14, 11}, {17, 14}, {21, 17}, {26, 21}, {30, 25},
      {36, 30}, {41, 35}, {47, 40}, {54, 46}, {60, 52},
      {68, 59}, {75, 66}, {83, 73}, {92, 81}, {101, 90},
      {110, 98}, {120, 107}, {130, 117}, {141, 127}, {152, 137},
      {163, 148}, {175, 159}, {188, 171}, {201, 183}, {214, 195},
      {228, 208}, {242, 222}, {256, 235}, {271, 250}, {287, 264},
      {303, 279}, {317, 295}, {336, 311}, {353, 327}, {371, 344},
      {389, 361}, {408, 379}, {427, 397}, {446, 415}, {466, 434}
    };

    const float z_scores[Z_SCORE_TABLE_SIZE] = { 
      0.00000F, 0.00399F, 0.00798F, 0.01197F, 0.01595F,
      0.01994F, 0.02392F, 0.02790F, 0.03188F, 0.03586F,
      0.03983F, 0.04380F, 0.04776F, 0.05172F, 0.05567F,
      1.05962F, 0.06356F, 0.06749F, 0.07142F, 0.07535F,
      0.07926F, 0.08317F, 0.08706F, 0.09095F, 0.09483F,
      0.09871F, 0.10257F, 0.10642F, 0.11026F, 0.11409F,
      0.11791F, 0.12172F, 0.12552F, 0.12930F, 0.13307F,
      0.13683F, 0.14058F, 0.14431F, 0.14803F, 0.15173F,
      0.15542F, 0.15910F, 0.16276F, 0.16640F, 0.17003F,
      0.17364F, 0.17724F, 0.18082F, 0.18439F, 0.18793F,
      0.19146F, 0.19497F, 0.19847F, 0.20194F, 0.20540F,
      0.20884F, 0.21226F, 0.21566F, 0.21904F, 0.22240F,
      0.22575F, 0.22907F, 0.23237F, 0.23565F, 0.23891F,
      0.24215F, 0.24537F, 0.24857F, 0.25175F, 0.25490F,
      0.25804F, 0.26115F, 0.26424F, 0.26730F, 0.27035F,
      0.27337F, 0.27637F, 0.27935F, 0.28230F, 0.28524F,
      0.28814F, 0.29103F, 0.29389F, 0.29673F, 0.29955F,
      0.30234F, 0.30511F, 0.30785F, 0.31057F, 0.31327F,
      0.31594F, 0.31859F, 0.32121F, 0.32381F, 0.32639F,
      0.32894F, 0.33147F, 0.33398F, 0.33646F, 0.33891F,
      0.34134F, 0.34375F, 0.34614F, 0.34849F, 0.35083F,
      0.35314F, 0.35543F, 0.35769F, 0.35993F, 0.36214F,
      0.36433F, 0.36650F, 0.36864F, 0.37076F, 0.37286F,
      0.37493F, 0.37698F, 0.37900F, 0.38100F, 0.38298F,
      0.38493F, 0.38686F, 0.38877F, 0.39065F, 0.39251F,
      0.39435F, 0.39617F, 0.39796F, 0.39973F, 0.40147F,
      0.40320F, 0.40490F, 0.40658F, 0.40824F, 0.40988F,
      0.41149F, 0.41308F, 0.41466F, 0.41621F, 0.41774F,
      0.41924F, 0.42073F, 0.42220F, 0.42364F, 0.42507F,
      0.42647F, 0.42785F, 0.42922F, 0.43056F, 0.43189F,
      0.43319F, 0.43448F, 0.43574F, 0.43699F, 0.43822F,
      0.43943F, 0.44062F, 0.44179F, 0.44295F, 0.44408F,
      0.44520F, 0.44630F, 0.44738F, 0.44845F, 0.44950F,
      0.45053F, 0.45154F, 0.45254F, 0.45352F, 0.45449F,
      0.45543F, 0.45637F, 0.45728F, 0.45818F, 0.45907F,
      0.45994F, 0.46080F, 0.46164F, 0.46246F, 0.46327F,
      0.46407F, 0.46485F, 0.46562F, 0.46638F, 0.46712F,
      0.46784F, 0.46856F, 0.46926F, 0.46995F, 0.47062F,
      0.47128F, 0.47193F, 0.47257F, 0.47320F, 0.47381F,
      0.47441F, 0.47500F, 0.47558F, 0.47615F, 0.47670F,
      0.47725F, 0.47778F, 0.47831F, 0.47882F, 0.47932F,
      0.47982F, 0.48030F, 0.48077F, 0.48124F, 0.48169F,
      0.48214F, 0.48257F, 0.48300F, 0.48341F, 0.48382F,
      0.48422F, 0.48461F, 0.48500F, 0.48537F, 0.48574F,
      0.48610F, 0.48645F, 0.48679F, 0.48713F, 0.48745F,
      0.48778F, 0.48809F, 0.48840F, 0.48870F, 0.48899F,
      0.48928F, 0.48956F, 0.48983F, 0.49010F, 0.49036F,
      0.49061F, 0.49086F, 0.49111F, 0.49134F, 0.49158F,
      0.49180F, 0.49202F, 0.49224F, 0.49245F, 0.49266F,
      0.49286F, 0.49305F, 0.49324F, 0.49343F, 0.49361F,
      0.49379F, 0.49396F, 0.49413F, 0.49430F, 0.49446F,
      0.49461F, 0.49477F, 0.49492F, 0.49506F, 0.49520F,
      0.49534F, 0.49547F, 0.49560F, 0.49573F, 0.49585F,
      0.49598F, 0.49609F, 0.49621F, 0.49632F, 0.49643F,
      0.49653F, 0.49664F, 0.49674F, 0.49683F, 0.49693F,
      0.49702F, 0.49711F, 0.49720F, 0.49728F, 0.49736F,
      0.49744F, 0.49752F, 0.49760F, 0.49767F, 0.49774F,
      0.49781F, 0.49788F, 0.49795F, 0.49801F, 0.49807F,
      0.49813F, 0.49819F, 0.49825F, 0.49831F, 0.49836F,
      0.49841F, 0.49846F, 0.49851F, 0.49856F, 0.49861F,
      0.49865F, 0.49869F, 0.49874F, 0.49878F, 0.49882F,
      0.49886F, 0.49889F, 0.49893F, 0.49896F, 0.49900F,
      0.49903F, 0.49906F, 0.49910F, 0.49913F, 0.49916F,
      0.49918F, 0.49921F, 0.49924F, 0.49926F, 0.49929F,
      0.49931F, 0.49934F, 0.49936F, 0.49938F, 0.49940F,
      0.49942F, 0.49944F, 0.49946F, 0.49948F, 0.49950F,
      0.49952F, 0.49953F, 0.49955F, 0.49957F, 0.49958F,
      0.49960F, 0.49961F, 0.49962F, 0.49964F, 0.49965F,
      0.49966F, 0.49968F, 0.49969F, 0.49970F, 0.49971F,
      0.49972F, 0.49973F, 0.49974F, 0.49975F, 0.49976F,
      0.49977F, 0.49978F, 0.49978F, 0.49979F, 0.49980F,
      0.49981F, 0.49981F, 0.49982F, 0.49983F, 0.49983F,
      0.49984F, 0.49985F, 0.49985F, 0.49986F, 0.49986F,
      0.49987F, 0.49987F, 0.49988F, 0.49988F, 0.49989F,
      0.49989F, 0.49990F, 0.49990F, 0.49990F, 0.49991F,
      0.49991F, 0.49992F, 0.49992F, 0.49992F, 0.49992F,
      0.49993F, 0.49993F, 0.49993F, 0.49994F, 0.49994F,
      0.49994F, 0.49994F, 0.49995F, 0.49995F, 0.49995F,
      0.49995F, 0.49995F, 0.49996F, 0.49996F, 0.49996F,
      0.49996F, 0.49996F, 0.49996F, 0.49997F, 0.49997F,
      0.49997F, 0.49997F, 0.49997F, 0.49997F, 0.49997F,
      0.49997F, 0.49998F, 0.49998F, 0.49998F, 0.49998F
    };

    if (((data_points = malloc(sizeof(*data_points) * number_of_queries))
      == NULL)
      || ((ranks = malloc(sizeof(float) * number_of_queries)) == NULL)) {
        return (-1);
    }

    for (i = 0; i < number_of_queries; i++) {
        difference = measurements[0][i] - measurements[1][i];
        if (difference < (float) 0) {
            data_points[non_zero_results_counter].sign = NEGATIVE;
            data_points[non_zero_results_counter].absolute_difference
              = -difference;
            non_zero_results_counter++;
        } else if (difference > 0) {
            data_points[non_zero_results_counter].sign = POSITIVE;
            data_points[non_zero_results_counter].absolute_difference
              = difference;
            non_zero_results_counter++;
        }
    }
    qsort(data_points, non_zero_results_counter, sizeof(*(data_points)),
      (int (*)(const void *, const void *)) compare_data_points);

    /* Resolve rankings of absolute_differences */
    counter = 0;
    while (counter < non_zero_results_counter) {
        /* reset average at start of every turn */
        average = (float) 0;
        old_position = counter;
        /* counting duplicates */
        while ((counter < (non_zero_results_counter - 1))
          && (data_points[counter].absolute_difference
          == data_points[counter + 1].absolute_difference)) {
            average += counter;
            counter++;
        }

        if (counter > old_position) {
            average += counter;
            counter++;
            average /= (float) (counter - old_position);
            for (i = old_position; i < counter; i++) {
                ranks[i] = average + (float) 1;
            }
        } else {
            ranks[counter] = (float) (counter + 1);
            counter++;
        }
    }

    /* Summing the '+' and '-' ranks */
    for (i = 0; i < non_zero_results_counter; i++) {
        if (data_points[i].sign == POSITIVE) {
            number_of_positive_differences += ranks[i];
            number_of_queries_degraded++;
        } else {
            number_of_negative_differences += ranks[i];
            number_of_queries_improved++;
        }
    }

    free(ranks);
    free(data_points);

    /* Calculate large-sample approximation (n >= 25) Rejection region:
     * (a) 2-tailed test: z > z_alpha/2 or z < -z_alpha/2 (b)
     * right-tailed: z > z_alpha (c) left-tailed: z < -z_alpha */

    z = (float) ((number_of_positive_differences 
      - (((float) (non_zero_results_counter
      * (non_zero_results_counter + 1))) / ((float) 4)))
      / sqrt(((float) (non_zero_results_counter * (non_zero_results_counter +
            1) * ((2 * non_zero_results_counter) + 1))) / ((float) 24)));

    z *= (float) 100;
    rounded_z = (float) ((int) z);
    if ((z - rounded_z) >= 0.5) {
        rounded_z += (float) 1;
    }
    rounded_z /= (float) 100;
    z = rounded_z;

    /* Generate test statistic (the smaller of PosSum and NegSum) */
    if (number_of_positive_differences > number_of_negative_differences) {
        test_stat = (int) number_of_negative_differences;
    } else {
        test_stat = (int) number_of_positive_differences;
    }

    stats->stats[statistic_id].improved = number_of_queries_improved;
    stats->stats[statistic_id].degraded = number_of_queries_degraded;
    stats->stats[statistic_id].z_score = z;

    if (non_zero_results_counter == 0) {
        stats->stats[statistic_id].hypothesis = NO_DIFF;
    } else if ((non_zero_results_counter > WILCOXON_TABLE_SIZE)
      || (wilcoxon_critical_values[non_zero_results_counter - 1][0] == -1)) {
        stats->stats[statistic_id].hypothesis = OUT_OF_RANGE;
    } else {
        if (stats->tailedness == ONE_TAILED) {
            if (test_stat <=
              wilcoxon_critical_values[non_zero_results_counter - 1][1]) {
                stats->stats[statistic_id].hypothesis = REJECTED;
                stats->stats[statistic_id].confidence = 0.05F;
            } else {
                stats->stats[statistic_id].hypothesis = NOT_REJECTED;
                stats->stats[statistic_id].confidence = 0.05F;
            }
        } else {
            if (test_stat <=
              wilcoxon_critical_values[non_zero_results_counter - 1][1]) {
                stats->stats[statistic_id].hypothesis = REJECTED;
                stats->stats[statistic_id].confidence = 0.05F;
            } else if (test_stat <=
              wilcoxon_critical_values[non_zero_results_counter - 1][0]) {
                stats->stats[statistic_id].hypothesis = REJECTED;
                stats->stats[statistic_id].confidence = 0.1F;
            } else {
                stats->stats[statistic_id].hypothesis = NOT_REJECTED;
                stats->stats[statistic_id].confidence = 0.1F;
            }
        }
    }

    if (z < (float) 0) {
        z *= (float) -1;
    }
    if (stats->tailedness == ONE_TAILED) {
        /* 4.09 is largest z-score in lookup table... */
        if ((z * (float) 100) >= Z_SCORE_TABLE_SIZE) {
            /* value in table is for area */
            stats->stats[statistic_id].sign = '<';
            stats->stats[statistic_id].actual_confidence =
              (float) (1 - (0.49998 + 0.5));
        } else {
            /* value in table is for area; mu=0 to z */
            stats->stats[statistic_id].sign = '=';
            stats->stats[statistic_id].actual_confidence =
              ((float) 1 - (z_scores[(int) (z * (float) 100)] + 0.5F));
        }
    } else {
        /* 4.09 is largest z-score in lookup table... */
        if ((z * (float) 100) >= Z_SCORE_TABLE_SIZE) {
            /* value in table is for area */
            stats->stats[statistic_id].sign = '<';
            stats->stats[statistic_id].actual_confidence =
              ((float) 1 - (0.49998F * (float) 2));
        } else {
            /* value in table is for area; mu=0 to z */
            stats->stats[statistic_id].sign = '=';
            stats->stats[statistic_id].actual_confidence =
              ((float) 1 - (z_scores[(int) (z * (float) 100)] * (float) 2));
        }
    }

    return non_zero_results_counter;
}

static void print_stat(const struct treceval_statistics *stats,
  const unsigned int id, FILE * output) {
    if (stats->stats[id].hypothesis == NO_DIFF) {
        fprintf(stdout, "(runs identical, no stats computed)\n");
    } else if (stats->stats[id].hypothesis == OUT_OF_RANGE) {
        fprintf(stdout, "(out of range)    %6.3f    %c%6.5f\n",
          stats->stats[id].z_score, stats->stats[id].sign,
          stats->stats[id].actual_confidence);
    } else {
        if (stats->stats[id].hypothesis == REJECTED) {
            fprintf(stdout, "rejected at %2.2f  %6.3f    %c %6.5f\n",
              stats->stats[id].confidence, stats->stats[id].z_score,
              stats->stats[id].sign, stats->stats[id].actual_confidence);
        } else {
            fprintf(stdout, "not rejd at %2.2f  %6.3f    %c %6.5f\n",
              stats->stats[id].confidence, stats->stats[id].z_score,
              stats->stats[id].sign, stats->stats[id].actual_confidence);
        }
    }
}

int treceval_stats_print(const struct treceval_statistics *stats,
  FILE *output) {
    if (stats->tailedness == ONE_TAILED) {
        fprintf(output, "Wilcoxon signed rank test (one-tailed)\n");
    } else {
        fprintf(output, "Wilcoxon signed rank test (two-tailed)\n");
    }
    if (stats->sample_size <= 0) {
        return (0);
    }
    fprintf(output, "Running Statistics over %d queries.\n",
      stats->sample_size);
    fprintf(output, "---------------------------------------------------"
      "----------------------\n");
    fprintf(output, "measures        improved degraded   Null Hypothesis"
      "   z-score   actual p\n");
    fprintf(output, "Avg Precision      %2d       %2d      ",
      stats->stats[0].improved, stats->stats[0].degraded);
    print_stat(stats, 0, output);
    fprintf(output, "Precision@   5     %2d       %2d      ",
      stats->stats[1].improved, stats->stats[1].degraded);
    print_stat(stats, 1, output);
    fprintf(output, "Precision@  10     %2d       %2d      ",
      stats->stats[2].improved, stats->stats[2].degraded);
    print_stat(stats, 2, output);
    fprintf(output, "Precision@  15     %2d       %2d      ",
      stats->stats[3].improved, stats->stats[3].degraded);
    print_stat(stats, 3, output);
    fprintf(output, "Precision@  20     %2d       %2d      ",
      stats->stats[4].improved, stats->stats[4].degraded);
    print_stat(stats, 4, output);
    fprintf(output, "Precision@  30     %2d       %2d      ",
      stats->stats[5].improved, stats->stats[5].degraded);
    print_stat(stats, 5, output);
    fprintf(output, "Precision@ 100     %2d       %2d      ",
      stats->stats[6].improved, stats->stats[6].degraded);
    print_stat(stats, 6, output);
    fprintf(output, "Precision@ 200     %2d       %2d      ",
      stats->stats[7].improved, stats->stats[7].degraded);
    print_stat(stats, 7, output);
    fprintf(output, "Precision@ 500     %2d       %2d      ",
      stats->stats[8].improved, stats->stats[8].degraded);
    print_stat(stats, 8, output);
    fprintf(output, "Precision@1000     %2d       %2d      ",
      stats->stats[9].improved, stats->stats[9].degraded);
    print_stat(stats, 9, output);
    fprintf(output, "R Precision        %2d       %2d      ",
      stats->stats[10].improved, stats->stats[10].degraded);
    print_stat(stats, 10, output);
    fprintf(output, "---------------------------------------------------"
      "----------------------\n");

    return (1);
}

static int compare_query_ids(result_tuple *tuple1, result_tuple *tuple2) {
    if (tuple1->query_id > tuple2->query_id) {
        return (1);
    }
    if (tuple1->query_id < tuple2->query_id) {
        return (-1);
    }
    return (0);
}

int treceval_stats_calculate(const struct treceval *trec_results_a,
  const struct treceval *trec_results_b, struct treceval_statistics *stats,
  const struct treceval_qrels *qrels,
  treceval_statistics_tailedness tailedness) {
    unsigned int i = 0;
    unsigned int start1 = 0;
    unsigned int start2 = 0;
    unsigned int end1 = 0;
    unsigned int end2 = 0;
    unsigned int query_count = 0;
    unsigned int max_query_count = 0;
    unsigned int evaluated1 = 1;
    unsigned int evaluated2 = 1;
    struct treceval_results *evaluations[2];
    float *measurements[2];

    qsort(trec_results_a->tuples, trec_results_a->cached_results,
      sizeof(*(trec_results_a->tuples)),
      (int (*)(const void *, const void *)) compare_query_ids);
    qsort(trec_results_b->tuples, trec_results_b->cached_results,
      sizeof(*(trec_results_a->tuples)),
      (int (*)(const void *, const void *)) compare_query_ids);

    stats->tailedness = tailedness;

    /* checking whether both results are empty in which case it is
     * useless to calculate statistics */
    if ((trec_results_a->cached_results == 0)
      || (trec_results_b->cached_results == 0)) {
        /* need to zero results */
        stats->sample_size = 0;
        for (i = 0; i < 11; i++) {
            stats->stats[i].improved = 0;
            stats->stats[i].degraded = 0;
            stats->stats[i].hypothesis = OUT_OF_RANGE;
            stats->stats[i].confidence = (float) 0;
            stats->stats[i].z_score = (float) 0;
            stats->stats[i].actual_confidence = (float) 0;
            stats->stats[i].sign = '?';
        }
        return 0;
    }

    /* counting query IDs in the first pass only, so we can set enough
     * results aside */
    while ((end1 < trec_results_a->cached_results)
      || (end2 < trec_results_b->cached_results)) {
        /* if this input has been evaluated last time 'round (see next
         * comment) then advance the input for the next evaluation */
        if (evaluated1) {
            start1 = end1;
            for (; end1 < trec_results_a->cached_results; end1++) {
                if (trec_results_a->tuples[end1].query_id !=
                  trec_results_a->tuples[start1].query_id) {
                    break;
                }
            }
        }
        if (evaluated2) {
            start2 = end2;
            for (; end2 < trec_results_b->cached_results; end2++) {
                if (trec_results_b->tuples[end2].query_id !=
                  trec_results_b->tuples[start2].query_id) {
                    break;
                }
            }
        }

        /* Checking that both are evaluating the same query. If not,
         * then have to make sure that only the one with the current
         * query_id gets evaluated whereas the results for the other is
         * zeroed. Also have to keep track of which one of the input
         * should be advanced next. */
        evaluated1 = 1;
        evaluated2 = 1;
        if (trec_results_a->tuples[start1].query_id !=
          trec_results_b->tuples[start2].query_id) {
            if (trec_results_a->tuples[start1].query_id >
              trec_results_b->tuples[start2].query_id) {
                evaluated1 = 0;
            } else {
                evaluated2 = 0;
            }
        }

        max_query_count++;
    }
    end1 = 0;
    end2 = 0;

    /* setting aside memory */
    if (((evaluations[0] =
      malloc(sizeof(struct treceval_results) * max_query_count)) == NULL)
      || ((evaluations[1] =
      malloc(sizeof(struct treceval_results) * max_query_count)) == NULL)) {
        return (0);
    }
    if (((measurements[0] = malloc(sizeof(float) * max_query_count)) == NULL)
      || ((measurements[1] 
          = malloc(sizeof(float) * max_query_count)) == NULL)) {
        return (0);
    }

    /* need to keep going until both inputs are exhausted */
    while ((end1 < trec_results_a->cached_results)
      || (end2 < trec_results_b->cached_results)) {
        /* if this input has been evaluated last time 'round (see next
         * comment) then advance the input for the next evaluation */
        if (evaluated1) {
            start1 = end1;
            for (; end1 < trec_results_a->cached_results; end1++) {
                if (trec_results_a->tuples[end1].query_id !=
                  trec_results_a->tuples[start1].query_id) {
                    break;
                }
            }
        }
        if (evaluated2) {
            start2 = end2;
            for (; end2 < trec_results_b->cached_results; end2++) {
                if (trec_results_b->tuples[end2].query_id !=
                  trec_results_b->tuples[start2].query_id) {
                    break;
                }
            }
        }

        /* Checking that both are evaluating the same query. If not,
         * then have to make sure that only the one with the current
         * query_id gets evaluated whereas the results for the other is
         * zeroed. Also have to keep track of which one of the input
         * should be advanced next. At the same time I changed the two
         * corresponding lines 50 lines above. */
        evaluated1 = 1;
        evaluated2 = 1;
        /* FIXME: just fixed a bug whereby I interchanged the
         * evaluated2/1 lines with each other (without too much thinking
         * about the problem) and everything seems to be working.
         * However if there is another problem, it might be worth
         * investigating this first. */
        if (trec_results_a->tuples[start1].query_id ==
          trec_results_b->tuples[start2].query_id) {
            evaluate_trec_results(start1, end1 - 1, trec_results_a, qrels,
              &evaluations[0][query_count]);
            evaluate_trec_results(start2, end2 - 1, trec_results_b, qrels,
              &evaluations[1][query_count]);
        } else if (trec_results_a->tuples[start1].query_id <
          trec_results_b->tuples[start2].query_id) {
            evaluate_trec_results(start1, end1 - 1, trec_results_a, qrels,
              &evaluations[0][query_count]);
            evaluate_trec_results(0, 0, trec_results_b, qrels,
              &evaluations[1][query_count]);
            evaluated2 = 0;
        } else {
            evaluate_trec_results(0, 0, trec_results_a, qrels,
              &evaluations[0][query_count]);
            evaluate_trec_results(start2, end2 - 1, trec_results_b, qrels,
              &evaluations[1][query_count]);
            evaluated1 = 0;
        }

        query_count++;
    }

    stats->sample_size = max_query_count;
    for (i = 0; i < max_query_count; i++) {
        measurements[0][i] = evaluations[0][i].average_precision;
        measurements[1][i] = evaluations[1][i].average_precision;
    }
    if (calculate_statistics(0, stats, max_query_count, measurements) < 0) {
        return (0);
    }
    for (i = 0; i < max_query_count; i++) {
        measurements[0][i] = evaluations[0][i].precision_at[0];
        measurements[1][i] = evaluations[1][i].precision_at[0];
    }
    if (calculate_statistics(1, stats, max_query_count, measurements) < 0) {
        return (0);
    }
    for (i = 0; i < max_query_count; i++) {
        measurements[0][i] = evaluations[0][i].precision_at[1];
        measurements[1][i] = evaluations[1][i].precision_at[1];
    }
    if (calculate_statistics(2, stats, max_query_count, measurements) < 0) {
        return (0);
    }
    for (i = 0; i < max_query_count; i++) {
        measurements[0][i] = evaluations[0][i].precision_at[2];
        measurements[1][i] = evaluations[1][i].precision_at[2];
    }
    if (calculate_statistics(3, stats, max_query_count, measurements) < 0) {
        return (0);
    }
    for (i = 0; i < max_query_count; i++) {
        measurements[0][i] = evaluations[0][i].precision_at[3];
        measurements[1][i] = evaluations[1][i].precision_at[3];
    }
    if (calculate_statistics(4, stats, max_query_count, measurements) < 0) {
        return (0);
    }
    for (i = 0; i < max_query_count; i++) {
        measurements[0][i] = evaluations[0][i].precision_at[4];
        measurements[1][i] = evaluations[1][i].precision_at[4];
    }
    if (calculate_statistics(5, stats, max_query_count, measurements) < 0) {
        return (0);
    }
    for (i = 0; i < max_query_count; i++) {
        measurements[0][i] = evaluations[0][i].precision_at[5];
        measurements[1][i] = evaluations[1][i].precision_at[5];
    }
    if (calculate_statistics(6, stats, max_query_count, measurements) < 0) {
        return (0);
    }
    for (i = 0; i < max_query_count; i++) {
        measurements[0][i] = evaluations[0][i].precision_at[6];
        measurements[1][i] = evaluations[1][i].precision_at[6];
    }
    if (calculate_statistics(7, stats, max_query_count, measurements) < 0) {
        return (0);
    }
    for (i = 0; i < max_query_count; i++) {
        measurements[0][i] = evaluations[0][i].precision_at[7];
        measurements[1][i] = evaluations[1][i].precision_at[7];
    }
    if (calculate_statistics(8, stats, max_query_count, measurements) < 0) {
        return (0);
    }
    for (i = 0; i < max_query_count; i++) {
        measurements[0][i] = evaluations[0][i].precision_at[8];
        measurements[1][i] = evaluations[1][i].precision_at[8];
    }
    if (calculate_statistics(9, stats, max_query_count, measurements) < 0) {
        return (0);
    }
    for (i = 0; i < max_query_count; i++) {
        measurements[0][i] = evaluations[0][i].rprecision;
        measurements[1][i] = evaluations[1][i].rprecision;
    }
    if (calculate_statistics(10, stats, max_query_count, measurements) < 0) {
        return (0);
    }

    free(measurements[0]);
    free(measurements[1]);
    free(evaluations[0]);
    free(evaluations[1]);
    return 1;
}

/* Assumes that the first entry in the relevance judgement contains a
 * result for the query with the smallest ID. */
struct treceval_qrels *treceval_qrels_new(const char *qrels_file_name) {
    unsigned int i = 0;
    struct treceval_qrels *qrels = NULL;
    FILE *qrels_file = NULL;
    char line[MAX_LINE_LENGTH];
    char *line_pointer = NULL;
    char *document_number_pointer = NULL;
    char judgement[MAX_LINE_LENGTH];
    int query_id = 0;
    char *judgement_string = NULL;

    if ((qrels = malloc(sizeof(struct treceval_qrels))) == NULL) {
        return NULL;
    }
    qrels->number_of_judged_queries = 50;
    qrels->lowest_query_id = -1;
    qrels->number_of_judgements_for_query = NULL;

    /* setting aside memeory for hash table */
    if ((qrels->judgements = chash_ptr_new(8, 0.8F, 
      (unsigned int (*)(const void *)) str_hash,
      (int (*)(const void *, const void *)) str_cmp)) == NULL) {
        return NULL;
    }

    if ((qrels->number_of_judgements_for_query =
        malloc(sizeof(int) * qrels->number_of_judged_queries)) == NULL) {
        return NULL;
    }
    for (i = 0; i < (unsigned int) qrels->number_of_judged_queries; i++) {
        qrels->number_of_judgements_for_query[i] = 0;
    }

    if ((qrels_file = fopen(qrels_file_name, "r")) == NULL) {
        return NULL;
    }

    /* populating relevance judgements hash table */
    while (fgets(line, MAX_LINE_LENGTH, qrels_file) != NULL) {
        /* Only care whether a document is relevant. */
        if (line[strlen(line) - 2] != '0') {
            line_pointer = line;
            /* getting Query ID first */
            while (*line_pointer != ' ') {
                line_pointer++;
            }
            *line_pointer = '\0';
            query_id = atoi(line);
            /* need to set the lowest query ID if that hasn't been done
             * before */
            if (qrels->lowest_query_id == -1) {
                qrels->lowest_query_id = query_id;
            }
            /* if the query ID had been set before and it was larger
             * than the current query ID we give up */
            else if (query_id < qrels->lowest_query_id) {
                return NULL;
            }
            /* if the current query ID is larger than the number of IDs
             * we have set space aside for, we need to increas space for
             * new queries.  Note that we assume that we need to have
             * enough space for all query IDs, from the lowest to the
             * highest (ie we don't deal with the fact that we might
             * have a (potentially large) gap in IDs and therefore might
             * waste (lots of) space. */
            else if (query_id >=
              (qrels->lowest_query_id + qrels->number_of_judged_queries)) {
                /* need to set aside memory for more queries */
                if ((qrels->number_of_judgements_for_query =
                  realloc(qrels->number_of_judgements_for_query, sizeof(int)
                  * (qrels->number_of_judged_queries + 50))) == NULL) {
                    return NULL;
                }
                /* need to initialise judgement counts for new queries */
                for (i = qrels->number_of_judged_queries;
                  i < (unsigned int) (qrels->number_of_judged_queries + 50); 
                  i++) {
                    qrels->number_of_judgements_for_query[i] = 0;
                }
                qrels->number_of_judged_queries += 50;
            }

            /* Extracting document number. */
            strcpy(judgement, line);
            strcat(judgement, " ");
            line_pointer += 3;
            document_number_pointer = line_pointer;
            while (*line_pointer != ' ') {
                line_pointer++;
            }
            *line_pointer = '\0';
            strcat(judgement, document_number_pointer);

            /* Adding judgement in form "QID TRECDOCNO" (for instance
             * "381 FBIS3-1") into hash table. Just need to check for
             * presence when checking if document is relevant. */
            if ((judgement_string = malloc(sizeof(*judgement_string)
              * (strlen(judgement) + 1))) == NULL) {
                return NULL;
            }
            str_cpy(judgement_string, judgement);
            chash_ptr_ptr_insert(qrels->judgements, judgement_string, NULL);

            /* Incrementing the count of judgements for this query (so
             * that average precision can be calculated later. */
            qrels->number_of_judgements_for_query[query_id -
              qrels->lowest_query_id]++;
        }
    }

    fclose(qrels_file);
    return qrels;
}

struct treceval *treceval_new() {
    struct treceval *new_trec_eval = NULL;

    if ((new_trec_eval = malloc(sizeof(struct treceval))) == NULL) {
        return NULL;
    }
    new_trec_eval->cached_results = 0;
    new_trec_eval->cache_size = 50;

    /* setting aside memory for cached results */
    if ((new_trec_eval->tuples =
        malloc(sizeof(result_tuple) * new_trec_eval->cache_size)) == NULL) {
        return NULL;
    }

    return new_trec_eval;
}

void treceval_qrels_delete(struct treceval_qrels **qrels) {
    void **dummy_pointer = NULL;
    char *previous_judgement = NULL;
    struct chash_iter *iterator = chash_iter_new((*qrels)->judgements);
    const void *key = NULL;

    if (*qrels != NULL) {
        if ((*qrels)->number_of_judgements_for_query != NULL) {
            free((*qrels)->number_of_judgements_for_query);
        }
        if ((*qrels)->judgements != NULL) {
            while (chash_iter_ptr_ptr_next(iterator, &key,
              &dummy_pointer) == CHASH_OK) {
                if (previous_judgement != NULL) {
                    free(previous_judgement);
                }
                previous_judgement = (char *) key;
            }
            if (previous_judgement != NULL) {
                free(previous_judgement);
            }
            chash_iter_delete(iterator);
            chash_delete((*qrels)->judgements);
        }
        free(*qrels);
        *qrels = NULL;
    }
}

void treceval_delete(struct treceval **trec_results) {
    if (*trec_results != NULL) {
        if ((*trec_results)->tuples != NULL) {
            free((*trec_results)->tuples);
        }
        free((*trec_results));
        *trec_results = NULL;
    }
}

int treceval_add_result(struct treceval *trec_results,
  const unsigned int query_id, const char *trec_document_number, 
  const float score) {

    /* if the current cache is exhausted, need to increase the cache size */
    if (trec_results->cached_results == trec_results->cache_size) {
        trec_results->cache_size *= 2;
        if ((trec_results->tuples =
            realloc(trec_results->tuples,
              sizeof(result_tuple) * trec_results->cache_size)) == NULL) {
            return (0);
        }
    }

    /* copying the result fields into the cache tuple */
    trec_results->tuples[trec_results->cached_results].query_id = query_id;
    if (strlen(trec_document_number)
      >= (TREC_DOCUMENT_NUMBER_MAX_LENGTH - 1)) {
        return (0);
    }
    strcpy(trec_results->tuples[trec_results->cached_results].
      trec_document_number, trec_document_number);
    trec_results->tuples[trec_results->cached_results].score = score;
    trec_results->cached_results++;

    return 1;
}

void treceval_print_results(struct treceval_results *evaluated_results,
  const unsigned int number_of_results, FILE *output, int interpolated) {
    unsigned int i = 0,
                 j = 0;

    fprintf(output, "        Run:    ");
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6d", i);
    }
    fprintf(output, "\n");

    fprintf(output, "No. of QIDs:    ");
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6d", evaluated_results[i].queries);
    }
    fprintf(output, "\n");

    fprintf(output, "Total number of documents over all queries\n");
    fprintf(output, "    Retrieved:  ");
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6d", evaluated_results[i].retrieved);
    }
    fprintf(output, "\n");

    fprintf(output, "    Relevant:   ");
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6d", evaluated_results[i].relevant);
    }
    fprintf(output, "\n");

    fprintf(output, "    Rel_ret:    ");
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6d", evaluated_results[i].relevant_retrieved);
    }
    fprintf(output, "\n");

    if (interpolated) {
        fprintf(output, "Interpolated Recall - Precision Averages:\n");
        fprintf(output, "    at 0.00     ");
        j = 0;
        for (i = 0; i < number_of_results; i++) {
            fprintf(output, "   %6.4f", 
              evaluated_results[i].interpolated_rp[j]);
        }
        fprintf(output, "\n");

        fprintf(output, "    at 0.10     ");
        j++;
        for (i = 0; i < number_of_results; i++) {
            fprintf(output, "   %6.4f", 
              evaluated_results[i].interpolated_rp[j]);
        }
        fprintf(output, "\n");

        fprintf(output, "    at 0.20     ");
        j++;
        for (i = 0; i < number_of_results; i++) {
            fprintf(output, "   %6.4f", 
              evaluated_results[i].interpolated_rp[j]);
        }
        fprintf(output, "\n");

        fprintf(output, "    at 0.30     ");
        j++;
        for (i = 0; i < number_of_results; i++) {
            fprintf(output, "   %6.4f", 
              evaluated_results[i].interpolated_rp[j]);
        }
        fprintf(output, "\n");

        fprintf(output, "    at 0.40     ");
        j++;
        for (i = 0; i < number_of_results; i++) {
            fprintf(output, "   %6.4f", 
              evaluated_results[i].interpolated_rp[j]);
        }
        fprintf(output, "\n");

        fprintf(output, "    at 0.50     ");
        j++;
        for (i = 0; i < number_of_results; i++) {
            fprintf(output, "   %6.4f", 
              evaluated_results[i].interpolated_rp[j]);
        }
        fprintf(output, "\n");

        fprintf(output, "    at 0.60     ");
        j++;
        for (i = 0; i < number_of_results; i++) {
            fprintf(output, "   %6.4f", 
              evaluated_results[i].interpolated_rp[j]);
        }
        fprintf(output, "\n");

        fprintf(output, "    at 0.70     ");
        j++;
        for (i = 0; i < number_of_results; i++) {
            fprintf(output, "   %6.4f", 
              evaluated_results[i].interpolated_rp[j]);
        }
        fprintf(output, "\n");

        fprintf(output, "    at 0.80     ");
        j++;
        for (i = 0; i < number_of_results; i++) {
            fprintf(output, "   %6.4f", 
              evaluated_results[i].interpolated_rp[j]);
        }
        fprintf(output, "\n");

        fprintf(output, "    at 0.90     ");
        j++;
        for (i = 0; i < number_of_results; i++) {
            fprintf(output, "   %6.4f", 
              evaluated_results[i].interpolated_rp[j]);
        }
        fprintf(output, "\n");

        fprintf(output, "    at 1.00     ");
        j++;
        for (i = 0; i < number_of_results; i++) {
            fprintf(output, "   %6.4f", 
              evaluated_results[i].interpolated_rp[j]);
        }
        fprintf(output, "\n");
    }

    fprintf(output, "Average precision (non-interpolated) ");
    fprintf(output, "for all rel docs\n");
    fprintf(output, "  P(avg)        ");
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6.4f", evaluated_results[i].average_precision);
    }
    fprintf(output, "\n");

    fprintf(output, "Precision:\n  At    5 docs: ");
    j = 0;
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6.4f", evaluated_results[i].precision_at[j]);
    }
    fprintf(output, "\n");

    fprintf(output, "  At   10 docs: ");
    j++;
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6.4f", evaluated_results[i].precision_at[j]);
    }
    fprintf(output, "\n");

    fprintf(output, "  At   15 docs: ");
    j++;
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6.4f", evaluated_results[i].precision_at[j]);
    }
    fprintf(output, "\n");

    fprintf(output, "  At   20 docs: ");
    j++;
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6.4f", evaluated_results[i].precision_at[j]);
    }
    fprintf(output, "\n");

    fprintf(output, "  At   30 docs: ");
    j++;
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6.4f", evaluated_results[i].precision_at[j]);
    }
    fprintf(output, "\n");

    fprintf(output, "  At  100 docs: ");
    j++;
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6.4f", evaluated_results[i].precision_at[j]);
    }
    fprintf(output, "\n");

    fprintf(output, "  At  200 docs: ");
    j++;
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6.4f", evaluated_results[i].precision_at[j]);
    }
    fprintf(output, "\n");

    fprintf(output, "  At  500 docs: ");
    j++;
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6.4f", evaluated_results[i].precision_at[j]);
    }
    fprintf(output, "\n");

    fprintf(output, "  At 1000 docs: ");
    j++;
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6.4f", evaluated_results[i].precision_at[j]);
    }
    fprintf(output, "\n");

    fprintf(output, "R-Precision (precision after R (= num_rel for a ");
    fprintf(output, "query) docs retrieved):\n");
    fprintf(output, "    Exact:      ");
    for (i = 0; i < number_of_results; i++) {
        fprintf(output, "   %6.4f", evaluated_results[i].rprecision);
    }
    fprintf(output, "\n");

    fprintf(output, "\n");
}

int treceval_evaluate_query(const unsigned int query_id,
  struct treceval_results *evaluated_results,
  const struct treceval_qrels *qrels, const struct treceval *trec_results) {
    unsigned int i = 0;

    unsigned int start = 0;
    unsigned int end = 0;

    /* sorting tuples first */
    qsort(trec_results->tuples, trec_results->cached_results,
      sizeof(*(trec_results->tuples)),
      (int (*)(const void *, const void *)) compare_query_ids);

    /* checking whether there are no results or the query_id is out of
     * bounds */
    if ((trec_results->cached_results == 0)
      || (((unsigned int) trec_results->tuples[0].query_id) > query_id)
      || ((unsigned int)
          trec_results->tuples[trec_results->cached_results - 1].query_id
        < query_id)) {

        /* abusing way of zeroing the evaluated_results values */
        evaluate_trec_results(0, 0, trec_results, qrels, evaluated_results);
        return 0;
    }

    for (i = 0; i < trec_results->cached_results; i++) {
        if (((unsigned int) trec_results->tuples[i].query_id) == query_id) {
            start = i;
            break;
        }
    }
    for (; i < trec_results->cached_results; i++) {
        if (((unsigned int) trec_results->tuples[i].query_id) != query_id) {
            end = i - 1;
        }
    }
    /* if this is the last query in the results then the next query id
     * cannot be found */
    if (end == 0) {
        end = trec_results->cached_results - 1;
    }
    evaluate_trec_results(start, end, trec_results, qrels,
      evaluated_results);
    return 1;
}

int treceval_evaluate(const struct treceval *trec_results,
  const struct treceval_qrels *qrels,
  struct treceval_results *evaluated_results) {
    /* sorting tuples first */
    qsort(trec_results->tuples, trec_results->cached_results,
      sizeof(*(trec_results->tuples)),
      (int (*)(const void *, const void *)) compare_query_ids);
    evaluate_trec_results(0, trec_results->cached_results, trec_results,
      qrels, evaluated_results);
    return 1;
}

