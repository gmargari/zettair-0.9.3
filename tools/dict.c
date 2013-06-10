/* 
 *  Tool for querying the vocab.
 *
 *  Usage: 
 *    zet_dict -f <index-prefix>
 *    zet_dict -h                   # for help
 *
 *  This tool loads the vocabulary for the index specified by the
 *  "-f <prefix>" argument, or by default the index with the prefix
 *  "index".  It then reads queries from standard input, and writes
 *  results out to standard output.  In interactive mode, a prompt
 *  will be printed before each command.
 *
 *  Queries must be contained on a single line.  There is currently
 *  no way of extending a query across multiple lines.
 *
 *  The following queries are currently recognised:
 *
 *    veclen <term>
 *        Get the length in bytes of the inverted list for the
 *        term <term>
 *
 */

#include "firstinclude.h"

#include <ctype.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "iobtree.h"
#include "index.h"
#include "_index.h"
#include "vocab.h"
#include "vec.h"
#include "getlongopt.h"

#define QUERY_BUF_SIZE 4096
#define QUERY_MAX_ARGS 1024

static void print_usage(FILE * stream, const char * progname);
static int get_query(FILE * stream, char * buf, int buf_size);
static int do_query(char * query, struct index * idx);

int main(int argc, char ** argv) {
    const char * prefix = "index";
    struct getlongopt * opt_parser;
    int help_flag = 0;
    enum getlongopt_ret opt_ret;
    const char * arg;
    int id;
    unsigned int ind;
    struct index * idx;
    char query_buf[QUERY_BUF_SIZE];
    int ret;

    struct getlongopt_opt opts[] = {
        {"help", 'h', GETLONGOPT_ARG_NONE, 'h'},
        {"prefix", 'f', GETLONGOPT_ARG_REQUIRED, 'f'}
    };

    if ( (opt_parser = getlongopt_new(argc - 1, (const char **) &argv[1],
              opts, sizeof(opts) / sizeof(*opts))) == NULL) {
        fprintf(stderr, "failed to initialise option parser\n");
        return EXIT_FAILURE;
    }
      
    while ( (opt_ret = getlongopt(opt_parser, &id, &arg)) == GETLONGOPT_OK) {
        switch(id) {
        case 'h':
            help_flag = 1;
            break;

        case 'f':
            prefix = arg;
            break;

        default:
            assert(0);
        }
    }

    ind = getlongopt_optind(opt_parser) + 1;
    getlongopt_delete(opt_parser);

    if (opt_ret != GETLONGOPT_END) {
        if (opt_ret == GETLONGOPT_UNKNOWN) {
            fprintf(stderr, "unknown option '%s'\n", argv[ind]);
        } else if (opt_ret == GETLONGOPT_MISSING_ARG) {
            fprintf(stderr, "missing argument to option '%s'\n", argv[ind]);
        } else if (opt_ret == GETLONGOPT_ERR) {
            fprintf(stderr, "unexpected error parsing options (around '%s')\n",
              argv[ind]);
        }
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    if (ind != argc) {
        fprintf(stderr, "Trailing arguments\n");
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    if (help_flag) {
        print_usage(stdout, argv[0]);
        return EXIT_SUCCESS;
    }

    if ((idx = index_load(prefix, 0, INDEX_LOAD_NOOPT, NULL)) == NULL) {
        fprintf(stderr, "Error loading index with prefix '%s'\n", prefix);
        return EXIT_FAILURE;
    }

    while ( (ret = get_query(stdin, query_buf, QUERY_BUF_SIZE)) > 0
      && (ret = do_query(query_buf, idx)) > 0)
        ;
    if (ret == 0)
        return EXIT_SUCCESS;
    else
        return EXIT_FAILURE;
}

static int get_query(FILE * stream, char * buf, int buf_size) {
    int query_len;
    if (isatty(fileno(stream))) {
        fprintf(stdout, "> ");
        fflush(stdout);
    }
    if (fgets(buf, buf_size, stream) == NULL) {
        if (feof(stream))
            return 0;
        else
            return -1;
    }
    query_len = strlen(buf);
    if (buf[query_len - 1] == '\n')
        buf[query_len - 1] = '\0';
    return 1;
}

static int tokenize_query(char * query, char ** args, int args_size);

static int print_veclen(FILE * stream, char * term, struct index * idx);

static int print_vocab_size(FILE * stream, struct index * idx);

static int do_query(char * query, struct index * idx) {
    char * args[QUERY_MAX_ARGS];
    int num_args = tokenize_query(query, args, QUERY_MAX_ARGS);

    if (num_args < 0) {
        fprintf(stderr, "Malformed query\n");
        return 1;
    } else if (num_args == 0) {
        return 1;
    }

    if (strcmp(args[0], "veclen") == 0) {
        if (num_args != 2) {
            fprintf(stderr, "'veclen' requires single term as argument\n");
            return 1;
        } else {
             print_veclen(stdout, args[1], idx);
        }
    } else if (strcmp(args[0], "vocab_size") == 0) {
        if (num_args != 1) {
            fprintf(stderr, "'vocab_size' takes no arguments\n");
            return 1;
        } else {
            print_vocab_size(stdout, idx);
        }
    } else if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "quit") == 0) {
        return 0;
    } else {
        fprintf(stderr, "Unknown query '%s'\n", args[0]);
    }
    return 1;
}

static int print_vocab_size(FILE * stream, struct index * idx) {
    struct index_stats stats;
    index_stats(idx, &stats);
    fprintf(stream, "%lu\n", stats.dterms);
    return 0;
}

static int get_vocab_vector(struct iobtree * vocab, char * term,
  struct vocab_vector * vocab_vector) {
    void * data;
    unsigned int veclen;
    struct vec v;

    data = iobtree_find(vocab, term, strlen(term), 0, &veclen);
    if (data == NULL)  /* XXX how to detect error? */
        return 0;
    v.pos = data;
    v.end = v.pos + veclen;
    if (!(vocab_decode(vocab_vector, &v))) 
        return -1;
    return 1;
}

static int print_veclen(FILE * stream, char * term, struct index * idx) {
    struct iobtree * vocab = idx->vocab;
    struct vocab_vector vocab_vector;
    int ret;

    if ( (ret = get_vocab_vector(vocab, term, &vocab_vector)) == 1) {
        fprintf(stream, "%lu\n", vocab_vector.size);
    } else if (ret == 0) {
        fprintf(stream, "-1\n");
    } else {
        fprintf(stderr, "Error reading vocab entry for '%s'\n", term);
    }
    return ret;
}

static int tokenize_query(char * query, char ** args, int args_size) {
    char * qp = query;
    int num_args = 0;

    while (*qp != '\0') {
        while (isspace(*qp)) {
            *qp = '\0';
            qp++;
        }
        if (*qp != '\0') {
            if (num_args == args_size)
                return -1;
            args[num_args] = qp;
            num_args++;
        }
        while (!isspace(*qp) && *qp != '\0') {
            qp++;
        }
    }
    return num_args;
}

static void print_usage(FILE * stream, const char * progname) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "\t%s [-f prefix]\n", progname);
    fprintf(stderr, "\t%s -h     # for help\n", progname);
}
