/* stack_1.c is a unit test for the stack module.  Its pretty
 * general, intended to test all of the individual stack functions.
 * Input format is:
 *
 * # comments
 * command params
 *
 * commands are:
 *
 *   new name sizehint: 
 *     create a new stack using size hint sizehint 
 *     name is the name of the test 
 *     the stack is a luint stack (for ease of testing)
 *
 *   push num ret:
 *     push the given number onto the stack, and verify that return
 *     value is ret
 *
 *   pop num ret:
 *     pop a value from the stack, verifying that the return value is
 *     ret and the number returned (ignored if ret != OK) is the same
 *     as num
 *
 *   peek num ret:
 *     peek at the top value in the stack, verifying that the return value is 
 *     ret and the number returned (ignored if ret != OK) is the same
 *     as num
 *
 *   print:
 *     print the contents of the stack
 *
 *   ls numitems [num]*:
 *     list the contents of the stack.  There should be
 *     numitems entries.  Each entry is a number entry in the stack.
 *
 * Commands should be put on a line by themselves, like this:
 *
 * # this is a comment, followed by a couple of commands
 * new
 *   case01 1
 * push 
 *   2 OK
 *
 * written nml 2004-07-26
 *
 */

#include "test.h"

#include "stack.h"
#include "str.h"
#include "getlongopt.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

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

enum stack_ret strtoret(char *str) {
    if (!str_casecmp(str, "ok")) {
        return STACK_OK;
    } else if (!str_casecmp(str, "enoent")) {
        return STACK_ENOENT;
    } else if (!str_casecmp(str, "enomem")) {
        return STACK_ENOMEM;
    } else {
        /* failed */
        return INT_MAX;
    }
}

const char *rettostr(enum stack_ret ret) {
    switch (ret) {
    case STACK_OK: return "ok";
    case STACK_ENOMEM: return "enomem";
    case STACK_ENOENT: return "enoent";
    case INT_MAX: return "lookup error";
    default: return "unknown";
    }
}

int test_file(FILE *fp, int argc, char **argv) {
    char *pos;
    unsigned long int tmp;
    struct params params = {0};
    struct stack *stack = NULL;
    char name[256];
    char ret[256];
    char buf[1024 + 1];

    if (!parse_params(argc, argv, &params)) {
        fprintf(stderr, "failed to parse params\n");
        return 0;
    }

    while (fgets((char *) buf, 1024, fp)) {
        str_rtrim(buf);
        pos = (char *) str_ltrim(buf);

        if (!str_casecmp(pos, "new")) {
            /* creating a new stack */

            if (stack) {
                stack_delete(stack);
            }

            /* read parameters */
            if ((fscanf(fp, "%255s %lu", name, &tmp) == 2)
              && (stack = stack_new((unsigned int) tmp))) {
                /* succeeded, do nothing */
                if (params.verbose) {
                    printf("%s: new stack with sizehint %lu\n", name, tmp);
                }
            } else {
                fprintf(stderr, "%s: failed to create stack\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "push")) {
            /* push a term onto the stack */

            if (!stack) { return 0; }

            /* read parameters */
            if (fscanf(fp, "%lu %256s", &tmp, ret) == 2) {
                if ((strtoret(ret) == stack_luint_push(stack, tmp))) {
                    if (params.verbose) {
                        printf("%s: pushed %lu, ret %s\n", name, tmp, ret);
                    }
                } else {
                    fprintf(stderr, "%s: failed to push %lu onto stack "
                      "(ret %s)\n", name, tmp, ret);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to push\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "pop")) {
            /* popping an entry from the stack */
            unsigned long int num;
            enum stack_ret tmpret;

            if (!stack) { return 0; }

            /* read parameters */
            if (fscanf(fp, "%lu %256s", &num, ret) == 2) {
                if ((strtoret(ret) == (tmpret = stack_luint_pop(stack, &tmp))) 
                  && (tmp == num)) {
                    if (params.verbose) {
                        printf("%s: popped %lu, ret %s\n", name, tmp, ret);
                    }
                } else if (tmpret == strtoret(ret)) {
                    fprintf(stderr, "%s: value mismatch (%lu vs %lu) "
                      "while popping value\n", name, tmp, num);
                    return 0;
                } else {
                    fprintf(stderr, "%s: return mismatch (%s vs %s) "
                      "while popping value\n", name, ret, rettostr(tmpret));
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to push\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "peek")) {
            /* peeking at an entry from the stack */
            unsigned long int num;
            enum stack_ret tmpret;

            if (!stack) { return 0; }

            /* read parameters */
            if (fscanf(fp, "%lu %256s", &num, ret) == 2) {
                if ((strtoret(ret) == (tmpret = stack_luint_peek(stack, &tmp))) 
                  && (tmp == num)) {
                    if (params.verbose) {
                        printf("%s: popped %lu, ret %s\n", name, tmp, ret);
                    }
                } else if (tmpret == strtoret(ret)) {
                    fprintf(stderr, "%s: value mismatch (%lu vs %lu) "
                      "while peeking at value\n", name, tmp, num);
                    return 0;
                } else {
                    fprintf(stderr, "%s: return mismatch (%s vs %s) "
                      "while peeking at value\n", name, ret, rettostr(tmpret));
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to peek\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "print")) {
            /* print all entries */
            unsigned int i;

            if (!stack) { return 0; }

            for (i = 0; i < stack_size(stack); i++) {
                if (stack_luint_fetch(stack, i, &tmp) == STACK_OK) {
                    printf("%lu\n", tmp);
                } else {
                    fprintf(stderr, "%s: failed to print\n", name);
                    return 0;
                }
            }
            printf("\n%u entries\n", stack_size(stack));
        } else if (!str_casecmp(pos, "ls")) {
            /* compare all entries */
            unsigned int i;
            unsigned int numitems;
            unsigned long int tmpnum;

            if (!stack) { return 0; }

            if (fscanf(fp, "%u", &numitems)) {
                for (i = 0; i < numitems; i++) {
                    if (fscanf(fp, "%lu ", &tmp)) {
                        if (params.verbose) {
                            printf("%s: ls checking %lu\n", name, tmp);
                        }

                        if (stack_luint_fetch(stack, i, &tmpnum) == STACK_OK) {
                            if (tmpnum != tmp) {
                                fprintf(stderr, "%s: ls comparison failed at "
                                  "position %u (%lu vs %lu)\n", name, i, tmp, 
                                  tmpnum);
                                return 0;
                            }
                        } else {
                            fprintf(stderr, "%s: ls comparison failed at "
                              "position %u (fetch failure %s)\n", name, i, 
                              rettostr(stack_luint_fetch(stack, i, &tmpnum)));
                            return 0;
                        }
                    } else {
                        fprintf(stderr, "%s: ls failed\n", name);
                        return 0;
                    }
                }
            } else {
                fprintf(stderr, "%s: ls failed\n", name);
                return 0;
            }
        } else if ((*pos != '#') && str_len(pos)) {
            fprintf(stderr, "%s: unknown command '%s'\n", name, pos);
            return 0;
        }
    }

    if (stack) {
        stack_delete(stack);
    }

    return 1;
}

