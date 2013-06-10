/* commandline.c is the new command-line interface for the search engine
 *
 * written nml 2003-07-21
 *
 */

#include "firstinclude.h"

#include "config.h"
#include "def.h"
#include "docmap.h"         /* needed to set cache values on index load */
#include "getlongopt.h"
#include "index.h"
#include "str.h"
#include "queryparse.h"
#include "timings.h"
#include "index_querybuild.h"
#include "summarise.h"
#include "error.h"
#include "signals.h"
#include "svnversion.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

void print_usage(const char *progname, FILE *output, int verbose) {
    const char *name = str_rchr(progname, '/');

    if (name) {
        name++;
    } else {
        name = progname;
    }
    fprintf(output, "usage to query: '%s'\n", name);
    fprintf(output, "  query options:\n");
    fprintf(output, "    -f,--filename: specify index to load (default "
      "'index')\n");
    fprintf(output, "    -n,--number-results: provide this many results per "
      "query (default: 20)\n");
    fprintf(output, "    -b,--begin-results: provide results after this "
      "offset (default 0)\n");
    fprintf(output, "    --summary=[value]: create textual summary of this "
      "type\n"
      "                       (where value is capitalise, plain, or tag)\n");
    fprintf(output, "    --query-list=[file]: read queries from this file\n");
    fprintf(output, "    --query-stop=[file]: stop queries according to the "
      "contents of this file\n");
    fprintf(output, 
      "                         (or use default if no file give)\n");
    fprintf(output, "    --big-and-fast: use more memory\n");
    fprintf(output, "    -s,--stats: get index statistics\n");
    fprintf(output, "    -v,--version: print version number\n");
    fprintf(output, "    -h,--help: print this message\n");

    fprintf(output, "\n");
    fprintf(output, "  query metric options:\n");
    fprintf(output, "    --anh-impact: evaluate using impact-ordered lists\n"
      "                  (must have specified --anh-impact while indexing)\n");
    fprintf(output, "    --okapi: use Okapi BM25 metric\n");
    fprintf(output, "    --k1=[float]: set Okapi BM25 k1 value\n");
    fprintf(output, "    --k3=[float]: set Okapi BM25 k3 value\n");
    fprintf(output, "    --b=[float]: set Okapi BM25 b value\n");
    fprintf(output, "    --pivoted-cosine=[float]: use pivoted cosine "
      "metric, with given pivot\n");
    fprintf(output, "    --cosine: use cosine metric\n");
    fprintf(output, "    --hawkapi=[float]: use Dave Hawking's metric, "
      "with alpha given\n");
    fprintf(output, "    --dirichlet=[uint]: use Dirichlet-smoothed LM "
      "metric, with mu given\n");

    fprintf(output, "\n");
    fprintf(output, "usage to index: '%s -i file1 ... fileN'\n", 
      name);
    fprintf(output, "  indexing options:\n");
    fprintf(output, "    -f,--filename: name the created index "
      "(default 'index')\n");
    fprintf(output, "    --big-and-fast: use more memory\n");
    fprintf(output, "    --file-list=[file]: read files to index from this "
      "file\n");
    fprintf(output, "    --stem=[value]: change stemming algorithm\n"
      "                    (value is one of none, eds, light, porters)\n"
      "                    (default is light)\n");
    fprintf(output, "    --add: add indexed files to an existing index\n");
    fprintf(output, "    --anh-impact: generate impact-ordered lists\n");

    return;
}

/* struct to hold arguments */
struct args {
    int stat;
    int index;
    int index_add;
    int index_add_stats;
    char *prefix;
    char **list;
    FILE *qlist;

    const char *type;
    char *config_file;
    unsigned int memory;
    unsigned int results;
    unsigned int first_result;
    char * stop_file;
    char * qstop_file;

    int sopts;
    struct index_search_opt sopt;
    int aopts;
    struct index_add_opt aopt;
    int copts;
    struct index_commit_opt copt;
    int nopts;
    struct index_new_opt nopt;
    int lopts;
    struct index_load_opt lopt;
};

static void free_args(struct args *args) {
    unsigned int i = 0;

    if (args->prefix) {
        free(args->prefix);
    }
    if (args->list) {
        for (i = 0; args->list[i]; i++) {
            free(args->list[i]);
        }
        free(args->list);
    }
    if (args->config_file) {
        free(args->config_file);
    }
    if (args->stop_file) {
        free(args->stop_file);
    }
    if (args->qstop_file) {
        free(args->qstop_file);
    }
    if (args->type) {
        free((char *) args->type);
    }
    if (args->qlist && args->qlist != stdin) {
        fclose(args->qlist);
    }
    return;
}

/* internal function to qualify a given filename with the current working
 * directly if necessary */
static char *path_dup(const char *file, const char *path) {
    unsigned int len;
    char *tmp;

    /* FIXME: avoid unix-centric checking of file-name absolutism */
#ifdef WIN32
    if ((*file && (file[1] == ':') && (file[2] == '\\')) || !path) {
        return str_dup(file);
    }
#else
    if ((*file == '/') || !path) {
        return str_dup(file);
    }
#endif
    if ((len = str_len(file) + 1 + str_len(path) + 1) 
      && (tmp = malloc(len))) {
        unsigned int tmplen;

        str_lcpy(tmp, path, len);

        tmplen = str_len(tmp);
        tmp[tmplen] = OS_SEPARATOR;
        tmp[tmplen + 1] = '\0';

        str_lcat(tmp, file, len);
        return tmp;
    } else {
        return NULL;
    }
}

/* internal function to fill the args list from the given stream */
static int fill_args_list(struct args *args, FILE *input, const char *path) {
    unsigned int listcount;
    char buf[FILENAME_MAX + 1];

    if (!args->list) {
        if (!(args->list = malloc(sizeof(*args->list)))) {
            return 0;
        }
        args->list[0] = NULL;
    }

    /* count initial items */
    for (listcount = 0; args->list[listcount]; listcount++) ;

    /* add items */
    while (fgets(buf, FILENAME_MAX + 1, input)
      && !ferror(input)) {
        void *ptr;

        str_rtrim(buf);

        if (!(ptr 
          = realloc(args->list, sizeof(*args->list) * (listcount + 2)))) {
            return 0;
        }
        args->list = ptr;
        if (!(args->list[listcount] = path_dup(buf, path))) {
            return 0;
        }
        listcount++;
    }
    args->list[listcount] = NULL;

    return feof(input) && !ferror(input);
}

/* enumeration of option ids */
enum {
    OPT_INDEX, OPT_FILENAME, OPT_CONFIG, OPT_TYPE, OPT_NUMBER_RESULTS,
    OPT_BEGIN_RESULTS, OPT_VERSION, OPT_HELP, OPT_STATS, OPT_SUMMARY,
    OPT_MEMORY, OPT_ACCUMULATION_MEMORY, OPT_ACCUMULATION_DOCS, OPT_DUMP_MEMORY,
    OPT_DUMP_VECS, OPT_REBUILD, OPT_REMERGE, OPT_INPLACE, OPT_MAXFILESIZE, 
    OPT_FILELIST, OPT_ADD, OPT_ADD_STATS, OPT_OKAPI, OPT_K1, OPT_K3, OPT_B,
    OPT_PIVOTED_COSINE, OPT_COSINE, OPT_WORD_LIMIT, OPT_HAWKAPI, 
    OPT_SORT_ACCESSES, OPT_VOCAB_LISTSIZE, 
    OPT_FRAPPEND, OPT_MAXFLIST,
    OPT_STEM, OPT_BUILD_STOP, OPT_QUERY_STOP, OPT_ACCUMULATOR_LIMIT, 
    OPT_IGNORE_VERSION,
    OPT_DIRICHLET, OPT_ANH_IMPACT, 
    OPT_TABLESIZE, OPT_PARSEBUF, OPT_BIG_AND_FAST, OPT_QUERYLIST
};

static struct args *parse_args(unsigned int argc, char **argv, 
  struct args *args, FILE *output, const char *path) {
    int quiet = 0,               /* an internal error occurred */
        err = 0,                 /* whether an error has occurred */
        must_index = 0,          /* whether arguments have to be for build */
        must_search = 0,         /* whether arguments have to be for search */
        must_stat = 0,           /* whether arguments have to be for stat */
        verbose = 0,             /* whether to print a bunch of stuff out */
        metric = 0,              /* whether a metric has been set */
        stem = 0;                /* whether stemming has been specified */
    unsigned int listitems = 0;  /* number of items in the list */
    struct getlongopt *parser;   /* option parser */
    int id;                      /* id of parsed option */
    const char *arg;             /* argument of parsed option */
    long int num;                /* temporary space for numeric conversion */
    char *tmp;                   /* temporary space */
    unsigned int ind;            /* current index in argv array */
    enum getlongopt_ret ret;     /* return value from option parser */
    FILE *fp;                    /* file pointer to open file list with */
    double dnum;

    struct getlongopt_opt opts[] = {
        {"index", 'i', GETLONGOPT_ARG_NONE, OPT_INDEX},
        {"add", 'a', GETLONGOPT_ARG_NONE, OPT_ADD},
        {"build-stats", '\0', GETLONGOPT_ARG_NONE, OPT_ADD_STATS},
        {"filename", 'f', GETLONGOPT_ARG_REQUIRED, OPT_FILENAME},
        {"config", 'c', GETLONGOPT_ARG_REQUIRED, OPT_CONFIG},
        {"type", 't', GETLONGOPT_ARG_REQUIRED, OPT_TYPE},
        {"number-results", 'n', GETLONGOPT_ARG_REQUIRED, OPT_NUMBER_RESULTS},
        {"begin-results", 'b', GETLONGOPT_ARG_REQUIRED, OPT_BEGIN_RESULTS},
        {"version", 'v', GETLONGOPT_ARG_NONE, OPT_VERSION},
        {NULL, 'V', GETLONGOPT_ARG_NONE, OPT_VERSION},
        {"help", 'h', GETLONGOPT_ARG_NONE, OPT_HELP},
        {NULL, 'H', GETLONGOPT_ARG_NONE, OPT_HELP},
        {"stats", 's', GETLONGOPT_ARG_NONE, OPT_STATS},
        {"summary", '\0', GETLONGOPT_ARG_REQUIRED, OPT_SUMMARY},    
        {"big-and-fast", '\0', GETLONGOPT_ARG_NONE, OPT_BIG_AND_FAST},
        {"memory", 'm', GETLONGOPT_ARG_REQUIRED, OPT_MEMORY},
        {"tablesize", '\0', GETLONGOPT_ARG_REQUIRED, OPT_TABLESIZE},
        {"parse-buffer", '\0', GETLONGOPT_ARG_REQUIRED, OPT_PARSEBUF},
        {"file-list", 'L', GETLONGOPT_ARG_REQUIRED, OPT_FILELIST},    
        {"query-list", '\0', GETLONGOPT_ARG_REQUIRED, OPT_QUERYLIST},    
        {"word-limit", '\0', GETLONGOPT_ARG_REQUIRED, OPT_WORD_LIMIT},    
        {"stem", '\0', GETLONGOPT_ARG_REQUIRED, OPT_STEM},    
        {"build-stop", '\0', GETLONGOPT_ARG_REQUIRED, OPT_BUILD_STOP},
        {"query-stop", '\0', GETLONGOPT_ARG_OPTIONAL, OPT_QUERY_STOP},
        {"accumulator-limit", 'A', GETLONGOPT_ARG_REQUIRED,
            OPT_ACCUMULATOR_LIMIT},
        {"ignore-version", '\0', GETLONGOPT_ARG_NONE, OPT_IGNORE_VERSION},

        /* metrics */
        {"okapi", '\0', GETLONGOPT_ARG_NONE, OPT_OKAPI},
        {"k1", '1', GETLONGOPT_ARG_REQUIRED, OPT_K1},
        {"k3", '3', GETLONGOPT_ARG_REQUIRED, OPT_K3},
        {"b", 'b', GETLONGOPT_ARG_REQUIRED, OPT_B},
        {"pivoted-cosine", '\0', GETLONGOPT_ARG_REQUIRED, OPT_PIVOTED_COSINE},
        {"cosine", '\0', GETLONGOPT_ARG_NONE, OPT_COSINE},
        {"hawkapi", '\0', GETLONGOPT_ARG_REQUIRED, OPT_HAWKAPI},
        {"anh-impact", '\0', GETLONGOPT_ARG_NONE, OPT_ANH_IMPACT},
        {"dirichlet", '\0', GETLONGOPT_ARG_REQUIRED, OPT_DIRICHLET},

        {"accumulation-memory", '\0', GETLONGOPT_ARG_REQUIRED, 
          OPT_ACCUMULATION_MEMORY},
        {"accumulation-docs", '\0', GETLONGOPT_ARG_REQUIRED, 
          OPT_ACCUMULATION_DOCS},
        {"dump-memory", '\0', GETLONGOPT_ARG_REQUIRED, OPT_DUMP_MEMORY},
        {"dump-vecs", '\0', GETLONGOPT_ARG_REQUIRED, OPT_DUMP_VECS},
        {"max-file-size", '\0', GETLONGOPT_ARG_REQUIRED, OPT_MAXFILESIZE},
        {"file-listsize", '\0', GETLONGOPT_ARG_REQUIRED, OPT_MAXFLIST},
    };

    /* set defaults */
    args->index = 0;             /* search by default */
    args->index_add = 0;         /* search by default */
    args->index_add_stats = 0;
    args->stat = 0;              /* search by default */
    args->first_result = 0;

    /* null out the others */
    args->results = 0;
    args->type = NULL;
    args->prefix = NULL;
    args->config_file = NULL;
    args->stop_file = NULL;
    args->qstop_file = NULL;
    args->memory = 0;
    args->list = NULL;
    args->qlist = stdin;

    args->aopts = INDEX_ADD_NOOPT;
    args->sopts = INDEX_SEARCH_NOOPT;
    args->copts = INDEX_COMMIT_NOOPT;
    args->nopts = INDEX_NEW_NOOPT;
    args->lopts = INDEX_LOAD_NOOPT;

    /* provide okapi defaults */
    args->sopt.u.okapi_k3.k1 = 1.2F;
    args->sopt.u.okapi_k3.k3 = 1e10; /* inf */
    args->sopt.u.okapi_k3.b = 0.75;

    args->sopt.summary_type = INDEX_SUMMARISE_NONE;

    args->nopt.stop_file = NULL;

    args->lopt.docmap_cache = DOCMAP_CACHE_TRECNO;

    if ((parser = getlongopt_new(argc - 1, (const char **) &argv[1], opts, 
        sizeof(opts) / sizeof(*opts)))) {
        /* succeeded, do nothing */
    } else {
        fprintf(output, "failed to initialise options parser\n");
        return NULL;
    }

    while ((ret = getlongopt(parser, &id, &arg)) == GETLONGOPT_OK) {
        switch (id) {
        case OPT_INDEX:
            /* indexing is on */
            if (!must_search && !must_stat) {
                must_index = 1;
                args->index = 1;
            } else {
                err = 1;
                fprintf(output, 
                  "i option cannot be used with search options\n");
            }
            break;

        case OPT_QUERYLIST:
            if (args->qlist == stdin) {
                if (!(args->qlist = fopen(arg, "rb"))) {
                    fprintf(output, "unable to open query list '%s': %s\n",
                      arg, strerror(errno));
                    err = 1;
                }
            } else {
                err = 1;
                fprintf(output, "query list already specified\n");
            }
            break;

        case OPT_ADD:
            /* updating is on */
            if (!must_search && !must_stat) {
                must_index = 1;
                args->index_add = 1;
            } else {
                err = 1;
                fprintf(output, 
                  "a option cannot be used with search options\n");
            }
            break;

        case OPT_ADD_STATS:
            args->index_add_stats = 1;
            break;

        case OPT_STATS:
            /* stat printing on */
            if (!must_search && !must_index) {
                must_stat = 1;
                args->stat = 1;
            } else {
                err = 1;
                fprintf(output, 
                  "s option cannot be used with search options\n");
            }
            break;

        case OPT_FILENAME:
            /* they want to set the prefix */
            if (!args->prefix) {
                if (!(args->prefix = str_dup(arg))) {
                    err = quiet = 1;
                    fprintf(output, "can't form index name: %s\n", 
                      strerror(errno));
                }
            } else {
                err = 1;
                fprintf(output, "prefix already set to %s\n", args->prefix);
            }
            break;

        case OPT_WORD_LIMIT:
            /* they want to set the word limit */
            if (!(args->sopts & INDEX_SEARCH_WORD_LIMIT)) {
                args->sopts |= INDEX_SEARCH_WORD_LIMIT;
                errno = 0;
                num = strtol(arg, &tmp, 10);
                if (!errno && (num > 0) && (num <= UINT_MAX) && !*tmp) {
                    args->sopt.word_limit = num;
                } else {
                    fprintf(output, 
                      "error converting word limit value '%s'\n", arg);
                    err = 1;
                    verbose = 0;
                }
            } else {
                verbose = 0;
                err = 1;
                fprintf(output, "word limit already set to %u\n", 
                  args->sopt.word_limit);
            }
            break;


        case OPT_MAXFILESIZE:
            /* they want to set the maximum filesize */
            if (!(args->nopts & INDEX_NEW_MAXFILESIZE)) {
                args->nopts |= INDEX_NEW_MAXFILESIZE;
                errno = 0;
                num = strtol(arg, &tmp, 10);
                if (!errno && (num > 0) && (num <= UINT_MAX) && !*tmp) {
                    args->nopt.maxfilesize = num;
                } else {
                    fprintf(output, 
                      "error converting maximum file size value '%s'\n", arg);
                    err = 1;
                    verbose = 0;
                }
            } else {
                verbose = 0;
                err = 1;
                fprintf(output, "maximum file size already set to %lu\n", 
                  args->nopt.maxfilesize);
            }
            break;

        case OPT_DUMP_MEMORY:
            /* they want to set the amount of memory used for dumping */
            if (!(args->copts & INDEX_COMMIT_DUMPBUF)) {
                args->copts |= INDEX_COMMIT_DUMPBUF;
                errno = 0;
                num = strtol(arg, &tmp, 10);
                if (!errno && (num > 0) && (num <= UINT_MAX) && !*tmp) {
                    args->copt.dumpbuf = num;
                } else {
                    fprintf(output, "error converting dump memory value '%s'\n",
                      arg);
                    err = 1;
                    verbose = 0;
                }
            } else {
                verbose = 0;
                err = 1;
                fprintf(output, "dump memory already set to %u\n", 
                  args->copt.dumpbuf);
            }
            break;

        case OPT_ACCUMULATION_DOCS:
            /* they want to set the amount of documents accumulated */
            if (!(args->aopts & INDEX_ADD_ACCDOC)) {
                args->aopts |= INDEX_ADD_ACCDOC;
                errno = 0;
                num = strtol(arg, &tmp, 10);
                if (!errno && (num > 0) && (num <= UINT_MAX) && !*tmp) {
                    args->aopt.accdoc = num;
                } else {
                    fprintf(output, "error converting acc docs value '%s'\n",
                      arg);
                    err = 1;
                    verbose = 0;
                }
            } else {
                verbose = 0;
                err = 1;
                fprintf(output, "acc docs already set to %u\n", 
                  args->aopt.accdoc);
            }
            break;

        case OPT_ACCUMULATION_MEMORY:
            /* they want to set the amount of memory used */
            if (!(args->aopts & INDEX_ADD_ACCBUF)) {
                args->aopts |= INDEX_ADD_ACCBUF;
                errno = 0;
                num = strtol(arg, &tmp, 10);
                if (!errno && (num > 0) && (num <= UINT_MAX) && !*tmp) {
                    args->aopt.accbuf = num;
                } else {
                    fprintf(output, "error converting memory value '%s'\n", 
                      arg);
                    err = 1;
                    verbose = 0;
                }
            } else {
                verbose = 0;
                err = 1;
                fprintf(output, "memory already set to %u\n", 
                  args->aopt.accbuf);
            }
            break;

        case OPT_BIG_AND_FAST:
            /* use big memory options */
            if (!args->memory) {
                args->memory = BIG_MEMORY_DEFAULT;
            }
            if (!(args->nopts & INDEX_NEW_PARSEBUF)) {
                args->nopts |= INDEX_NEW_PARSEBUF;
                args->lopts |= INDEX_LOAD_PARSEBUF;
                args->nopt.parsebuf = args->lopt.parsebuf = BIG_PARSE_BUFFER;
            }
            if (!(args->nopts & INDEX_NEW_TABLESIZE)) {
                args->nopts |= INDEX_NEW_TABLESIZE;
                args->lopts |= INDEX_LOAD_TABLESIZE;
                args->nopt.tablesize = args->lopt.tablesize = BIG_TABLESIZE;
            }
            break;

        case OPT_MEMORY:
            /* they want to set the amount of memory used */
            if (!args->memory) {
                errno = 0;
                num = strtol(arg, &tmp, 10);
                if (!errno && (num > 0) && (num <= UINT_MAX) && !*tmp) {
                    args->memory = num;
                } else {
                    fprintf(output, "error converting memory value '%s'\n", 
                      arg);
                    err = 1;
                    verbose = 0;
                }
            } else {
                verbose = 0;
                err = 1;
                fprintf(output, "memory already set to %u\n", args->memory);
            }
            break;

        case OPT_PARSEBUF:
            errno = 0;
            num = strtol(arg, &tmp, 10);
            if (!errno && (num > 0) && (num <= UINT_MAX) && !*tmp) {
                args->nopts |= INDEX_NEW_PARSEBUF;
                args->lopts |= INDEX_LOAD_PARSEBUF;
                args->nopt.parsebuf = args->lopt.parsebuf = num;
            } else {
                fprintf(output, "error converting parsebuf value '%s'\n", 
                  arg);
                err = 1;
                verbose = 0;
            }
            break;

        case OPT_TABLESIZE:
            errno = 0;
            num = strtol(arg, &tmp, 10);
            if (!errno && (num > 0) && (num <= UINT_MAX) && !*tmp) {
                args->nopts |= INDEX_NEW_TABLESIZE;
                args->lopts |= INDEX_LOAD_TABLESIZE;
                args->nopt.tablesize = args->lopt.tablesize = num;
            } else {
                fprintf(output, "error converting tablesize value '%s'\n", 
                  arg);
                err = 1;
                verbose = 0;
            }
            break;

        case OPT_STEM:
            args->nopts |= INDEX_NEW_STEM;
            stem = 1;
            if (!str_casecmp(arg, "eds")) {
                args->nopt.stemmer = INDEX_STEM_EDS;
            } else if (!str_casecmp(arg, "light")) {
                args->nopt.stemmer = INDEX_STEM_LIGHT;
            } else if (!str_casecmp(arg, "none")) {
                /* in fact, don't stem */
                args->nopts ^= INDEX_NEW_STEM;
            } else if (!str_casecmp(arg, "porters")
              || (!str_casecmp(arg, "porter"))) {
                args->nopt.stemmer = INDEX_STEM_PORTERS;
            } else {
                fprintf(output, "unrecognised stemming algorithm '%s'\n", arg);
                err = 1;
            }
            break;

        case OPT_QUERY_STOP:
            if (args->qstop_file) {
                err = 1;
                fprintf(output, "query stoplist specified multiple times\n");
            } else if (!arg || (args->qstop_file = str_dup(arg))) {
                args->nopts |= INDEX_NEW_QSTOP;
                args->nopt.qstop_file = args->qstop_file;
                args->lopts |= INDEX_LOAD_QSTOP;
                args->lopt.qstop_file = args->qstop_file;
            } else {
                err = quiet = 1;
                fprintf(output, "can't copy query stoplist name\n");
            } 
            break;

        case OPT_BUILD_STOP:
            if (!(args->nopts & INDEX_NEW_STOP)) {
                if (!(args->stop_file = str_dup(arg))) {
                    err = quiet = 1;
                    fprintf(output, "can't form index name: %s\n", 
                      strerror(errno));
                } else {
                    args->nopts |= INDEX_NEW_STOP;
                    args->nopt.stop_file = args->stop_file;
                }
            } else {
                err = 1;
                fprintf(output, "stop file already set to %s\n", 
                  args->stop_file);
            }
            break;

        case OPT_CONFIG:
            /* they want to use a different config file than the default */
            if (!must_search && !must_stat && !args->config_file) {
                must_index = 1;
                if (!(args->config_file = str_dup(arg))) {
                    verbose = 0;
                    err = quiet = 1;
                    fprintf(output, "can't copy config\n");
                }
            } else if (args->config_file) {
                fprintf(output, "config already set to '%s'\n", 
                  args->config_file);
                verbose = 0;
                err = 1;
            } else {
                verbose = 0;
                err = 1;
                fprintf(output, 
                  "c option cannot be used with search options\n");
            }
            break;

        case OPT_TYPE:
            /* type specification */
            if (!must_search && !must_stat && (!args->type)) {
                /* be backward compatible with previous versions, recognise
                 * 'trec', 'html' and 'inex' */
                if (!str_casecmp(arg, "trec")) {
                    if (!(args->type = str_dup("application/x-trec"))) {
                        fprintf(output, "no memory to copy type trec\n");
                        verbose = 0;
                        err = 1;
                    }
                } else if (!str_casecmp(arg, "html")) {
                    if (!(args->type = str_dup("text/html"))) {
                        fprintf(output, "no memory to copy type html\n");
                        verbose = 0;
                        err = 1;
                    }
                } else if (!str_casecmp(arg, "inex")) {
                    if (!(args->type = str_dup("application/x-inex"))) {
                        fprintf(output, "no memory to copy type inex\n");
                        verbose = 0;
                        err = 1;
                    }
                } else if (!(args->type = str_dup(arg))) {
                    fprintf(output, "no memory, couldn't copy type '%s'\n", 
                      arg);
                    verbose = 0;
                    err = 1;
                }
            } else if (args->type) {
                fprintf(output, "type specified multiple times\n");
                verbose = 0;
                err = 1;
            } else {
                verbose = 0;
                err = 1;
                fprintf(output, 
                  "t option cannot be used with search options\n");
            }
            break;

        case OPT_NUMBER_RESULTS:
            if (!must_index && !must_stat && (args->results == 0)) {
                must_search = 1;
                errno = 0;
                num = strtol(arg, &tmp, 10);
                if (!errno && (num > 0) && (num <= UINT_MAX) && !*tmp) {
                    args->results = num;
                } else {
                    fprintf(output, "error converting results value '%s'\n", 
                      arg);
                    err = 1;
                }
            } else if (args->results) {
                fprintf(output, 
                  "results already specified (%d)\n", args->results);
                verbose = 0;
                err = 1;
            } else {
                verbose = 0;
                err = 1;
                fprintf(output, 
                  "n option cannot be used with indexing options\n");
            }
            break;

        case OPT_BEGIN_RESULTS:
            if (!must_index && !must_stat && (args->first_result == 0)) {
                must_search = 1;
                errno = 0;
                num = strtol(arg, &tmp, 10);
                if (!errno && (num >= 0) && (num <= UINT_MAX) && !*tmp) {
                    args->first_result = num;
                } else {
                    fprintf(output, 
                      "error converting start result value '%s'\n", arg);
                    verbose = 0;
                    err = 1;
                }
            } else if (args->first_result) {
                fprintf(output, 
                  "start result already specified (%u)\n", args->first_result);
                verbose = 0;
                err = 1;
            } else {
                verbose = 0;
                err = 1;
                fprintf(output, 
                  "b option cannot be used with indexing options\n");
            }
            break;

        case OPT_SUMMARY:
            /* they want to set the query-biased summary type */
            if (!must_index && !str_casecmp(arg, "plain")) {
                must_search = 1;
                args->sopt.summary_type = INDEX_SUMMARISE_PLAIN;
                args->sopts |= INDEX_SEARCH_SUMMARY_TYPE;
            } else if ((!must_index && !str_casecmp(arg, "capitalise"))
              || (!must_index && !str_casecmp(arg, "capitalize"))) {
                must_search = 1;
                args->sopt.summary_type = INDEX_SUMMARISE_CAPITALISE;
                args->sopts |= INDEX_SEARCH_SUMMARY_TYPE;
            } else if (!must_index && !str_casecmp(arg, "tag")) {
                must_search = 1;
                args->sopt.summary_type = INDEX_SUMMARISE_TAG;
                args->sopts |= INDEX_SEARCH_SUMMARY_TYPE;
            } else if (!must_index && !str_casecmp(arg, "none")) {
                must_search = 1;
                args->sopt.summary_type = INDEX_SUMMARISE_NONE;
                args->sopts |= INDEX_SEARCH_SUMMARY_TYPE;
            } else if (!must_index) {
                fprintf(output, "unrecognised summary type '%s'\n", arg);
                err = 1;
            } else {
                fprintf(output, "no summary available during indexing\n");
                err = 1;
            }
            break;

        case OPT_VERSION: 
            printf("version %s\n", PACKAGE_VERSION);
            err = 1;
            if (argc == 2) {
                quiet = 1;
                output = stdout; /* direct to stdout since they requested it */
                verbose = 0;
            }
            break;

        case OPT_HELP:
            err = 1;
            verbose = 1;
            output = stdout;  /* direct to stdout since they requested it */
            break;

        case OPT_FILELIST:
            if ((fp = fopen(arg, "rb"))) {
                if (!fill_args_list(args, fp, path)) {
                    fprintf(output, "unable to parse list of files from '%s'\n",
                      arg);
                    err = 1;
                }
                fclose(fp);
            } else {
                fprintf(output, "unable to read '%s' for list of files\n", 
                  arg);
            }
            break;

        case OPT_OKAPI: 
            if (!must_index && !must_stat) {
                must_search = 1;
                if (metric) {
                    err = 1;
                    fprintf(output, "metric set multiple times\n");
                } else {
                    metric = 1;
                    args->sopts |= INDEX_SEARCH_OKAPI_RANK;
                }
            } else {
                err = 1;
                fprintf(output, 
                  "okapi option must be used with search options\n");
            }
            break;

        case OPT_K1: 
            if (!must_index && !must_stat) {
                must_search = 1;
                errno = 0;
                dnum = strtod(arg, &tmp);
                if (!errno && !*tmp) {
                    args->sopt.u.okapi_k3.k1 = (float) dnum;
                } else {
                    fprintf(output, 
                      "error converting start result value '%s'\n", arg);
                    verbose = 0;
                    err = 1;
                }
            } else {
                err = 1;
                fprintf(output, 
                  "k1 option must be used with search options\n");
            }
            break;

        case OPT_K3: 
            if (!must_index && !must_stat) {
                must_search = 1;
                errno = 0;
                dnum = strtod(arg, &tmp);
                if (!errno && !*tmp) {
                    args->sopt.u.okapi_k3.k3 = (float) dnum;
                } else {
                    fprintf(output, 
                      "error converting start result value '%s'\n", arg);
                    verbose = 0;
                    err = 1;
                }
            } else {
                err = 1;
                fprintf(output, 
                  "k3 option must be used with search options\n");
            }
            break;

        case OPT_B:
            if (!must_index && !must_stat) {
                must_search = 1;
                errno = 0;
                dnum = strtod(arg, &tmp);
                if (!errno && !*tmp) {
                    args->sopt.u.okapi_k3.b = (float) dnum;
                } else {
                    fprintf(output, 
                      "error converting start result value '%s'\n", arg);
                    verbose = 0;
                    err = 1;
                }
            } else {
                err = 1;
                fprintf(output, 
                  "okapi b option must be used with search options\n");
            }
            break;

        case OPT_ANH_IMPACT:
            args->sopts |= INDEX_SEARCH_ANH_IMPACT_RANK;
            args->copts |= INDEX_COMMIT_ANH_IMPACTS;
            break;

        case OPT_DIRICHLET:
            if (!must_index && !must_stat) {
                must_search = 1;
                if (metric) {
                    err = 1;
                    fprintf(output, "metric set multiple times\n");
                } else {
                    metric = 1;
                    args->sopts |= INDEX_SEARCH_DIRICHLET_RANK;

                    errno = 0;
                    dnum = strtod(arg, &tmp);
                    if (!errno && !*tmp) {
                        args->sopt.u.dirichlet.mu = (float) dnum;
                    } else {
                        fprintf(output, 
                          "error converting mu value '%s'\n", arg);
                        verbose = 0;
                        err = 1;
                    }
                }
            } else {
                err = 1;
                fprintf(output, 
                  "dirichlet option must be used with search options\n");
            }
            break;

        case OPT_HAWKAPI:
            if (!must_index && !must_stat) {
                must_search = 1;
                if (metric) {
                    err = 1;
                    fprintf(output, "metric set multiple times\n");
                } else {
                    metric = 1;
                    args->sopts |= INDEX_SEARCH_HAWKAPI_RANK;

                    errno = 0;
                    dnum = strtod(arg, &tmp);
                    if (!errno && !*tmp) {
                        args->sopt.u.hawkapi.alpha = (float) dnum;
                        args->sopt.u.hawkapi.k3 = 1e10; /* ~= inf */
                    } else {
                        fprintf(output, 
                          "error converting start result value '%s'\n", arg);
                        verbose = 0;
                        err = 1;
                    }
                }
            } else {
                err = 1;
                fprintf(output, 
                  "hawkapi option must be used with search options\n");
            }
            break;

        case OPT_PIVOTED_COSINE: 
            if (!must_index && !must_stat) {
                must_search = 1;
                if (metric) {
                    err = 1;
                    fprintf(output, "metric set multiple times\n");
                } else {
                    metric = 1;
                    args->sopts |= INDEX_SEARCH_PCOSINE_RANK;

                    /* arrange for weights to be loaded into memory */
                    args->lopts |= INDEX_LOAD_DOCMAP_CACHE;
                    args->lopt.docmap_cache |= DOCMAP_CACHE_WEIGHT;

                    errno = 0;
                    dnum = strtod(arg, &tmp);
                    if (!errno && !*tmp) {
                        args->sopt.u.pcosine.pivot = (float) dnum;
                    } else {
                        fprintf(output, 
                          "error converting start result value '%s'\n", arg);
                        verbose = 0;
                        err = 1;
                    }
                }
            } else {
                err = 1;
                fprintf(output, 
                  "pivoted cosine option must be used with search options\n");
            }
            break;

        case OPT_COSINE:
            if (!must_index && !must_stat) {
                must_search = 1;
                if (metric) {
                    err = 1;
                    fprintf(output, "metric set multiple times\n");
                } else {
                    metric = 1;
                    args->sopts |= INDEX_SEARCH_COSINE_RANK;

                    /* arrange for weights to be loaded into memory */
                    args->lopts |= INDEX_LOAD_DOCMAP_CACHE;
                    args->lopt.docmap_cache |= DOCMAP_CACHE_WEIGHT;
                }
            } else {
                err = 1;
                fprintf(output, 
                  "cosine option must be used with search options\n");
            }
            break;

        case OPT_VOCAB_LISTSIZE: 
            errno = 0;
            num = strtol(arg, &tmp, 10);
            if (!errno && !*tmp) {
                args->lopt.vocab_size = args->nopt.vocab_size = num;
                args->lopts |= INDEX_LOAD_VOCAB;
                args->nopts |= INDEX_NEW_VOCAB;
            } else {
                fprintf(output, 
                  "error converting vocab list-size value '%s'\n", arg);
                verbose = 0;
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
                verbose = 0;
                err = 1;
            }
            break;

        case OPT_MAXFLIST: 
            errno = 0;
            num = strtol(arg, &tmp, 10);
            if (!errno && !*tmp) {
                args->lopt.maxflist_size = num;
                args->lopts |= INDEX_LOAD_MAXFLIST;
            } else {
                fprintf(output, 
                  "error converting vocab list-size value '%s'\n", arg);
                verbose = 0;
                err = 1;
            }
            break;

        default:
            /* shouldn't happen */
            assert(0);
        };
    }

    /* finish up with the option parser */
    ind = getlongopt_optind(parser) + 1;
    getlongopt_delete(parser);

    if (ret == GETLONGOPT_END) {
        /* succeeded, do nothing */
    } else {
        if (ret == GETLONGOPT_UNKNOWN) {
            fprintf(output, "unknown option '%s'\n", argv[ind]);
        } else if (ret == GETLONGOPT_MISSING_ARG) {
            fprintf(output, "missing argument to option '%s'\n", argv[ind]);
        } else if (ret == GETLONGOPT_ERR) {
            fprintf(output, "unexpected error parsing options (around '%s')\n",
              argv[ind]);
        } else {
            /* shouldn't happen */
            assert(0);
        }
        return NULL;
    }

    if (!err && !args->prefix) {
        if (!(args->prefix = str_dup("index"))) {
            fprintf(output, "couldn't copy prefix 'index'\n");
            err = quiet = 1;
        }
    }

    if (!err && !args->memory) {
        args->memory = MEMORY_DEFAULT;
    }

    if (!err && !args->results) {
        args->results = 20;
    }

    /* use light stemming by default */
    if (!err && !stem) {
        args->nopts |= INDEX_NEW_STEM;
        args->nopt.stemmer = INDEX_STEM_LIGHT;
    }
 
    /* remaining arguments should be sources */
    if (!args->list) {
        if ((args->list = malloc(sizeof(*args->list)))) {
            args->list[0] = NULL;
        } else {
            fprintf(output, "couldn't allocate list memory\n");
            err = quiet = 1;
        }
    }

    /* count initial items */
    for (listitems = 0; args->list[listitems]; listitems++) ;

    while (!err && (ind < argc)) {
        void *ptr;

        if ((ptr
          = realloc(args->list, (listitems + 2) * sizeof(*args->list)))) {
            args->list = ptr;
        } else {
            fprintf(output, "couldn't allocate list memory\n");
            err = quiet = 1;
        }
        if (!err) {
            if (!args->stat && !args->index && !args->index_add) {
                /* querying (don't prepend path) */
                if ((args->list[listitems] = str_dup(argv[ind]))) {
                    listitems++;
                } else {
                    fprintf(output, "couldn't allocate list memory\n");
                    err = quiet = 1;
                }
            } else {
                /* not querying */
                if ((args->list[listitems] = path_dup(argv[ind], path))) {
                    listitems++;
                } else {
                    fprintf(output, "couldn't allocate list memory\n");
                    err = quiet = 1;
                }
            }
        }
        ind++;
    }
    args->list[listitems] = NULL;

    if (err) {
        if (!quiet) {
            fprintf(output, "\n");
            print_usage(*argv, output, verbose);
        }
        free_args(args);
        return NULL;
    } else {
        return args;
    }
}

/* internal function that indicates whether the given query is a request to
 * return a document from the cache.  Returns true if it is, else false on error
 * or if it isn't.  If true, then docno will contain requested document 
 * number */
static int is_cache_request(const char *querystr, unsigned int maxwordlen, 
  unsigned long int *docno) {
    struct queryparse *qp;           /* structure to parse the query */
    char word[TERMLEN_MAX + 1];      /* buffer to hold words */
    unsigned int wordlen;            /* length of word */
    int parse_ret;                   /* parsing result */
    char *end;
    
    if (!(qp 
      = queryparse_new(maxwordlen, querystr, str_len(querystr)))) {

        return 0;
    }

    /* first element must be a cache: modifier */
    parse_ret = queryparse_parse(qp, word, &wordlen);
    if ((parse_ret != QUERYPARSE_START_MODIFIER) 
      || (wordlen != str_len("cache")) || str_ncmp(word, "cache", wordlen)) {
        /* didn't match */
        queryparse_delete(qp);
        return 0;
    }

    /* second element must the docno (XXX: or URL) of document */
    parse_ret = queryparse_parse(qp, word, &wordlen);
    word[wordlen] = '\0';
    if ((parse_ret != QUERYPARSE_WORD) 
      || ((*docno = strtol(word, &end, 10)) < 0) 
      || (*end != '\0')) {
        /* didn't match */
        queryparse_delete(qp);
        return 0;
    }

    /* third element must be an end modifier */
    parse_ret = queryparse_parse(qp, word, &wordlen);
    if (parse_ret != QUERYPARSE_END_MODIFIER) {
        /* didn't match */
        queryparse_delete(qp);
        return 0;
    }

    /* that must be the last element */
    parse_ret = queryparse_parse(qp, word, &wordlen);
    if (parse_ret != QUERYPARSE_EOF) {
        /* didn't match */
        queryparse_delete(qp);
        return 0;
    }

    queryparse_delete(qp);
    return 1;
}

int search(struct index *idx, const char *query, struct index_result *result, 
  unsigned int requested, unsigned int start, unsigned int maxwordlen,
  int opts, struct index_search_opt *opt) {
    unsigned int results,                /* number of results */
                 i;
    double total_results;
    struct timeval then,
                   now;
    double seconds;
    unsigned long int docno;
    int est;

    /* check to see whether they have requested a document from the cache */
    if (!is_cache_request(query, maxwordlen, &docno)) {

        gettimeofday(&then, NULL);

        if (index_search(idx, query, start, requested,
              result, &results, &total_results, &est, opts, opt)) {

            gettimeofday(&now, NULL);

            seconds = (double) ((now.tv_sec - then.tv_sec) 
              + (now.tv_usec - (double) then.tv_usec) / 1000000.0);

            for (i = 0; i < results; i++) {
                fprintf(stdout, "%u. %s (score %f, docid %lu)\n",
                      start + i + 1, result[i].auxilliary, result[i].score,
                      result[i].docno);
                
                if (opt->summary_type != INDEX_SUMMARISE_NONE) {
                    /* print out document summary and title */ 
                    if (result[i].title[0] != '\0') {
                        fprintf(stdout, "title: %s\n", 
                          result[i].title);
                    }
                    if (result[i].summary[0] != '\0') {
                        fprintf(stdout, "%s\n", result[i].summary);
                    }
                }              
            }
          
            if (seconds == 0.0) {
                fprintf(stdout, "\n%u results of %s%.0f shown\n", 
                  i, est ? "about " : "", total_results);
            } else {
                fprintf(stdout, "\n%u results of %s%.0f shown "
                  "(took %f seconds)\n",
                  i, est ? "about " : "", total_results, seconds);
            }
        }
    } else {
        int readlen;
        unsigned int total = 0;
        char buf[BUFSIZ];

        while ((readlen = index_retrieve(idx, docno, total, buf, BUFSIZ)) > 0) {
            fwrite(buf, 1, readlen, stdout);
            total += readlen;
        } 

        if (readlen < 0) {
            /* error occurred */
            fprintf(stderr, "failed to retrieve document %lu\n", docno);
            return 0;
        }

        return 1;
    }

    return 1;
}

int build(struct args *args, FILE *output) {
    struct index *idx = NULL;                /* index we're constructing */
    unsigned int i,                          /* counter */
                 docs;
    int ret;
    unsigned long int docno;
    struct index_stats stats;
    struct index_expensive_stats estats;
    struct timeval now,
                   then;
    double seconds;

    TIMINGS_DECL();

    TIMINGS_START();

    fprintf(output, "%s version %s", PACKAGE, PACKAGE_VERSION);

    if (1) {
        /* print a little more info */
        int ndebug = 0;

#ifdef NDEBUG
        ndebug = 1;
#endif

        fprintf(output, ", %s%s", 
          ndebug ? "" : "no NDEBUG, ", 
          DEAR_DEBUG ? "DEAR_DEBUG! (may run EXTREMELY slowly)" : "");
    }
    fprintf(output, "\n");

    if (signal(SIGINT, signals_cleanup_handler) == SIG_ERR) {
        fprintf(stderr, "Warning: unable to catch SIGINT\n");
    }
    if (signal(SIGTERM, signals_cleanup_handler) == SIG_ERR) {
        fprintf(stderr, "Warning: unable to catch SIGTERM\n");
    }

    if (args->index_add) {
        if ((idx 
          = index_load(args->prefix, args->memory, args->lopts, &args->lopt))
            == NULL) {

            fprintf(stderr, "Failed to load index with prefix '%s': "
              "%s\n", args->prefix, error_last_msg());
            return 0;
        }
        fprintf(output, "loaded index '%s'\n", args->prefix);
    } else {
        assert(args->index);
        if ((idx = index_new(args->prefix, args->config_file, args->memory, 
          args->nopts, &args->nopt)) == NULL) {
            fprintf(stderr, "Failed to create new index with prefix '%s': "
              "%s\n", args->prefix, error_last_msg());
            return 0;
        }
        fprintf(output, "created new index '%s'\n", args->prefix);
    }
    signals_set_index_under_construction(idx);

    fprintf(output, "sources (type %s): ", args->type ? args->type : "auto");
    for (i = 0; args->list[i]; i++) {
        fprintf(output, "%s ", args->list[i]);
    }
    fprintf(output, "\n");

    /* add repositories from args */
    for (i = 0; args->list[i]; i++) {
        fprintf(output, "parsing %s... ", args->list[i]);
        fflush(output);

        gettimeofday(&then, NULL);

        if (index_add(idx, args->list[i], args->type, &docno, &docs, 
            args->aopts, &args->aopt, args->copts, &args->copt)) {

            gettimeofday(&now, NULL);

            seconds = (double) ((now.tv_sec - then.tv_sec) 
              + (now.tv_usec - (double) then.tv_usec) / 1000000.0);

            /* succeded */
            fprintf(output, "found %u doc%s, %s%s%s%f seconds\n", docs, 
              docs == 1 ? "": "s", 
              !args->type ? "type " : "",
              !args->type 
                ? (args->aopt.detected_type
                  ? args->aopt.detected_type
                  : "unknown")
                : "",
              !args->type ? ", " : "", seconds);
        } else {
            fprintf(stderr, "error while adding file %s: %s\n", 
              args->list[i], error_last_msg());
            index_rm(idx);
            index_delete(idx);
            return 0;
        }
    }

    if (args->index && !args->index_add) {
        fprintf(output, "merging...\n");
    } else {
        assert(args->index_add);
        fprintf(output, "updating...\n");
    }

    if (index_commit(idx, args->copts, &args->copt, args->aopts, &args->aopt)) {
        /* successful, do nothing */
    } else {
        fprintf(stderr, "error committing...\n");
        index_rm(idx);
        index_delete(idx);
        return 0;
    }

    /* timings */
    TIMINGS_END("build");

    /* get stats from index */
    ret = index_stats(idx, &stats);
    if (args->index_add_stats) {
        index_expensive_stats(idx, &estats);
    }
    signals_clear_index_under_construction();
    index_delete(idx);

    if (ret) {
        /* print out end stuff */
        if (stats.terms_high) {
            fprintf(output, 
              "\nsummary: %lu documents, %lu distinct index terms, "
              "%u %u terms\n", stats.docs, stats.dterms, 
              stats.terms_high, stats.terms_low);
        } else {
            /* terms.high unused, don't print it */
            fprintf(output, 
              "\nsummary: %lu documents, %lu distinct index terms, "
              "%u terms\n", stats.docs, stats.dterms, 
              stats.terms_low);
        }

        if (args->index_add_stats) {
            /* print out extended summary */
            printf("dterms: %lu\n", stats.dterms);
            printf("terms_high: %u\n", stats.terms_high);
            printf("terms_low: %u\n", stats.terms_low);
            printf("docs: %lu\n", stats.docs);
            printf("avg_weight: %f\n", estats.avg_weight);
            printf("avg_words: %f\n", estats.avg_words);
            printf("avg_length: %f\n", estats.avg_length);
            printf("maxtermlen: %u\n", stats.maxtermlen);
            printf("vocab_listsize: %u\n", stats.vocab_listsize);
            printf("updates: %u\n", stats.updates);
            printf("tablesize: %u\n", stats.tablesize);
            printf("parsebuf: %u\n", stats.parsebuf); 
            printf("vocab_leaves: %u\n", estats.vocab_leaves); 
            printf("vocab_pages: %u\n", estats.vocab_pages); 
            printf("pagesize: %u\n", estats.pagesize); 
            printf("vectors: %f\n", estats.vectors); 
            printf("vectors_files: %f\n", estats.vectors_files); 
            printf("vectors_vocab: %f\n", estats.vectors_vocab); 
            printf("allocated_files: %f\n", estats.allocated_files); 
            printf("vocab_info: %f\n", estats.vocab_info); 
            printf("vocab_structure: %f\n", estats.vocab_structure); 
            printf("sorted: %d\n", stats.sorted); 
        }

        return 1;
    } else {
        fprintf(stderr, "failed to get stats from index: %s\n", 
          strerror(errno));
        return 0;
    }
}

int main(int argc, char **argv) {
    struct args argspace,
               *args;
    FILE *output;
    struct index *idx;
    struct index_result *results;
    unsigned int i;
    struct index_stats stats;
    char path[FILENAME_MAX + 1];
    struct timeval now, 
                   then;

    if (isatty(STDOUT_FILENO)) {
        output = stdout;
    } else {
        output = stderr;
    }

    if (!getcwd(path, FILENAME_MAX)) {
        fprintf(stderr, "failed to get current working directory\n");
        return EXIT_FAILURE;
    }

    if ((args = parse_args(argc, argv, &argspace, output, path))) {
        if (!args->index && !args->index_add && !args->stat) {
            /* load the index */
            if ((idx = index_load(args->prefix, args->memory, 
                  args->lopts, &args->lopt)) 
              && (results = malloc(sizeof(*results) * args->results))) {

                gettimeofday(&then, NULL);

                /* get word length for this index */
                if (!index_stats(idx, &stats)) {
                    index_delete(idx);
                    free_args(args);
                    return EXIT_FAILURE;
                }

                /* non-interactive mode */
                for (i = 0; args->list && args->list[i]; i++) {
                    if (!(search(idx, args->list[i], results, args->results,
                      args->first_result, stats.maxtermlen, args->sopts,
                      &args->sopt))) {
                        index_delete(idx);
                        free_args(args);
                        return EXIT_FAILURE;
                    }
                }

                if (args->qlist != stdin || !args->list || !args->list[0]) {
                    /* stream-sourced mode */
                    char querybuf[QUERYBUF + 1];

                    while (((args->qlist != stdin) 
                        || (printf("> ") && (fflush(stdout) == 0)))
                      && fgets(querybuf, QUERYBUF, args->qlist)) {
                        querybuf[QUERYBUF] = '\0';

                        if (!(search(idx, querybuf, results, args->results, 
                          args->first_result, stats.maxtermlen, args->sopts, 
                          &args->sopt))) {
                            index_delete(idx);
                            free_args(args);
                            return EXIT_FAILURE;
                        }
                    }
                }

                gettimeofday(&now, NULL);

                printf("%lu microseconds querying "
                  "(excluding loading/unloading)\n", 
                  (unsigned long int) (now.tv_usec - then.tv_usec 
                    + (now.tv_sec - then.tv_sec) * 1000000));

                index_delete(idx);
                free(results);
            } else {
                /* failed to load index or allocate memory */
                if (idx) {
                    index_delete(idx);
                    fprintf(output, "failed to allocate memory for results\n");
                } else {
                    if (error_has_msg())
                        fprintf(stderr, "%s\n", error_last_msg());
                    if (argc == 1) {
                        fprintf(output, "failed to load index '%s'\n\n", 
                          args->prefix);
                        print_usage(*argv, output, 0);
                    } else {
                        fprintf(output, "failed to load index '%s'\n", 
                          args->prefix);
                    }
                }
                free_args(args);
                return EXIT_FAILURE;
            }
        } else if (args->stat) {
            /* load the index */
            if ((idx = index_load(args->prefix, args->memory, 
                INDEX_LOAD_NOOPT, NULL))) {

                struct index_stats stats;
                struct index_expensive_stats estats;

                if (index_stats(idx, &stats) 
                  && index_expensive_stats(idx, &estats)) {
                    printf("distinct terms: %lu\n", stats.dterms);
                    printf("terms: %u %u\n", stats.terms_high, stats.terms_low);
                    printf("documents: %lu\n", stats.docs);
                    printf("average document length: %f\n", estats.avg_length);
                    printf("average document weight: %f\n", estats.avg_weight);
                    printf("average document terms: %f\n", estats.avg_words);
                    printf("sorted: %d\n", stats.sorted);
                    index_delete(idx);
                } else {
                    fprintf(stderr, "failed to get statistics\n");
                    index_delete(idx);
                    return EXIT_FAILURE;
                }
            } else {
                /* failed to load index or allocate memory */
                if (error_has_msg())
                    fprintf(stderr, "%s\n", error_last_msg());
                fprintf(output, "failed to load index '%s'\n\n", 
                  args->prefix);
                print_usage(*argv, output, 0);
                return EXIT_FAILURE;
            }
        } else {
            if (!args->list || !args->list[0]) {
                /* read from stdin */
                if (!fill_args_list(args, stdin, path)) {
                    fprintf(output, "unable to read files from stdin\n");
                    free_args(args);
                    return EXIT_FAILURE;
                } else if (!args->list || !args->list[0]) {
                    fprintf(output, "no input files specified\n");
                    free_args(args);
                    return EXIT_FAILURE;
                }
            }

            /* build or add to a index */
            if (!build(args, output)) {
                free_args(args);
                return EXIT_FAILURE;
            }
        }
        free_args(args);
    }

    return EXIT_SUCCESS;
}

