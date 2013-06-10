/* trec_eval.h is an interface to perform trec_eval calculations
 * programmatically.  It is based upon the trec_eval program (7.0beta)
 * available from ftp://ftp.cs.cornell.edu/pub/smart.  trec_eval.c
 * contains code copyright Chris Buckley and Gerard Salton.
 *
 * written bodob, nml 2004-10-10
 *
 */

#ifndef TRECEVAL_H
#define TRECEVAL_H

#ifdef _cplusplus
extern "C" {
#endif

#include <stdio.h>

struct treceval_qrels;
struct treceval;

/* structure to hold evaluated results */
struct treceval_results {
    unsigned int queries;               /* number of queries this result
                                           is over */
    unsigned int retrieved;             /* number of retrieved documents */
    unsigned int relevant;              /* number of all relevant
                                           documents in collection */
    unsigned int relevant_retrieved;    /* relevant documents retrieved */
    float interpolated_rp[11];          /* interpolated recall-precision
                                           at 11 points, in ascending
                                           order of position */
    float average_precision;            /* average precision measure */
    float precision_at[9];              /* precision at 9 recall points:
                                           5, 10, 15, 20, 30, 100, 200,
                                           500, 1000 */
    float rprecision;                   /* r-precision value */
};

/* enumerated type used for statistics below */
typedef enum {
    NO_DIFF = 0, 
    REJECTED = 1, 
    OUT_OF_RANGE = 2, 
    NOT_REJECTED = 3
} treceval_hypothesis;

/* enumerated type used for selecting one tailed or two tailed test */
typedef enum {
    ONE_TAILED = 0,
    TWO_TAILED = 1
} treceval_statistics_tailedness;

struct treceval_statistics {
    unsigned int sample_size;           /* keep track of how many
                                           queries were evaluated in
                                           order to calculate the
                                           statistics */
    treceval_statistics_tailedness tailedness; /* keeps track whether
                                           one or two tail test was
                                           performed */
    struct {
        unsigned int improved;          /* this many queries have been
                                           improved */
        unsigned int degraded;          /* this many queries have been
                                           degraded */
        treceval_hypothesis hypothesis; /* hypothesis was (not) rejected
                                           ... */
        float confidence;               /* ... with this confidence */
        float z_score;                  /* the confidence level at which
                                           the hypothesis has been
                                           rejected/accepted */
        float actual_confidence;        /* the "exact" confidence ... */
        char sign;                      /* equal or smaller thant the
                                           value shown */
    } stats[11];
};

/* initialises an new treceval: sets memory aside for raw results;
 * returns a pointer to a new struct treceval which needs to be freed by
 * a call to the next function after useage */
struct treceval *treceval_new();

/* releases all memory and points *trecResults to NULL;
 * expects a pointer to a pointer of a treceval structure for which
 * memory was allocated on the heap */
void treceval_delete(struct treceval **raw_results);

/* fetches relevance judgements for later evaluation of results;
 * accepts a file name to a qrels file;
 * returns a pointer to a structure that contains a hash table and
 * auxiliary information needed for judging results; structure returned
 * needs to be freed by a later call to the following function */
struct treceval_qrels *treceval_qrels_new(const char *qrels_file_name);

/* releases all memory and points *qrels to NULL;
 * expects a pointer to a pointer to a qrels structure */
void treceval_qrels_delete(struct treceval_qrels **qrels);

/* adds a new trec tuple (query_id, trec document number, accumulator
 * Score) to the raw results;
 * accepts a query ID, trec document number, a similarity score of the
 * document to the query and a pointer to a relevance information
 * structure;
 * returns currently 1 if successful */
int treceval_add_result(struct treceval *raw_results, 
  const unsigned int query_id, const char *trec_document_number, 
  const float score);

/* results for a particular query_id are evaluated;
 * accepts a query ID, a pointer to a results structure, a pointer to a
 * qrels structure and a pointer to a structure containing the raw
 * results;
 * returns 0 in case there are no raw results entered for that
 *            particular query or there are no relevance judgements
 *            available for that query;
 *         1 if results were evaluated and placed in the structure of
 *           which a pointer was passed in */
int treceval_evaluate_query(const unsigned int query_id,
  struct treceval_results *evaluated_results,
  const struct treceval_qrels *qrels,
  const struct treceval *raw_results);

/* all results are evaluated
 * accepts a a pointer to a results structure, a pointer to a qrels
 * structure and a pointer to a structure containing raw results;
 * returns currently 1 */
int treceval_evaluate(const struct treceval *raw_results,
  const struct treceval_qrels *qrels,
  struct treceval_results *evaluated_results);

/* printing results
 * accepts a pointer to an array of evaluated results (or a pointer to a
 * pointer of a single evaluated result), the number of elements in the
 * array and a file descriptor (such as stdout, stderr) to which the
 * result is to be printed */
void treceval_print_results(struct treceval_results *evaluated_results,
  const unsigned int number_of_results, FILE *output, int interpolated);

/* calculate statistics between 2 runs over all those queries for which
 * at least one of the runs has an entry;
 * accepts pointers to two structures containing raw results, a pointer
 * to a strucuter in which the results of the statistics will be placed
 * and a pointer to relevance judgements;
 * returns 0 in case either of the raw results structures contains no
 *           tuples, or
 *         1 upon success */
int treceval_stats_calculate(const struct treceval *one,
  const struct treceval *two, struct treceval_statistics *stats,
  const struct treceval_qrels *qrels,
  treceval_statistics_tailedness tailedness);

/* print statistics
 * accepts a pointer to a statistics structure and a file descriptor to
 * which the output should be written, and whether to print out those
 * seemingly-useless interpolated statistics;
 * returns 1 on success, 0 in case the statistics haven't been
 * calculated (ie the structure is empty or corrupted) */
int treceval_stats_print(const struct treceval_statistics *stats,
  FILE *output);

#ifdef _cplusplus
}
#endif

#endif
