/* trecrun.c is a tool that answers queries in trec topic files and
 * outputs the answers in a format suitable for evaluation by the
 * trec_eval set of scripts
 *
 * trec_eval is available from ftp://ftp.cs.cornell.edu/pub/smart/
 * or http://trec.nist.gov/trec_eval/
 *
 * written nml 2003-06-06
 *
 */

#include "firstinclude.h"

#include "config.h"
#include "def.h"
#include "getlongopt.h"
#include "mlparse_wrap.h"
#include "docmap.h"           /* needed to set cache values on index load */
#include "index.h"
#include "str.h"
#include "summarise.h"
#include "trec_eval.h"
#include "timings.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void print_usage(FILE *output, const char *progname) {
    const char *name = strrchr(progname, '/');

    if (name) {
        name++;
    } else {
        name = progname;
    }

    fprintf(output, "usage: '%s index', where index is the name of the index "
      "to query\n", name);
    fprintf(output, "  query options:\n");
    fprintf(output, "    -f,--file=[topic_file]: add topic_file to list of "
      "topic files\n");
    fprintf(output, "    -F,--file-list=[file]: add files listed in file to "
      "list of topic files\n");
    fprintf(output, "    -r,--runid=[run_id]: output run_id as id for this "
      "evaluation\n");
    fprintf(output, "    -n,--number-results=[num]: number of results to "
      "output per query\n");
    fprintf(output, "    -t,--title: use title in query\n");
    fprintf(output, "    -d,--description: use description in query\n");
    fprintf(output, "    -a,--narrative: use narrative in query\n");
    fprintf(output, "    --print-queries: print topic queries to stderr\n");
    fprintf(output, "    --timing: print total querying time to stderr\n");
    fprintf(output, "              (excludes index loading time)\n");
    fprintf(output, "    --big-and-fast: use more memory\n");
    fprintf(output, "    --dummy: insert dummy results for topics with no "
      "results\n");
    fprintf(output, "    --non-stop: don't halt on empty topics\n");
    fprintf(output, "    --query-stop=[filename]: use filename as stoplist\n");
    fprintf(output, 
      "                             (or use default if no file give)\n");
    fprintf(output, "    --qrels=[filename]: evaluate effectiveness using "
      "the given TREC qrels\n");
    fprintf(output, "    -h,--help: this message\n");
    fprintf(output, "    -v,--version: print version\n");

    fprintf(output, "\n");
    fprintf(output, "  query metric options:\n");
    fprintf(output, "    --anh-impact: evaluate using impact-ordered lists\n"
      "                  (must have specified --anh-impact while indexing)\n");
    fprintf(output, "    -o,--okapi: use Okapi BM25 metric\n");
    fprintf(output, "    -1,--k1=[float]: set Okapi BM25 k1 value\n");
    fprintf(output, "    -3,--k3=[float]: set Okapi BM25 k3 value\n");
    fprintf(output, "    -b,--b=[float]: set Okapi BM25 b value\n");
    fprintf(output, "    -p,--pivoted-cosine=[float]: use pivoted cosine "
      "metric, with given pivot\n");
    fprintf(output, "    -c,--cosine: use cosine metric\n");
    fprintf(output, "    --hawkapi=[float]: use Dave Hawking's metric, "
      "with alpha given\n");
    fprintf(output, "    --dirichlet=[uint]: use Dirichlet-smoothed LM "
      "metric, with mu given\n");
    return;
}

/* struct to hold arguments */
struct args {
    char **topic_file;                 /* topic files containing queries */
    unsigned int topic_files;          /* number of topic files */
    struct index *idx;                 /* index to query */
    char *run_id;                      /* run_id to output */
    unsigned int numresults;           /* number of results per topic */
    unsigned int numaccumulators;      /* accumulator limit for eval */
    int title;                         /* whether to use title */
    int descr;                         /* whether to use description */
    int narr;                          /* whether to use narrative */
    enum index_search_opts sopts;      /* search options */
    struct index_search_opt sopt;      /* search options structure */
    int lopts;                         /* index load options */
    struct index_load_opt lopt;        /* index load options structure */
    int print_queries;                 /* whether to print out queries */
    int timing;                        /* whether to time only */
    struct treceval_qrels *qrels;      /* qrels structure if we want results 
                                        * evaluated */
    unsigned int memory;               /* amount of memory to use */
    int phrase;                        /* whether to run as phrases */
    unsigned int sloppiness;           /* sloppiness of phrases to run */
    unsigned int cutoff;               /* phrase must occur within this many 
                                        * words */
    int dummy;                         /* whether to print dummy results */
    int cont;                          /* whether to continue evaluation 
                                        * through errors */
    char *stoplist;                    /* stop file */
};

static void free_args(struct args *args) {
    unsigned int i;

    if (args->topic_file) {
        for (i = 0; i < args->topic_files; i++) {
            if (args->topic_file[i]) {
                free(args->topic_file[i]);
                args->topic_file[i] = NULL;
            }
        }
        free(args->topic_file);
    }

    if (args->stoplist) {
        free(args->stoplist);
    }

    if (args->idx) {
        index_delete(args->idx);
        args->idx = NULL;
    }

    if (args->run_id) {
        free(args->run_id);
        args->run_id = NULL;
    }

    if (args->qrels) {
        treceval_qrels_delete(&args->qrels);
    }

    free(args);
    return;
}

/* internal function to add a topic file to the args */
static int add_topic_file(struct args *args, const char *file) {
    void *ptr;
    struct stat statbuf;

    if ((stat((const char *) file, &statbuf) == 0)
      && (ptr 
        = realloc(args->topic_file, sizeof(char*) * (args->topic_files + 1)))
      && (args->topic_file = ptr) 
      && (args->topic_file[args->topic_files] = str_dup(file))) {

        args->topic_files++;
        return 1;
    } else {
        return 0;
    }
}

/* internal function to add a file of topic files to the args */
static int add_topic_file_file(struct args *args, const char *file) {
    char filenamebuf[FILENAME_MAX + 1];
    char format[50];
    FILE *fp;

    snprintf(format, 50, "%%%ds", FILENAME_MAX);

    if ((fp = fopen((const char *) file, "rb"))) {
        while (fscanf(fp, format, filenamebuf) && !feof(fp) && !ferror(fp)) {
            filenamebuf[FILENAME_MAX] = '\0';
            if (!add_topic_file(args, filenamebuf)) {
                return 0;
            }
        }
        fclose(fp);
        return 1;
    }

    return 0;
}

/* internal function to load an index from a parameters file */
static struct index *load_index(const char *prefix, unsigned int memory,
  int lopts, struct index_load_opt * lopt) {
    char filename[FILENAME_MAX + 1];
    struct index *idx;

    if ((idx = index_load((const char *) prefix, memory, lopts, lopt)) 
      || ((snprintf((char *) filename, FILENAME_MAX, "%s.%s", prefix, 
          INDSUF))
        && (idx = index_load((const char *) filename, memory, INDEX_LOAD_NOOPT, 
            NULL)))) {

        return idx;
    } else {
        return NULL;
    }
}

enum {
    OPT_FILE, OPT_FILELIST, OPT_NUMRESULTS, OPT_RUNID, OPT_PRINTQUERIES,
    OPT_OKAPI, OPT_DIRICHLET, OPT_HAWKAPI,
    OPT_K1, OPT_K3, OPT_B, OPT_PIVOTED_COSINE,
    OPT_COSINE, OPT_TITLE, OPT_DESCRIPTION, OPT_NARRATIVE, OPT_HELP,
    OPT_VERSION, OPT_QRELS, OPT_TIMING, OPT_ACCUMULATOR_LIMIT,
    OPT_IGNORE_VERSION, OPT_MEMORY, OPT_ANH_IMPACT, OPT_PHRASE, OPT_DUMMY, 
    OPT_CUTOFF, OPT_PARSEBUF, OPT_TABLESIZE, OPT_BIG_AND_FAST, OPT_NONSTOP,
    OPT_STOP
};

static struct args *parse_args(unsigned int argc, char **argv, FILE *output) {
    struct args *args;
    int err = 0,
        quiet = 0,
        id;
    long int num;                /* temporary space for numeric conversion */
    char *tmp;                   /* temporary space */
    unsigned int ind;
    const char *arg;
    struct getlongopt *parser;
    enum getlongopt_ret ret = GETLONGOPT_OK;

    struct getlongopt_opt opts[] = {
        {"file", 'f', GETLONGOPT_ARG_REQUIRED, OPT_FILE},
        {"file-list", 'F', GETLONGOPT_ARG_REQUIRED, OPT_FILELIST},
        {"number-results", 'n', GETLONGOPT_ARG_REQUIRED, OPT_NUMRESULTS},
        {"runid", 'r', GETLONGOPT_ARG_REQUIRED, OPT_RUNID},
        {"print-queries", '\0', GETLONGOPT_ARG_NONE, OPT_PRINTQUERIES},
        {"timing", '\0', GETLONGOPT_ARG_NONE, OPT_TIMING},

        {"okapi", 'o', GETLONGOPT_ARG_NONE, OPT_OKAPI},
        {"dirichlet", '\0', GETLONGOPT_ARG_REQUIRED, OPT_DIRICHLET},
        {"hawkapi", '\0', GETLONGOPT_ARG_REQUIRED, OPT_HAWKAPI},
        {"k1", '1', GETLONGOPT_ARG_REQUIRED, OPT_K1},
        {"k3", '3', GETLONGOPT_ARG_REQUIRED, OPT_K3},
        {"b", 'b', GETLONGOPT_ARG_REQUIRED, OPT_B},
        {"pivoted-cosine", 'p', GETLONGOPT_ARG_REQUIRED, OPT_PIVOTED_COSINE},
        {"cosine", 'c', GETLONGOPT_ARG_NONE, OPT_COSINE},
        {"anh-impact", '\0', GETLONGOPT_ARG_NONE, OPT_ANH_IMPACT},

        {"title", 't', GETLONGOPT_ARG_NONE, OPT_TITLE},
        {"description", 'd', GETLONGOPT_ARG_NONE, OPT_DESCRIPTION},
        {"narrative", 'a', GETLONGOPT_ARG_NONE, OPT_NARRATIVE},
        {"qrels", 'q', GETLONGOPT_ARG_REQUIRED, OPT_QRELS},
        {"accumulator-limit", 'A', GETLONGOPT_ARG_REQUIRED, 
            OPT_ACCUMULATOR_LIMIT},
        {"ignore-version", '\0', GETLONGOPT_ARG_NONE, OPT_IGNORE_VERSION},

        {"help", 'h', GETLONGOPT_ARG_NONE, OPT_HELP},
        {"version", 'v', GETLONGOPT_ARG_NONE, OPT_VERSION},
        {"memory", 'm', GETLONGOPT_ARG_REQUIRED, OPT_MEMORY},
        {"parse-buffer", 'm', GETLONGOPT_ARG_REQUIRED, OPT_PARSEBUF},
        {"tablesize", 'm', GETLONGOPT_ARG_REQUIRED, OPT_TABLESIZE},
        {"big-and-fast", 'm', GETLONGOPT_ARG_NONE, OPT_BIG_AND_FAST},
        {"phrase", '\0', GETLONGOPT_ARG_OPTIONAL, OPT_PHRASE},
        {"term-cutoff", '\0', GETLONGOPT_ARG_REQUIRED, OPT_CUTOFF},
        {"dummy", '\0', GETLONGOPT_ARG_NONE, OPT_DUMMY},
        {"non-stop", '\0', GETLONGOPT_ARG_NONE, OPT_NONSTOP},
        {"query-stop", '\0', GETLONGOPT_ARG_OPTIONAL, OPT_STOP},
        {NULL, 'V', GETLONGOPT_ARG_NONE, OPT_VERSION}
    };

    if ((args = malloc(sizeof(*args))) 
      && (parser = getlongopt_new(argc - 1, (const char **) &argv[1], opts, 
          sizeof(opts) / sizeof(*opts)))) {
        /* succeeded, do nothing */
    } else {
        if (args) {
            free(args);
        }
        fprintf(output, "failed to initialise option parsing\n");
        return NULL;
    }

    args->stoplist = NULL;
    args->topic_files = 0;
    args->topic_file = NULL;
    args->run_id = NULL;
    args->idx = NULL;
    args->numresults = 0;
    args->numaccumulators = 0;
    args->sopts = INDEX_SEARCH_NOOPT;
    args->lopts = INDEX_LOAD_NOOPT;
    args->title = 0;
    args->descr = 0;
    args->narr = 0;
    args->qrels = NULL;
    args->sopt.u.okapi_k3.k1 = 1.2F;
    args->sopt.u.okapi_k3.k3 = 1e10;
    args->sopt.u.okapi_k3.b = 0.75;
    args->lopt.docmap_cache = DOCMAP_CACHE_TRECNO;
    args->print_queries = args->timing = 0;
    args->memory = MEMORY_DEFAULT;
    args->phrase = 0;
    args->cutoff = 0;
    args->dummy = 0;
    args->cont = 0;

    /* parse arguments */
    while (!err 
      && ((ret = getlongopt(parser, &id, &arg)) == GETLONGOPT_OK)) {
        switch (id) {
        case OPT_RUNID:
            /* set run_id */
            if (!args->run_id) {
                if (!(args->run_id = str_dup(arg))) {
                    fprintf(output, "couldn't set run_id '%s': %s\n", arg, 
                      strerror(errno));
                    err = quiet = 1;
                }
            } else if (args->run_id) {
                fprintf(output, "run_id already set to '%s'\n", args->run_id);
                err = 1;
                quiet = 0;
            }
            break;

        case OPT_STOP:
            if (args->stoplist) {
                err = 1;
                fprintf(output, "query stoplist specified multiple times\n");
                quiet = 0;
            } else if (!arg || (args->stoplist = str_dup(arg))) {
                args->lopts |= INDEX_LOAD_QSTOP;
                args->lopt.qstop_file = args->stoplist;
            } else {
                err = quiet = 1;
                fprintf(output, "can't copy query stoplist name\n");
            } 
            break;

        case OPT_MEMORY:
            /* set memory */
            args->memory = atoi(arg);
            break;

        case OPT_PARSEBUF:
            errno = 0;
            num = strtol(arg, &tmp, 10);
            if (!errno && (num > 0) && (num <= UINT_MAX) && !*tmp) {
                args->lopts |= INDEX_LOAD_PARSEBUF;
                args->lopt.parsebuf = num;
            } else {
                fprintf(output, "error converting parsebuf value '%s'\n", 
                  arg);
                err = 1;
            }
            break;

        case OPT_TABLESIZE:
            errno = 0;
            num = strtol(arg, &tmp, 10);
            if (!errno && (num > 0) && (num <= UINT_MAX) && !*tmp) {
                args->lopts |= INDEX_LOAD_TABLESIZE;
                args->lopt.tablesize = num;
            } else {
                fprintf(output, "error converting tablesize value '%s'\n", 
                  arg);
                err = 1;
            }
            break;

        case OPT_BIG_AND_FAST:
            /* use big memory options */
            if (args->memory == MEMORY_DEFAULT) {
                args->memory = BIG_MEMORY_DEFAULT;
            }
            if (!(args->lopts & INDEX_NEW_PARSEBUF)) {
                args->lopts |= INDEX_LOAD_PARSEBUF;
                args->lopt.parsebuf = BIG_PARSE_BUFFER;
            }
            if (!(args->lopts & INDEX_NEW_TABLESIZE)) {
                args->lopts |= INDEX_LOAD_TABLESIZE;
                args->lopt.tablesize = BIG_TABLESIZE;
            }
            break;

        case OPT_QRELS:
            /* they want it evaluated against some qrels */
            if (!args->qrels) {
                if ((args->qrels = treceval_qrels_new(arg))) {
                    /* succeeded, do nothing */
                } else {
                    fprintf(output, "failed to load qrels from '%s'\n", arg);
                    err = 1;
                }
            } else {
                fprintf(output, "qrels specified multiple times\n");
                err = 1;
            }
            break;

        case OPT_IGNORE_VERSION:
            args->lopts |= INDEX_LOAD_IGNORE_VERSION;
            break;

        case OPT_ACCUMULATOR_LIMIT:
            /* set number of accumulators to use. */
            errno = 0;
            num = strtol(arg, &tmp, 10);
            if (!errno && !*tmp) {
                args->sopt.accumulator_limit = num;
                args->sopts |= INDEX_SEARCH_ACCUMULATOR_LIMIT;
            } else {
                fprintf(output, 
                  "error converting accumulator limit value '%s'\n", arg);
                err = 1;
            }
            break;

        case OPT_NONSTOP:
            args->cont = 1;
            break;

        case OPT_DUMMY:
            args->dummy = 1;
            break;

        case OPT_CUTOFF:
            args->cutoff = atoi(arg);
            break;

        case OPT_PHRASE:
            /* they want to run it as a phrase, optionally sloppy */
            args->phrase = 1;
            if (arg) {
                char *end;
                long int tmpnum = strtol(arg, &end, 0);

                if (!*end && (tmpnum <= UINT_MAX)) {
                    args->sloppiness = tmpnum;
                } else {
                    fprintf(output, "couldn't convert '%s' to number\n", 
                      arg);
                }
            } else {
                args->sloppiness = 0;
            }
            break;

        case OPT_NUMRESULTS:
            /* set number of results per topic */
            if (!args->numresults) {
                char *end;
                long int tmpnum = strtol(arg, &end, 0);

                if (!*end && (tmpnum <= UINT_MAX)) {
                    args->numresults = tmpnum;
                } else {
                    fprintf(output, "couldn't convert '%s' to number\n", 
                      arg);
                }
            } else {
                fprintf(output, "number of results is already set to %u\n", 
                  args->numresults);
                err = 1;
            }
            break;

        case OPT_FILE:
            /* add topic file */
            if (!(add_topic_file(args, arg))) {
                fprintf(output, "couldn't add file '%s': %s\n", arg, 
                  strerror(errno));
                err = quiet = 1;
            }
            break;

        case OPT_FILELIST:
            /* add topic file file */
            if (!(add_topic_file_file(args, arg))) {
                fprintf(output, "couldn't add list '%s': %s\n", arg, 
                  strerror(errno));
                err = quiet = 1;
            }
            break;

        case OPT_HELP:
            /* they want help */
            err = 1;
            output = stdout;
            break;

        case OPT_VERSION:
            /* they want version info */
            printf("version %s\n", PACKAGE_VERSION);
            err = quiet = 1;
            output = stdout;
            break;

        case OPT_ANH_IMPACT:
            args->sopts |= INDEX_SEARCH_ANH_IMPACT_RANK;
            break;

        case OPT_HAWKAPI:
            /* they want to use hawkapi */
            args->sopts |= INDEX_SEARCH_HAWKAPI_RANK;
            if (!sscanf(arg, "%f", &args->sopt.u.hawkapi.alpha)) {
                fprintf(stderr, "failed to read alpha parameter\n");
                err = 1;
            }
            break;

        case OPT_DIRICHLET:
            /* they want to use dirichlet */
            args->sopts |= INDEX_SEARCH_DIRICHLET_RANK;
            if (!sscanf(arg, "%f", &args->sopt.u.dirichlet.mu)) {
                fprintf(stderr, "failed to read mu parameter\n");
                err = 1;
            }
            break;

        case OPT_OKAPI:
            /* they want to use okapi */
            args->sopts |= INDEX_SEARCH_OKAPI_RANK;
            break;

        case OPT_K1:
            /* set okapi k1 parameter */
            if (sscanf(arg, "%f", &args->sopt.u.okapi_k3.k1)) {
                /* do nothing */
            } else {
                fprintf(output, "can't read k1 value '%s'\n", arg);
                err = 1;
            }
            break;

        case OPT_K3:
            /* set okapi k3 parameter */
            if (sscanf(arg, "%f", &args->sopt.u.okapi_k3.k3)) {
                /* do nothing */
            } else {
                fprintf(output, "can't read k3 value '%s'\n", arg);
                err = 1;
            }
            break;

        case OPT_B:
            /* set okapi b parameter */
            if (sscanf(arg, "%f", &args->sopt.u.okapi_k3.b)) {
                /* do nothing */
            } else {
                fprintf(output, "can't read b value '%s'\n", arg);
                err = 1;
            }
            break;

        case OPT_TIMING:
            /* print queries */
            args->timing = 1;
            break;

        case OPT_PRINTQUERIES:
            /* print queries */
            args->print_queries = 1;
            break;

        case OPT_TITLE:
            /* use title in query */
            args->title = 1;
            break;

        case OPT_DESCRIPTION:
            /* use description in query */
            args->descr = 1;
            break;

        case OPT_NARRATIVE:
            /* use narrative in query */
            args->narr = 1;
            break;

        case OPT_PIVOTED_COSINE:
            /* they want to use pivoted cosine */
            if (sscanf(arg, "%f", &args->sopt.u.pcosine.pivot)) {
                args->sopts |= INDEX_SEARCH_PCOSINE_RANK;

                /* arrange for weights to be loaded into memory */
                args->lopts |= INDEX_LOAD_DOCMAP_CACHE;
                args->lopt.docmap_cache |= DOCMAP_CACHE_WEIGHT;

                if (args->sopt.u.pcosine.pivot < 0.0) {
                    fprintf(output, "cosine pivot can't be negative\n");
                    err = 1;
                }
            } else {
                fprintf(output, "can't read pivot value '%s'\n", arg);
                err = 1;
            }
            break;

        case OPT_COSINE:
            /* they want to use the cosine metric */
            args->sopts |= INDEX_SEARCH_COSINE_RANK;

            /* arrange for weights to be loaded into memory */
            args->lopts |= INDEX_LOAD_DOCMAP_CACHE;
            args->lopt.docmap_cache |= DOCMAP_CACHE_WEIGHT;
            break;

        default:
            /* shouldn't happen */
            assert(0);
        }
    }

    ind = getlongopt_optind(parser) + 1;
    getlongopt_delete(parser);

    if (err || ret == GETLONGOPT_END) {
        /* succeeded, do nothing */
    } else {
        if (ret == GETLONGOPT_UNKNOWN) {
            fprintf(stderr, "unknown option '%s'\n", argv[ind]);
        } else if (ret == GETLONGOPT_MISSING_ARG) {
            fprintf(stderr, "missing argument to option '%s'\n", argv[ind]);
        } else {
            fprintf(stderr, "unexpected error parsing options (around '%s')\n",
              argv[ind]);
        }
        free(args);
        return NULL;
    }
        
    /* remaining arguments should be index */
    while (!err && (ind < (unsigned int) argc)) {
        if (!args->idx) {
            if (!(args->idx = load_index(argv[ind], args->memory,
                      args->lopts, &args->lopt))) {

                fprintf(output, "couldn't load index from '%s': %s\n", 
                  argv[ind], strerror(errno));
                err = quiet = 1;
            }
        } else {
            fprintf(output, "index already loaded\n");
            err = 1;
            quiet = 0;
        }
        ind++;
    }

    /* set run_id if they haven't */
    if (!err && !args->run_id 
      && !(args->run_id = str_dup(PACKAGE))) {
        fprintf(output, "couldn't set default run_id: %s\n", strerror(errno));
        err = quiet = 1;
    }

    /* set number of results if they haven't */
    if (!err && !args->numresults) {
        args->numresults = 1000;
    }

    /* set number of accumulators if they haven't */
    if (!err && !args->numaccumulators) {
        args->numaccumulators = ACCUMULATOR_LIMIT;
    }

    /* check that they've given a topic file */
    if (!err && !args->topic_files) {
        fprintf(output, "no topic files given\n");
        err = 1;
    }

    /* check that they've given an index */
    if (!err && !args->idx) {
        fprintf(output, "no index given\n");
        err = 1;
    }

    if (!(args->title || args->descr || args->narr)) {
        /* title only run is default */
        args->title = 1;
    }

    if (err) {
        if (!quiet) {
            fprintf(output, "\n");
            print_usage(output, *argv);
        }
        free_args(args);
        return NULL;
    } else {
        return args;
    }
}

/* small function to append a string to a buffer */
static char *strappend(char **buf, unsigned int *len, unsigned int *cap, 
  char *str, unsigned int strlen) {

    while (*cap <= *len + strlen) {
        void *ptr = realloc(*buf, *cap * 2 + 1);

        if (ptr) {
            *buf = ptr;
            *cap = *cap * 2 + 1;
        } else {
            return NULL;
        }
    }

    memcpy(*buf + *len, str, strlen);
    *len += strlen;
    return *buf;
}

/* small macro to append a string to the buffer, exiting with an error message
 * on failure */
#define APPEND(buf, buflen, bufcap, str, strlen)                              \
    if (!strappend(&buf, &buflen, &bufcap, str, strlen)) {                    \
        if (buf) {                                                            \
            free(buf);                                                        \
        }                                                                     \
        fprintf(stderr, "error allocating memory for query, length %u + %u\n",\
          buflen, strlen);                                                    \
        return NULL;                                                          \
    } else 

/* internal function to parse the topic file and extract a query and
 * querynum from it */
static char *get_next_query(struct mlparse_wrap *parser, char *querynum, 
  unsigned int querynum_len, int title, int descr, int narr, 
  unsigned int *words, struct args *args) {
    int innum = 0,                       /* parsing the topic number */
        intitle = 0,                     /* whether we're parsing the title */
        indescr = 0,                     /* whether we're parsing description */
        innarr = 0,                      /* whether we're parsing narrative */
        firstword = 0;                   /* whether its the first word of a 
                                          * field */
    enum mlparse_ret ret;                /* return value from parser */
    char *buf = NULL,                    /* query buffer */
         word[TERMLEN_MAX + 1];          /* last word parsed */
    unsigned int buflen = 0,             /* length of the query */
                 bufcapacity = 0,        /* capacity of the query buffer */
                 wordlen;                /* length of the last word parsed */

    *words = 0;

    do {
        ret = mlparse_wrap_parse(parser, word, &wordlen, 0);
    } while (ret != MLPARSE_TAG && ret != MLPARSE_ERR && ret != MLPARSE_EOF);

    /* check that we got a 'top' tag */
    if (ret == MLPARSE_TAG && str_casecmp(word, "top")) {
        fprintf(stderr, "expected to parse 'top' tag from topic file\n");
        return NULL;
    } else if (ret == MLPARSE_ERR) {
        fprintf(stderr, "error parsing topic file\n");
        return NULL;
    } else if (ret == MLPARSE_EOF) {
        return NULL;
    } else {
        assert(ret == MLPARSE_TAG);
    }

    *querynum = 0;

    /* set up phrase if requested by user */
    if (args->phrase) {
        APPEND(buf, buflen, bufcapacity, "\"", 1);
    }

    /* parse the rest of the top entry */
    while ((ret = mlparse_wrap_parse(parser, word, &wordlen, 0)) 
      != MLPARSE_ERR) {
        switch (ret) {
        case MLPARSE_TAG:
            if (!str_casecmp(word, "num")) {
                /* query number */
                innum = 1;
                intitle = 0;
                indescr = 0;
                innarr = 0;
                firstword = 1;
            } else if (!str_casecmp(word, "title")) {
                /* query title */
                if (title) {
                    intitle = 1;
                } else {
                    intitle = 0;
                }
                indescr = 0;
                innarr = 0;
                innum = 0;
                firstword = 1;
            } else if (!str_casecmp(word, "desc")) {
                /* query description */
                if (descr) {
                    indescr = 1;
                } else {
                    indescr = 0;
                }
                intitle = 0;
                innarr = 0;
                innum = 0;
                firstword = 1;
            } else if (!str_casecmp(word, "narr")) {
                /* query narrative */
                if (narr) {
                    innarr = 1;
                } else {
                    innarr = 0;
                }
                indescr = 0;
                intitle = 0;
                innum = 0;
                firstword = 1;
            } else if (!str_casecmp(word, "/top")) {
                /* finished this topic, return the query */
                if (*querynum) {
                    if (buf) {
                        /* optionally terminate phrase */
                        if (args->phrase) {
#define TMPBUFLEN 100
                            char tmpbuf[TMPBUFLEN + 1];
                            unsigned int n;

                            APPEND(buf, buflen, bufcapacity, "\"", 1);
                            if (args->sloppiness) {

                                n = snprintf(tmpbuf, TMPBUFLEN, " [sloppy:%u] ",
                                    args->sloppiness);
                                assert(n < TMPBUFLEN);
                                APPEND(buf, buflen, bufcapacity, tmpbuf, n);

                                /* ensure sloppy directive is processed */
                                (*words)++; 
                            }
                            if (args->cutoff) {
                                n = snprintf(tmpbuf, TMPBUFLEN, " [cutoff:%u] ",
                                    args->cutoff);
                                assert(n < TMPBUFLEN);
                                APPEND(buf, buflen, bufcapacity, tmpbuf, n);

                                /* ensure cutoff directive is processed */
                                (*words)++; 
                            }
#undef TMPBUFLEN
                        }

                        /* terminate string and return it */
                        APPEND(buf, buflen, bufcapacity, "\0", 1);
                        return buf;
                    } else {
                        APPEND(buf, buflen, bufcapacity, "\0", 1);
                        return buf;
                    }
                } else {
                    fprintf(stderr, 
                      "didn't find topic number in topic file\n");
                    return NULL;
                }
            } else {
                /* unexpected tag, ignore it */
                innum = 0;
                innarr = 0;
                indescr = 0;
                intitle = 0;
            }
            break;

        case MLPARSE_EOF:
            if (*querynum || buf) {
                fprintf(stderr, "unexpected eof while parsing\n");
            }
            return NULL;

        /* ignore this stuff */
        case MLPARSE_PARAM:
        case MLPARSE_CONT | MLPARSE_PARAM:
        case MLPARSE_PARAMVAL:
        case MLPARSE_CONT | MLPARSE_PARAMVAL:
        case MLPARSE_CDATA:
        case MLPARSE_END | MLPARSE_CDATA:
        case MLPARSE_WHITESPACE:
            break;

        /* XXX: comments shouldn't just be ignored (as control flow, the stuff
         * inside comments should be ignored) */
        case MLPARSE_COMMENT | MLPARSE_END:
        case MLPARSE_COMMENT:
            break;

        case MLPARSE_WORD:
        case MLPARSE_END | MLPARSE_WORD:
            /* include everything in tags indicated by arguments, except
             * that TREC topic files often start the topic with 'Topic:',
             * the description with 'Description:' and narrative with
             * 'Narrative:' :o( */
            if ((intitle && (!firstword 
                || str_nncmp(word, wordlen, "Topic:", str_len("Topic:"))))
              || (indescr && (!firstword 
                  || str_nncmp(word, wordlen, "Description:", 
                      str_len("Description:"))))
              || (innarr && (!firstword 
                  || str_nncmp(word, wordlen, "Narrative:", 
                      str_len("Narrative:"))))) {

                /* we have to add the word to the buffer */
                (*words)++;
                APPEND(buf, buflen, bufcapacity, word, wordlen);
                APPEND(buf, buflen, bufcapacity, " ", 1);
            } else if (innum 
              && (!firstword 
                || str_nncmp(word, wordlen, "Number:", str_len("Number:")))) {

                if (wordlen < querynum_len) {
                    str_ncpy(querynum, word, wordlen);
                    querynum[wordlen] = '\0';
                    str_toupper(querynum);
                } else {
                    fprintf(stderr, "querynum '%s' too long\n", word);
                    return NULL;
                } 
            }
            firstword = 0;
            break;

        case MLPARSE_CONT | MLPARSE_WORD:
        case MLPARSE_CONT | MLPARSE_TAG:
            /* ignore (XXX: should also ignore subsequent word as well) */
            break;

        default:
            /* not expecting anything else */
            if (buf) {
                free(buf);
            }
            fprintf(stderr, "error parsing topic file\n");
            return NULL;
            break;
        }
    }

    /* must have received an error */
    if (buf) {
        free(buf);
    }
    fprintf(stderr, "error parsing topic file\n");
    return NULL;
}

/* internal function to execute queries from a topic file against an
 * index and output the results in trec_eval format */
static int process_topic_file(FILE *fp, struct args *args, FILE *output, 
  struct treceval *teresults) {
    struct index_stats stats;
    char *query,
         *querynum = NULL;
    struct index_result *results = NULL;
    unsigned int returned,
                 i;
    int est;
    double total_results;
    struct mlparse_wrap *parser;
    struct timeval now, then;
    struct timeval topic_now, topic_then;

    gettimeofday(&topic_then, NULL);

    if (!index_stats(args->idx, &stats)) {
        return 0;
    }

    if ((parser = mlparse_wrap_new_file(stats.maxtermlen, LOOKAHEAD, fp, 
        BUFSIZ, 0))
      && (results = malloc(sizeof(*results) * args->numresults))
      && (querynum = malloc(stats.maxtermlen + 2))) {

        while ((query = get_next_query(parser, 
            querynum, stats.maxtermlen + 1, args->title, args->descr, 
            args->narr, &args->sopt.word_limit, args))) {

            /* check that we actually got a query */
            if (!str_len(query)) {
                if (!args->cont) {
                    fprintf(stderr, "failed to extract query for topic %s\n", 
                      querynum);
                }
                if (atoi((const char *) querynum) == 201) {
                    fprintf(stderr, "looks like it occurred on TREC topics "
                      "201-250, which is probably because you specified a "
                      "title-only run and it doesn't contain titles\n");
                }

                if (args->cont && strlen(querynum)) {
                    /* continue evaluation */
                    if (args->dummy) {
                        /* no results, insert dummy result if requested */
                        fprintf(output, "%s\tQ0\t%s\t%u\t%f\t%s\n", querynum,
                          "XXXX-XXX-XXXXXXX", 1, 0.0, args->run_id);
                    }
                } else {
                    /* give up evaluation */
                    free(query);
                    free(results);
                    free(querynum);
                    mlparse_wrap_delete(parser);
                    return 0;
                }
            }
 
            args->sopt.summary_type = INDEX_SUMMARISE_NONE;

            /* FIXME: detect errors */

            gettimeofday(&then, NULL);
            if (index_search(args->idx, (const char *) query, 0, 
                args->numresults, results, &returned, 
                &total_results, &est, (args->sopts | INDEX_SEARCH_WORD_LIMIT | 
                INDEX_SEARCH_SUMMARY_TYPE), &args->sopt)) {
                char aux_buf[512];

                gettimeofday(&now, NULL);

                if (args->print_queries) {
                    fprintf(stderr, 
                      "query '%s' completed in %lu microseconds\n", query,
                      (unsigned long int) now.tv_usec - then.tv_usec 
                        + (now.tv_sec - then.tv_sec) * 1000000);
                }

                /* print results */
                for (i = 0; i < returned; i++) {
                    char *docno = NULL;

                    /* hack: any document that doesn't have a TREC docno will
                     * inherit the docno of the previous document (until we 
                     * find one that has a docno) */

                    if (results[i].auxilliary && results[i].auxilliary[0]) {
                        strncpy(aux_buf, results[i].auxilliary, 
                          sizeof(aux_buf));
                        docno = aux_buf;
                    } else {
                        int ret = 1;
                        unsigned long int docnum = results[i].docno;
                        unsigned int aux_len = 0;

                        /* retrieve TREC docno for progressively higher docs */
                        while (ret && (docnum > 0) && !aux_len) {
                            docnum--;
                            ret = index_retrieve_doc_aux(args->idx, docnum,
                              aux_buf, sizeof(aux_buf) - 1, &aux_len);
                            /* XXX assume trec docno always < 512 bytes long */
                        }
                        if (ret && aux_len) {
                            docno = aux_buf;
                        }
                    }
                    aux_buf[sizeof(aux_buf) - 1] = '\0';

                    if (teresults 
                      && docno
                      && treceval_add_result(teresults, atoi(querynum), 
                          docno, (float) results[i].score)) {
                        /* they want evaluated results, stuck it in the results
                         * structure */
                    } else if (!teresults && docno) {
                        str_toupper(docno);

                        /* print out query_id, iter (ignored - so we print
                         * out number of seconds taken), docno, rank
                         * (ignored), score, run_id */
                        fprintf(output, "%s\tQ0\t%s\t%u\t%f\t%s\n", querynum, 
                          docno, i + 1, results[i].score, 
                          args->run_id);
                    } else if (teresults && docno) {
                        fprintf(stderr, "failed to add to treceval results\n");
                        free(results);
                        free(querynum);
                        free(docno);
                        free(query);
                        mlparse_wrap_delete(parser);
                        return 0;
                    } else {
                        /* couldn't copy the docno */
                        fprintf(stderr, "docno ('%s') copy failed: %s\n", 
                          results[i].auxilliary, strerror(errno));
                        free(query);
                        free(results);
                        free(querynum);
                        mlparse_wrap_delete(parser);
                        return 0;
                    }
                }

                if (returned == 0 && args->dummy) {
                    /* no results, insert dummy result if requested */
                    fprintf(output, "%s\tQ0\t%s\t%u\t%f\t%s\n", querynum,    
                      "XXXX-XXX-XXXXXXX", 1, 0.0, args->run_id);
                }
            } else {
                /* error searching */
                fprintf(stderr, "error searching index\n");
                free(query);
                mlparse_wrap_delete(parser);
                return 0;
            }

            free(query);
            querynum[0] = '\0';
        }

        if (!feof(fp)) {
            fprintf(stderr, "parser or read error\n");
            free(results);
            free(querynum);
            mlparse_wrap_delete(parser);
            return 0;
        }
        free(results);
        free(querynum);

        gettimeofday(&topic_now, NULL);

        if (args->timing) {
            fprintf(stderr, "topic processed in %lu microseconds\n", 
              (unsigned long int) topic_now.tv_usec - topic_then.tv_usec 
                + (topic_now.tv_sec - topic_then.tv_sec) * 1000000);
        }

        mlparse_wrap_delete(parser);
        return 1;
    } else {
        if (parser) {
            if (results) {
                free(results);
            }
            mlparse_wrap_delete(parser);
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    struct args *args;
    unsigned int i;
    FILE *fp,
         *output;
    struct treceval *results = NULL;

    if (isatty(STDOUT_FILENO)) {
        output = stdout;
    } else {
        output = stderr;
    }

    args = parse_args(argc, argv, output);

    if (args == NULL) {
        return EXIT_FAILURE;
    } 

    if (args->qrels) {
        if (!(results = treceval_new())) {
            fprintf(stderr, "failed to initialise results structure\n");
        }
    }

    if (args) {
        for (i = 0; i < args->topic_files; i++) {
            if ((fp = fopen((const char *) args->topic_file[i], "rb"))) {
                if (!process_topic_file(fp, args, stdout, results)) {
                    if (results) {
                        treceval_delete(&results);
                    }
                    fprintf(output, "failed to process topic file '%s'\n", 
                      args->topic_file[i]);
                    fclose(fp);
                    free_args(args);
                    return EXIT_FAILURE;
                }
                fclose(fp);
            } else {
                if (results) {
                    treceval_delete(&results);
                }
                fprintf(output, "couldn't open topic file '%s': %s\n", 
                  args->topic_file[i], strerror(errno));
                free_args(args);
                return EXIT_FAILURE;
            }
        }

        if (results) {
            struct treceval_results eval;

            treceval_evaluate(results, args->qrels, &eval);

            treceval_print_results(&eval, 1, stdout, 0);

            treceval_delete(&results);
        }

        free_args(args);
    }

    return EXIT_SUCCESS;
}

