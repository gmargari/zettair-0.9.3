/* binsearch_1.c is a unit test for the binsearch module.  It operates
 * by sorting randomly (well, generated with rand) generated integers.
 *
 * input format is:
 *
 * # comment
 * num_items [rand_seed]
 *
 * where both num_items is the number of integers to sort, rand_seed
 * is the random seed to initialise generation, and both occur on the
 * same line.  Line length is limited to 4k.
 *
 * written nml 2003-10-17
 *
 */

#include "firstinclude.h"

#include "test.h"
#include "binsearch.h"
#include "getlongopt.h"
#include "str.h"
#include "lcrand.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

struct params {
    int verbose;
};

static int parse_params(int argc, char **argv, struct params *params) {
    struct getlongopt *parser;   /* option parser */
    int id;                      /* id of parsed option */
    const char *arg;             /* argument of parsed option */
    enum getlongopt_ret ret;     /* return value from option parser */

    struct getlongopt_opt opts[] = {
        {"input", '\0', GETLONGOPT_ARG_REQUIRED, 'i'},
        {"verbose", 'v', GETLONGOPT_ARG_NONE, 'v'}
    };

    if ((parser = getlongopt_new(argc - 1, (const char **) &argv[1], opts, 
        sizeof(opts) / sizeof(*opts)))) {
        /* succeeded, do nothing */
    } else {
        fprintf(stderr, "failed to initialise options parser\n");
        return 0;
    }

    while ((ret = getlongopt(parser, &id, &arg)) == GETLONGOPT_OK) {
        switch (id) {
        case 'i':
            /* ignore */
            break;

        case 'v':
            /* verbose */
            params->verbose = 1;
            break;

        default: assert(0);
        }
    }

    getlongopt_delete(parser);
    return 1;
}

#define BUFSIZE 4096

static int int_cmp(const void* one, const void* two) {
    const int* ione = one,
             * itwo = two;

    if (*ione < *itwo) {
        return -1;
    } else if (*ione > *itwo) {
        return 1;
    } else {
        return 0;
    }
}

static int test_arr(int *arr, unsigned long int entries) {
    unsigned int i;

    if (!entries) {
        return 1;
    }

    for (i = 1; i < entries; i++) {
        if (arr[i] < arr[i - 1]) {
            return 0;
        }
    }

    return 1;
}

static int test_bsearch(unsigned long int entries, unsigned long int seed, 
  int verbose) {
    int *arr,
        *ptr,
        i,
        r;
    struct lcrand *rand = lcrand_new(seed);

    if (!rand) {
        fprintf(stderr, "failed to init random number generator\n");
        return EXIT_FAILURE;
    }

    if (!(arr = malloc(sizeof(int) * entries))) {
        fprintf(stderr, "failed to allocate %lu bytes of memory\n", 
          sizeof(int) * entries);
        lcrand_delete(rand);
        return EXIT_FAILURE;
    }

    /* insert numbers into the array */
    for (i = 0; i < entries; i++) {
        r = lcrand(rand);
        ptr = (int*) binsearch(&r, arr, i, sizeof(int), int_cmp);
        memmove(ptr + 1, ptr, sizeof(int) * (i - (ptr - arr)));
        *ptr = r;
    }

    /* check the sorting of the array */
    lcrand_delete(rand);
    if (!test_arr(arr, entries)) {
        free(arr);
        return 0;
    } else {
        free(arr);
        return 1;
    }
}

int test_file(FILE *fp, int argc, char **argv) {
    char buf[BUFSIZE + 1];
    char *pos;
    struct params params = {0};
    unsigned long int entries,
                      seed;

    if (!parse_params(argc, argv, &params)) {
        fprintf(stderr, "failed to parse params\n");
        return 0;
    }

    while (fgets(buf, BUFSIZE, fp)) {
        str_rtrim((char*) buf);
        pos = (char*) str_ltrim((char*) buf);

        if ((*pos != '#') && str_len(pos)) {
            errno = 0;
            entries = strtol(pos, &pos, 10);
            if (!errno && (isspace(*pos) || (*pos == '\0'))) {
                pos = (char*) str_ltrim(pos);
                if (str_len(pos)) {
                    seed = strtol(pos, &pos, 10);
                    if (!errno && (isspace(*pos) || (*pos == '\0'))) {
                        if (!test_bsearch(entries, seed, params.verbose)) {
                            fprintf(stderr, "binsearch sort using entries %lu "
                              "seed %lu failed\n", entries, seed);
                            return 0;
                        } else if (params.verbose) {
                            printf("sort using %lu %lu succeeded\n", entries, 
                              seed);
                        }
                    }
                } else {
                    seed = time(NULL);
                    if (!test_bsearch(entries, seed, params.verbose)) {
                        fprintf(stderr, "binsearch sort using entries %lu "
                          "seed %lu failed\n", entries, seed);
                        return 0;
                    } else if (params.verbose) {
                        printf("sort using %lu %lu succeeded\n", entries, seed);
                    }
                }
            } else {
                fprintf(stderr, "can't understand line '%s'\n", buf);
                return 0;
            }
        }
    }

    return 1;
}

