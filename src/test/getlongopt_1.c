/* getlongopt_1.c is a unit test for the getlongopt module.  It accepts a script
 * describing what tests to perform in the format:
 *
 *   opt longname shortname arg id -- adds an option to the current list, where:
 *     longname is the string to match (to match '--blah' use 'blah')
 *     shortname is the character to match (to match '-v' use v)
 *     arg is whether to require an argument, with 0 == no argument, 
 *       1 == required argument, 2 == optional argument
 *     id is the number to return when this option is matched
 *
 *   print -- prints the current options
 *
 *   clear -- clears the current options
 *
 *   parse options* -- prints out a parsed representation of options provided
 *
 *   check options* -- verifies that the parsed representation of the
 *     options is the same as the options listed on the next few lines.
 *     Next line must be number of options expected (by itself).
 *     Subsequent lines are of the form 'ret [id arg]' ret is the
 *     return value from getlongopt ('OK' or 'UNKNOWN' etc) and id is the id
 *     number of the expected option, and arg is the expected argument string.
 *     Comments are not currently supported within these lines
 *
 * written nml 2004-06-07
 *
 */

#include "firstinclude.h"

#include "test.h"

#include "getlongopt.h"
#include "str.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFSIZE 2048

int test_file(FILE *input, int argc, char **argv) {
    char buf[BUFSIZE];
    char buf2[BUFSIZE];
    char buf3[BUFSIZE];
    unsigned int opts = 0;
    unsigned int cap = 0,
                 i;
    enum getlongopt_ret ret;
    struct getlongopt_opt *opt = NULL;
    struct getlongopt *parser = NULL;
    int verbose = 0;
    unsigned int line = 0;

    if ((argc == 2) && (!str_cmp(argv[1], "-v"))) {
        verbose = 1;
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
                    return 0;
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
              && (parser = getlongopt_new(splitargs, (const char **) splitarg, 
                 opt, opts))) {

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
                return 0;
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
              && (parser = getlongopt_new(splitargs, (const char **) splitarg, 
                  opt, opts))) {

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
                        return 0;
                    }
                }

                free(splitarg);
                getlongopt_delete(parser);
            } else {
                perror(*argv);
                return 0;
            }
        } else if ((str_len(buf) > 0) && (*str_ltrim(buf) != '#')) {
            printf("command '%s' not understood\n", buf);
        }
    }

    if (opt) {
        for (i = 0; (i < opts) && opt; i++) {
            free((char *) opt[i].longname);
        }
        free(opt);
    }

    return 1;
}

