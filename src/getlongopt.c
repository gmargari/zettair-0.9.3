/* getlongopt.c implements option parsing as specified in getlongopt.h
 * and following the principles put forward in the Single Unix
 * Specification v3 in the section on getopt the function.
 *
 * written nml 2003-04-11
 *
 */

/* FIXME: seems to accept args for NONE types */

#include "firstinclude.h"

#include "getlongopt.h"

#include "str.h"

#include <errno.h>
#include <stdlib.h>

/* all information needed to parse arguments */
struct getlongopt {
    unsigned int argc;                   /* size of array of arguments */
    const char **argv;                   /* array of arguments */

    struct getlongopt_opt *optstring;    /* array of options */
    unsigned int optstrings;             /* size of array of options */

    unsigned int optind;                 /* argument we're up to */
    unsigned int pos;                    /* position in argument we're up to */
};

struct getlongopt *getlongopt_new(unsigned int argc, const char **argv,
  struct getlongopt_opt *optstring, unsigned int optstrings) {
    struct getlongopt *opts = malloc(sizeof(*opts));
    unsigned int i;

    if (opts) {
        opts->argc = argc;
        opts->argv = argv;
        opts->optstring = optstring;
        opts->optstrings = optstrings;
        
        opts->optind = 0;
        opts->pos = 0;

        /* check that the argument values are valid */
        for (i = 0; i < optstrings; i++) {
            if ((optstring[i].argument != GETLONGOPT_ARG_NONE)
              && (optstring[i].argument != GETLONGOPT_ARG_REQUIRED)
              && (optstring[i].argument != GETLONGOPT_ARG_OPTIONAL)) {
                errno = EINVAL;
                free(opts);
                return NULL;
            }
        }
    }

    return opts;
}

void getlongopt_delete(struct getlongopt *opts) {
    free(opts);
}

unsigned int getlongopt_optind(struct getlongopt *opts) {
    return opts->optind;
}

static int matchshort(struct getlongopt *opts, unsigned int *optind, 
  unsigned int *optpos, int *optid, const char **optarg, int recurse);

/* internal function to match a long argument and take appropriate action */
static int matchlong(struct getlongopt *opts, unsigned int *optind, 
  int *optid, const char **optarg, int recurse) {
    unsigned int i,
                 tmpoptind;

    for (i = 0; i < opts->optstrings; i++) {
        /* check for '--opt arg' syntax */
        if (opts->optstring[i].longname 
          && !str_cmp(opts->optstring[i].longname, &opts->argv[*optind][2])) {

            switch (opts->optstring[i].argument) {
            case GETLONGOPT_ARG_NONE:
                /* its easy if no argument is allowed */
                *optarg = NULL;
                (*optind)++;
                *optid = opts->optstring[i].id;
                return GETLONGOPT_OK;

            case GETLONGOPT_ARG_REQUIRED:
                /* make sure that there actually is an argument following */
                *optid = opts->optstring[i].id;
                if ((++(*optind) < opts->argc) && opts->argv[*optind]) {
                    *optarg = opts->argv[(*optind)++];
                    return GETLONGOPT_OK;
                } else {
                    return GETLONGOPT_MISSING_ARG;
                }
                break;

            case GETLONGOPT_ARG_OPTIONAL:
                /* check for arg and match */
                tmpoptind = *optind + 1;

                if ((tmpoptind >= opts->argc) || !opts->argv[tmpoptind] 
                  || !recurse) {
                    /* matched without argument */
                    *optid = opts->optstring[i].id;
                    *optarg = NULL;
                    (*optind)++;
                    return GETLONGOPT_OK;
                }

                if (*opts->argv[tmpoptind] == '-') {
                    if (opts->argv[tmpoptind][1] == '-') {

                        /* check if we could use next item as an option.  If so,
                         * don't match if as an argument */
                        if ((matchlong(opts, &tmpoptind, optid, optarg, 0) 
                            == GETLONGOPT_OK)

                          /* check for '--', don't match as arg */
                          || !opts->argv[tmpoptind][2]) {
                            /* matched without argument */
                            *optid = opts->optstring[i].id;
                            *optarg = NULL;
                            (*optind)++;
                            return GETLONGOPT_OK;
                        }
                    } else {
                        unsigned int tmpoptpos = 1;
                        if (matchshort(opts, &tmpoptind, &tmpoptpos, 
                            optid, optarg, 0) == GETLONGOPT_OK) {
                            /* matched without argument */
                            *optid = opts->optstring[i].id;
                            *optarg = NULL;
                            (*optind)++;
                            return GETLONGOPT_OK;
                        }
                    }
                }

                /* matched with argument */
                *optid = opts->optstring[i].id;
                (*optind)++;
                *optarg = opts->argv[*optind];
                (*optind)++;
                return GETLONGOPT_OK;

            default:
                return GETLONGOPT_ERR;
            };

        /* check for --opt=arg syntax */
        } else if (opts->optstring[i].longname 
          && !str_ncmp(opts->optstring[i].longname, 
              &opts->argv[*optind][2], str_len(opts->optstring[i].longname)) 
            && (opts->argv[*optind][2 + str_len(opts->optstring[i].longname)] 
              == '=')) {
            *optid = opts->optstring[i].id;

            switch (opts->optstring[i].argument) {
            case GETLONGOPT_ARG_NONE:
                /* no argument is allowed */
                *optarg = NULL;
                (*optind)++;
                return GETLONGOPT_OK;

            case GETLONGOPT_ARG_REQUIRED:
            case GETLONGOPT_ARG_OPTIONAL:
                *optarg = &opts->argv[*optind][3 
                  + str_len(opts->optstring[i].longname)];
                (*optind)++;
                return GETLONGOPT_OK;
                break;

            default:
                return GETLONGOPT_ERR;
            };
        }
    }

    /* couldn't match argument */
    return GETLONGOPT_UNKNOWN;
}

/* internal function to match a short argument and take appropriate action */
static int matchshort(struct getlongopt *opts, unsigned int *optind, 
  unsigned int *optpos, int *optid, const char **optarg, int recurse) {
    unsigned int i,
                 tmpoptind;

    for (i = 0; i < opts->optstrings; i++) {
        if (opts->optstring[i].shortname == opts->argv[*optind][*optpos]) {
            *optid = opts->optstring[i].id;

            switch (opts->optstring[i].argument) {
            case GETLONGOPT_ARG_NONE:
                /* no argument is allowed */
                *optarg = NULL;
                (*optpos)++;
                return GETLONGOPT_OK;

            case GETLONGOPT_ARG_REQUIRED:
                if (opts->argv[*optind][++(*optpos)]) {
                    /* there's still more options in this argument */
                    return GETLONGOPT_MISSING_ARG;
                }

                /* make sure that there actually is an argument following */
                if ((++(*optind) < opts->argc) && opts->argv[*optind]) {
                    *optarg = opts->argv[(*optind)++];
                    *optpos = 0;
                    return GETLONGOPT_OK;
                } else {
                    return GETLONGOPT_MISSING_ARG;
                }
                break;

            case GETLONGOPT_ARG_OPTIONAL:
                /* check for arg and match */
                tmpoptind = *optind + 1;

                if ((tmpoptind >= opts->argc) || !opts->argv[tmpoptind] 
                  || !recurse || opts->argv[*optind][*optpos + 1]) {
                    /* matched without argument */
                    *optid = opts->optstring[i].id;
                    *optarg = NULL;
                    (*optpos)++;
                    return GETLONGOPT_OK;
                }

                if (*opts->argv[tmpoptind] == '-') {
                    if (opts->argv[tmpoptind][1] == '-') {
                        if (matchlong(opts, &tmpoptind, optid, optarg, 0) 
                            == GETLONGOPT_OK) {
                            /* matched without argument */
                            *optid = opts->optstring[i].id;
                            *optarg = NULL;
                            (*optpos)++;
                            return GETLONGOPT_OK;
                        }
                    } else {
                        unsigned int tmpoptpos = 1;
                        if (matchshort(opts, &tmpoptind, &tmpoptpos, 
                            optid, optarg, 0) == GETLONGOPT_OK) {
                            /* matched without argument */
                            *optid = opts->optstring[i].id;
                            *optarg = NULL;
                            (*optpos)++;
                            return GETLONGOPT_OK;
                        }
                    }
                }

                /* matched with argument */
                *optid = opts->optstring[i].id;
                (*optind)++;
                *optarg = opts->argv[*optind];
                (*optind)++;
                *optpos = 0;
                return GETLONGOPT_OK;

            default:
                return GETLONGOPT_ERR;
            };
        }
    }

    /* couldn't match argument */
    return GETLONGOPT_UNKNOWN;
}

int getlongopt(struct getlongopt *opts, int *optid, const char **optarg) {

    /* catch a couple of silly cases */
    if ((opts->optind >= opts->argc) || !opts->argv[opts->optind]) {
        /* came to the end of the arguments */
        return GETLONGOPT_END;
    }

    if (opts->pos && opts->argv[opts->optind][opts->pos]) {
        /* have previously parsed a short option, match next charater
         * against other short options */
        return matchshort(opts, &opts->optind, &opts->pos, optid, optarg, 1);
    } else if (opts->pos) {
        /* came to the end of the current string, advance to the next */
        opts->pos = 0;
        opts->optind++;
    }

    if (opts->argv[opts->optind]) {
        if (*opts->argv[opts->optind] == '-') {
            if (opts->argv[opts->optind][1] == '-') {
                if (opts->argv[opts->optind][2]) {
                    /* its a long option, try and match it */
                    return matchlong(opts, &opts->optind, optid, optarg, 1);
                } else {
                    /* came to '--', that ends parsing */
                    return GETLONGOPT_END;
                }
            } else if (opts->argv[opts->optind][1]) {
                /* its a short option, try and match it */
                opts->pos = 1;
                return matchshort(opts, &opts->optind, &opts->pos, optid, 
                  optarg, 1);
            } else {
                /* got a single '-', which is an argument and ends parsing */
                return GETLONGOPT_END;
            }
        } else {
            /* not an option, that ends parsing */
            return GETLONGOPT_END;
        }
    } else {
        /* came to the end of the arguments */
        return GETLONGOPT_END;
    }
}

#ifdef GETLONGOPT_TEST

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#define BUFSIZE 2048

int main(int argc, char *argv[]) {
    char buf[BUFSIZE];
    char buf2[BUFSIZE];
    char buf3[BUFSIZE];
    unsigned int opts = 0;
    unsigned int cap = 0,
                 i;
    enum getlongopt_ret ret;
    struct getlongopt_opt *opt = NULL;
    struct getlongopt *parser = NULL;
    FILE *input;
    int verbose = 0;
    unsigned int line = 0;

    if (argc == 1) {
        input = stdin;
    } else if (argc == 2) {
        if ((input = fopen(argv[1], "rb"))) {

        } else {
            perror(*argv);
            return EXIT_FAILURE;
        }
    } else if ((argc == 3) && (!str_cmp(argv[1], "-v"))) {
        if ((input = fopen(argv[2], "rb"))) {
            verbose = 1;
        } else {
            perror(*argv);
            return EXIT_FAILURE;
        }
    } else {
        fprintf(stderr, "too many args\n");
        return EXIT_FAILURE;
    }

    while (fgets(buf, BUFSIZE, input)) {
        line++;
        str_rtrim(buf);
        if (verbose) {
            printf("> %s\n", buf);
        }
        if (!str_ncmp(buf, "option", str_len("option"))) {
            /* add new option */
            if (opts >= cap) {
                void *ptr;
                if ((ptr = realloc(opt, (2 * cap + 1) * sizeof(*opt)))) {
                    opt = ptr;
                    cap = 2 * cap + 1;
                } else {
                    perror(*argv);
                    return EXIT_FAILURE;
                }
            }

            if ((sscanf(buf + str_len("option"), "%s %c %d %d", buf2, 
                &opt[opts].shortname, (int*) &opt[opts].argument, 
                &opt[opts].id) == 4) 
              && (opt[opts].longname = str_dup(buf2))) {
                if (verbose) {
                    printf("added %s %c %s id %d\n", opt[opts].longname, 
                      opt[opts].shortname,
                      opt[opts].argument == GETLONGOPT_ARG_NONE ? "no arg" 
                        : opt[opts].argument == GETLONGOPT_ARG_REQUIRED 
                          ? "arg req" 
                            : opt[opts].argument == GETLONGOPT_ARG_OPTIONAL 
                              ? "arg opt" : "arg unknown", opt[opts].id);
                }
                opts++;
            } else {
                buf[str_len(buf) - 1] = '\0';
                fprintf(stderr, "failed to parse '%s'\n", buf);
            }
        } else if (!str_cmp(buf, "print")) {
            for (i = 0; i < opts; i++) {
                printf("%s %c %s (%d) id %d\n", opt[i].longname, 
                  opt[i].shortname,
                  opt[i].argument == GETLONGOPT_ARG_NONE ? "no arg" 
                    : opt[i].argument == GETLONGOPT_ARG_REQUIRED ? "arg req" 
                      : opt[i].argument == GETLONGOPT_ARG_OPTIONAL ? "arg opt" 
                        : "arg unknown", opt[i].argument, opt[i].id);
            }
        } else if (!str_cmp(buf, "clear")) {
            for (i = 0; i < opts; i++) {
                free((char *) opt[i].longname);
            }
            opts = 0;
            if (verbose) {
                printf("cleared\n");
            }
        } else if (!str_ncmp(buf, "parse", str_len("parse"))) {
            /* parse the line */
            unsigned int splitargs;
            char **splitarg;
            int id;
            const char *arg;

            if ((splitarg 
              = str_split(buf + str_len("parse"), " \t\n\f\r", &splitargs))
              && (parser = getlongopt_new(splitargs, splitarg, opt, opts))) {

                while ((ret = getlongopt(parser, &id, &arg)) == GETLONGOPT_OK) {
                    printf("parsed id %d (", id);
                    for (i = 0; i < opts; i++) {
                        if (opt[i].id == id) {
                            printf("%s", opt[i].longname);
                        }
                    }
                    printf(") ");
                    if (isprint(id)) {
                        printf("(%c)", id);
                    }

                    if (arg) {
                        printf("arg '%s'\n", arg);
                    } else {
                        printf("\n");
                    }
                }

                for (i = getlongopt_optind(parser); i < splitargs; i++) {
                    printf("parsed arg %s\n", splitarg[i]);
                }

                if (ret != GETLONGOPT_END) {
                    fprintf(stderr, "didn't end, got %d instead\n", ret);
                }

                free(splitarg);
                getlongopt_delete(parser);
            } else {
                perror(*argv);
                return EXIT_FAILURE;
            }
        } else if (!str_ncmp(buf, "check", str_len("check"))) {
            /* parse the line */
            unsigned int splitargs;
            char **splitarg;
            int id,
                scan_id;
            const char *arg;

            if ((splitarg 
              = str_split(buf + str_len("check"), " \t\n\f\r", &splitargs))
              && (parser 
                = getlongopt_new(splitargs, splitarg, opt, opts))) {

                ret = GETLONGOPT_OK;

                while ((ret == GETLONGOPT_OK) 
                  && ((ret = getlongopt(parser, &id, &arg)), 1)
                  && (fgets(buf3, BUFSIZE, input))
                  && (++line)
                  && (sscanf(buf3, "%s", buf2) == 1)) {

                    if (!str_ncmp(buf2, "OK", str_len("OK"))) {

                        if ((ret == GETLONGOPT_OK)
                          && (buf2[0] = '\0', 1)
                          && (sscanf(buf3, "%s %d %s", buf, &scan_id, buf2)
                            >= 2)) {
                            if ((scan_id == id) 
                              && ((!arg && !str_len(buf2)) 
                                || (!str_cmp(arg, buf2)))) {

                                /* matched */
                                if (verbose) {
                                    printf("checked OK %d '%s'\n", id, arg);
                                }
                            } else {
                                if (scan_id != id) {
                                    fprintf(stderr, "expecting id %d, got %d\n",
                                      id, scan_id);
                                }
                                if (str_cmp(arg, buf2)) {
                                    fprintf(stderr, 
                                      "expecting arg '%s', got '%s'\n", buf2, 
                                      arg);
                                } 
                            }
                        } else {
                            fprintf(stderr, 
                              "expected %s, got %d line %u\n", buf2, ret,
                              line);
                        }
                    } else if (!str_cmp(buf2, "END")) {
                        if (ret != GETLONGOPT_END) {
                            fprintf(stderr, 
                              "expected %s, got %d line %u\n", buf2, ret,
                              line);
                        } else if (verbose) {
                            printf("checked %s\n", buf2);
                        }
                    } else if (!str_cmp(buf2, "UNKNOWN")) {
                        if (ret != GETLONGOPT_UNKNOWN) {
                            fprintf(stderr, 
                              "expected %s, got %d line %u\n", buf2, ret,
                              line);
                        } else if (verbose) {
                            printf("checked %s\n", buf2);
                        }
                    } else if (!str_cmp(buf2, "MISSING_ARG")) {
                        if (ret != GETLONGOPT_MISSING_ARG) {
                            fprintf(stderr, 
                              "expected %s, got %d line %u\n", buf2, ret,
                              line);
                        } else if (verbose) {
                            printf("checked %s\n", buf2);
                        }
                    } else if (!str_cmp(buf2, "ERR")) {
                        if (ret != GETLONGOPT_ERR) {
                            fprintf(stderr, 
                              "expected %s, got %d line %u\n", buf2, ret,
                              line);
                        } else if (verbose) {
                            printf("checked %s\n", buf2);
                        }
                    } else {
                        fprintf(stderr, "unknown error '%s'\n", buf2);
                        return EXIT_FAILURE;
                    }
                }

                free(splitarg);
                getlongopt_delete(parser);
            } else {
                perror(*argv);
                return EXIT_FAILURE;
            }
        } else if ((str_len(buf) > 0) && (*str_ltrim(buf) != '#')) {
            printf("command '%s' not understood\n", buf);
        }
    }

    if (input != stdin) {
        fclose(input);
    }

    if (opt) {
        for (i = 0; (i < opts) && opt; i++) {
            free((char *) opt[i].longname);
        }
        free(opt);
    }

    return EXIT_SUCCESS;
}

#endif


