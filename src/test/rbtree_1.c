/* rbtree_1.c is a unit test for the rbtree module.  Its pretty
 * general, intended to test all of the individual rbtree functions.
 * Input format is:
 *
 * # comments
 * command params
 *
 * commands are:
 *
 * insert, find, remove, find_insert, print, ls, clear
 *
 *   new name: 
 *     create a new rbtree containing luints, ordered naturally.
 *     name is the name of the test 
 *
 *   insert num datanum ret:
 *     inserts the given number (num) into the current rbtree, verifying that
 *     the return value is ret
 *
 *   remove num ret:
 *     removes the given number (num) from the current rbtree, verifying that
 *     the return value is ret
 *
 *   find num datanum ret:
 *     finds the given number (num) in the current rbtree, verifying that
 *     the return value is ret, and thats its data member is datanum
 *
 *   findinsert num datanum ret:
 *     finds the given number (num) in the rbtree.  If found, the data member
 *     is verfied as datanum, if not then it is inserted.  Return value is
 *     verified as ret.
 *
 *   findnear num foundnum datanum ret:
 *     finds the smallest key such that its smaller than or equal to the given
 *     num.  If found, the found key and data member are verified as foundnum,
 *     datanum.  Return value is verified as ret.
 *
 *   print order:
 *     print the contents of the rbtree.  order is 
 *     ("rev_"? ("in" | "pre" | "post")) to indicate inorder, post-order,
 *     pre-order, optionally reversed, down the tree.
 *
 *   ls order numitems [num datanum]*:
 *     list the contents of the rbtree.  There should be
 *     numitems entries.  Each entry is a number entry in the rbtree.  They must
 *     be in sorted order to pass.  order same as print command.
 *
 *   newrand name seed items iterations:
 *     perform random validation of rbtree by creating a new rbtree, seeding the
 *     random number generator with seed (or "time" to seed from the clock),
 *     inserting items number of items into the tree, and then alternately
 *     selecting an item for deletion and inserting a random replacement.  This
 *     occurs iterations times
 *
 * Commands should be put on a line by themselves, like this:
 *
 * # this is a comment, followed by a couple of commands
 * new
 *   case01 1
 * insert 
 *   2 2 ok
 *
 * written nml 2004-07-26
 *
 */

#include "test.h"

#include "rbtree.h"
#include "str.h"
#include "getlongopt.h"
#include "lcrand.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* dodgy internal declaration for internal rbtree function */
void rbtree_print(struct rbtree *rb, FILE *output);

struct params {
    int verbose;
};

int ulongcmp(const void *vone, const void *vtwo) {
    const unsigned long int *one = vone,
                            *two = vtwo;

    if (*one < *two) {
        return -1;
    } else if (*one > *two) {
        return 1;
    } else {
        return 0;
    }
}

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

enum rbtree_ret strtoret(char *str) {
    if (!str_casecmp(str, "ok")) {
        return RBTREE_OK;
    } else if (!str_casecmp(str, "enoent")) {
        return RBTREE_ENOENT;
    } else if (!str_casecmp(str, "enomem")) {
        return RBTREE_ENOMEM;
    } else if (!str_casecmp(str, "eexist")) {
        return RBTREE_EEXIST;
    } else if (!str_casecmp(str, "einval")) {
        return RBTREE_EINVAL;
    } else {
        /* failed */
        return INT_MAX;
    }
}

const char *rettostr(enum rbtree_ret ret) {
    switch (ret) {
    case RBTREE_OK: return "ok";
    case RBTREE_ENOMEM: return "enomem";
    case RBTREE_ENOENT: return "enoent";
    case INT_MAX: return "lookup error";
    default: return "unknown";
    }
}

int luintcmp(const void *key1, const void *key2) {
    const unsigned long int *lu1 = key1,
                            *lu2 = key2;

    if (*lu1 < *lu2) {
        return -1;
    } else if (*lu1 > *lu2) {
        return 1;
    } else {
        return 0;
    }
}

int test_file(FILE *fp, int argc, char **argv) {
    char *pos;
    unsigned long int key,
                      data,
                      tmpkey,
                      tmpdata,
                      *ptr;
    struct params params = {0};
    struct rbtree *rbtree = NULL;
    char name[256];
    char ret[256];
    char buf[1024 + 1];
    char orderbuf[11];
    enum rbtree_ret tmpret;
    struct lcrand *rand = NULL;

    if (!parse_params(argc, argv, &params)) {
        fprintf(stderr, "failed to parse params\n");
        return 0;
    }

    while (fgets((char *) buf, 1024, fp)) {
        str_rtrim(buf);
        pos = (char *) str_ltrim(buf);

        if (!str_casecmp(pos, "newrand")) {
            unsigned int seed,
                         items,
                         iterations,
                         i,
                         curr;
            unsigned long int *arr;
            char seedbuf[20];

            /* create a new btree, fill it with random stuff, alternately delete
             * and insert random stuff, delete */

            if (rbtree) {
                if (params.verbose) {
                    printf("\n");
                }
                rbtree_delete(rbtree);
                rbtree = NULL;
                lcrand_delete(rand);
                rand = NULL;
            }

            /* read parameters */
            if (fscanf(fp, "%255s %20s %u %u", name, seedbuf, &items, 
                &iterations) == 4) {

                if (!str_casecmp(seedbuf, "time")) {
                    seed = time(NULL);
                } else {
                    seed = atoi(seedbuf);
                }

                if (!(rand = lcrand_new(seed))) {
                    fprintf(stderr, "%s: failed to init random "
                      "number generator\n", name);
                    return 0;
                }

                if (!(rbtree = rbtree_luint_new())) {
                    fprintf(stderr, 
                      "%s (seed %u): failed to get new rbtree\n", 
                      name, seed);
                    return 0;
                }

                if (!(arr = malloc(sizeof(*arr) * (items + 1)))) {
                    fprintf(stderr, 
                      "%s (seed %u): failed to get memory for %u items\n", 
                      name, seed, items);
                    return 0;
                }

                if (params.verbose) {
                    printf("%s: new rbtree rand, seed %u\n", name, seed);
                }

                memset(arr, 0, items * sizeof(*arr));
                /* insert 'items' number of entries */
                for (i = 0; i < items; i++) {
                    enum rbtree_ret ret;
                    unsigned long int *tmp;

                    do {
                        curr = (unsigned int) (((float) items * 10) 
                            * lcrand(rand) / (LCRAND_MAX + 1.0));
                    } while (rbtree_luint_luint_find(rbtree, curr, &tmp) 
                      == RBTREE_OK);

                    arr[i] = curr;
                    ret = rbtree_luint_luint_insert(rbtree, curr, curr);

                    if (params.verbose) {
                        printf("%s: insert\n%s: \t %u %u ok\n", name, name, 
                          curr, curr);
                    }

                    if (ret != RBTREE_OK) {
                        fprintf(stderr, 
                          "%s (seed %u): failed to insert (%d)\n", name, seed,
                          ret);
                        return 0;
                    }
                }

                assert(rbtree_size(rbtree) == items);

                for (i = 0; i < iterations; i++) {
                    enum rbtree_ret ret;
                    struct rbtree_iter *iter;
                    unsigned long int *tmp;

                    /* add */

                    do {
                        curr = (unsigned int) (((float) items * 10) 
                            * lcrand(rand) / (LCRAND_MAX + 1.0));
                    } while (rbtree_luint_luint_find(rbtree, curr, &tmp) 
                      == RBTREE_OK);

                    arr[items] = curr;
                    ret = rbtree_luint_luint_insert(rbtree, curr, curr);

                    if (params.verbose) {
                        printf("%s: insert\n%s: \t %u %u ok\n", name, name, 
                          curr, curr);
                    }

                    if (ret != RBTREE_OK) {
                        fprintf(stderr, "%s (seed %u): failed to insert (%d)\n",
                          name, seed, ret);
                        return 0;
                    }

                    /* delete */

                    curr = (unsigned int) (((float) items) 
                        * lcrand(rand) / (LCRAND_MAX + 1.0));
                    assert(curr >= 0 && curr < items);

                    if (params.verbose) {
                        printf("%s: remove\n%s: \t%lu %lu ok\n", name, name, 
                          arr[curr], arr[curr]);
                    }

                    ret 
                      = rbtree_luint_luint_remove(rbtree, arr[curr], &tmpdata);
                    if ((ret != RBTREE_OK) || (tmpdata != arr[curr])) {
                        fprintf(stderr, 
                          "%s (seed %u): rand removal of %lu failed, (%d)\n", 
                          name, seed, arr[curr], ret);
                        return 0;
                    }

                    arr[curr] = arr[items];

                    /* compare (every so often) */
                    if ((i % 10 == 0) || (i + 1 == items)) {
                        qsort(arr, items, sizeof(*arr), ulongcmp);
                        if ((iter 
                          = rbtree_iter_new(rbtree, RBTREE_ITER_INORDER, 0))) {
                            unsigned long int *ptr;
                            unsigned int count = 0;

                            while ((tmpret 
                              = rbtree_iter_luint_luint_next(iter, &key, &ptr))
                                == RBTREE_OK) {
                                assert(count < items);

                                if ((key == *ptr) && arr[count++] == key) {
                                    /* succeeded, do nothing */
                                } else {
                                    fprintf(stderr, 
                                      "%s (seed %u): compare failed "
                                        "(%lu, %lu vs %lu, %lu\n", 
                                      name, seed, key, *ptr, arr[count - 1], 
                                      arr[count - 1]);

                                    for (i = 0; i < items; i++) {
                                        printf("%u: %lu\n", i, arr[i]);
                                    }
                                    printf("\n");
                                    rbtree_print(rbtree, stdout);
                                    return 0;
                                }
                            }

                            if (count != items) {
                                fprintf(stderr, 
                                  "%s (seed %u): compare failed, wrong number "
                                  "of items (%u vs %u)\n", 
                                  name, seed, count, items);
                                return 0;
                            }
                            rbtree_iter_delete(iter);
                        } else {
                            fprintf(stderr, 
                              "%s (seed %u): failed to get iterator over "
                                "rbtree to compare\n", name, seed);
                            return 0;

                        }

                        if (params.verbose) {
                            printf("%s: successful comparison\n", name);
                        }
                    }
                }

                free(arr);
                rbtree_delete(rbtree);
                lcrand_delete(rand);
                rbtree = NULL;
                rand = NULL;
            } else {
                fprintf(stderr, "%s: failed to read newrand parameters\n", 
                  name);
                return 0;
            }
        } else if (!str_casecmp(pos, "new")) {
            /* creating a new rbtree */

            if (rbtree) {
                if (params.verbose) {
                    printf("\n");
                }
                rbtree_delete(rbtree);
            }

            /* read parameters */
            if ((fscanf(fp, "%255s", name) == 1)
              && (rbtree = rbtree_luint_new(luintcmp))) {
                /* succeeded, do nothing */
                if (params.verbose) {
                    printf("%s: new rbtree\n", name);
                }
            } else {
                fprintf(stderr, "%s: failed to create rbtree\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "insert")) {
            /* insert a number into the rbtree */

            if (!rbtree) { return 0; }

            /* read parameters */
            if (fscanf(fp, "%lu %lu %256s", &key, &data, ret) == 3) {
                if ((strtoret(ret) 
                  == rbtree_luint_luint_insert(rbtree, key, data))) {
                    if (params.verbose) {
                        printf("%s: inserted %lu %lu, ret %s\n", name, key, 
                          data, ret);
                    }
                } else {
                    fprintf(stderr, "%s: failed to insert %lu %lu into rbtree "
                      "(ret %s)\n", name, key, data, ret);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to insert\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "remove")) {
            /* remove an entry from the rbtree */
            enum rbtree_ret tmpret;

            if (!rbtree) { return 0; }

            /* read parameters */
            if (fscanf(fp, "%lu %lu %256s", &key, &data, ret) == 3) {
                if ((strtoret(ret) == (tmpret 
                    = rbtree_luint_luint_remove(rbtree, key, &tmpdata)))
                  && ((tmpret != RBTREE_OK) || (data == tmpdata))) {
                    if (params.verbose) {
                        printf("%s: removed %lu %lu, ret %s\n", name, key, 
                          data, ret);
                    }
                } else if (tmpret == strtoret(ret)) {
                    fprintf(stderr, "%s: value mismatch (%lu vs %lu) "
                      "while removing key %lu\n", name, data, tmpdata, key);
                    return 0;
                } else {
                    fprintf(stderr, "%s: return mismatch (%s vs %s) "
                      "while removing key %lu\n", name, ret, rettostr(tmpret), 
                      key);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to remove\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "find")) {
            /* find an entry in the rbtree */
            enum rbtree_ret tmpret;

            if (!rbtree) { return 0; }

            /* read parameters */
            if (fscanf(fp, "%lu %lu %256s", &key, &data, ret) == 3) {
                ptr = &tmpdata;
                tmpdata = 0;
                if ((strtoret(ret) == (tmpret 
                    = rbtree_luint_luint_find(rbtree, key, &ptr))) 
                  && (*ptr == data)) {
                    if (params.verbose) {
                        printf("%s: found %lu %lu, ret %s\n", name, key, data,
                          ret);
                    }
                } else if (tmpret == strtoret(ret)) {
                    fprintf(stderr, "%s: value mismatch (%lu vs %lu) "
                      "while finding %lu\n", name, *ptr, data, key);
                    return 0;
                } else {
                    fprintf(stderr, "%s: return mismatch (%s vs %s) "
                      "while finding %lu\n", name, ret, rettostr(tmpret), key);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to find\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "findnear")) {
            /* find an entry in the rbtree */
            enum rbtree_ret tmpret;
            unsigned long int fkey = 0,
                              tmp_fkey = 0;

            if (!rbtree) { return 0; }

            /* read parameters */
            if (fscanf(fp, "%lu %lu %lu %256s", &key, &tmp_fkey, &data, ret) 
                == 4) {

                ptr = &tmpdata;
                tmpdata = 0;
                if ((strtoret(ret) == (tmpret 
                    = rbtree_luint_luint_find_near(rbtree, key, &fkey, &ptr))) 
                  && (fkey == tmp_fkey)
                  && (*ptr == data)) {
                    if (params.verbose) {
                        printf("%s: found %lu %lu near %lu, ret %s\n", name, 
                          fkey, data, key, ret);
                    }
                } else if (tmpret == strtoret(ret)) {
                    fprintf(stderr, "%s: value mismatch (%lu vs %lu) "
                      "while finding near %lu\n", name, *ptr, data, key);
                    return 0;
                } else {
                    fprintf(stderr, "%s: return mismatch (%s vs %s) "
                      "while finding near %lu\n", name, ret, rettostr(tmpret), 
                      key);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to find near\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "print")) {
            struct rbtree_iter *iter;
            enum rbtree_iter_order order;
            int reverse = 0;
            int internal = 0;

            /* print all entries */
            if (fscanf(fp, "%10s", orderbuf)) {
                orderbuf[10] = '\0';
                if (!str_casecmp(orderbuf, "internal")) {
                    rbtree_print(rbtree, stdout);
                    order = RBTREE_ITER_PREORDER;
                    internal = 1;
                } else if (!str_casecmp(orderbuf, "pre")) {
                    order = RBTREE_ITER_PREORDER;
                    reverse = 0;
                } else if (!str_casecmp(orderbuf, "in")) {
                    order = RBTREE_ITER_INORDER;
                    reverse = 0;
                } else if (!str_casecmp(orderbuf, "post")) {
                    order = RBTREE_ITER_POSTORDER;
                    reverse = 0;
                } else if (!str_casecmp(orderbuf, "rev_pre")) {
                    order = RBTREE_ITER_PREORDER;
                    reverse = 1;
                } else if (!str_casecmp(orderbuf, "rev_in")) {
                    order = RBTREE_ITER_INORDER;
                    reverse = 1;
                } else if (!str_casecmp(orderbuf, "rev_post")) {
                    order = RBTREE_ITER_POSTORDER;
                    reverse = 1;
                } else {
                    fprintf(stderr, "%s: unknown order '%s'\n",
                      name, orderbuf);
                    return 0;
                }

                if (!internal 
                  && (iter = rbtree_iter_new(rbtree, order, reverse))) {
                    unsigned long int *ptr;
                    unsigned int items = 0;

                    while ((tmpret 
                      = rbtree_iter_luint_luint_next(iter, &key, &ptr))
                        == RBTREE_OK) {

                        printf("%lu %lu\n", key, *ptr);
                        items++;
                    }

                    if ((tmpret == RBTREE_ITER_END) 
                      && (items == rbtree_size(rbtree))) {
                        printf("\n%u entries (%s)\n\n", items, orderbuf);
                    } else if (tmpret != RBTREE_ITER_END) {
                        fprintf(stderr, 
                          "%s: iteration over rbtree failed (%s)\n",
                          name, rettostr(tmpret));
                        return 0;
                    } else {
                        fprintf(stderr, 
                          "%s: iteration over rbtree fell short (%u vs %u)\n", 
                          name, items, rbtree_size(rbtree));
                        return 0;
                    }
                    rbtree_iter_delete(iter);
                } else if (!internal) {
                    fprintf(stderr, "%s: iteration over rbtree failed\n", name);
                    return 0;
                } else {
                    printf("\n%u entries (%s)\n\n", rbtree_size(rbtree), 
                      orderbuf);
                }
            }
        } else if (!str_casecmp(pos, "ls")) {
            /* match all entries */
            struct rbtree_iter *iter;
            unsigned int entries;
            enum rbtree_iter_order order;
            int reverse;

            if (!rbtree) { return 0; }

            if (fscanf(fp, "%10s %u", orderbuf, &entries) != 2) {
                fprintf(stderr, "%s: expected order, entry count\n", name);
                return 0;
            }

            orderbuf[10] = '\0';
            if (!str_casecmp(orderbuf, "pre")) {
                order = RBTREE_ITER_PREORDER;
                reverse = 0;
            } else if (!str_casecmp(orderbuf, "in")) {
                order = RBTREE_ITER_INORDER;
                reverse = 0;
            } else if (!str_casecmp(orderbuf, "post")) {
                order = RBTREE_ITER_POSTORDER;
                reverse = 0;
            } else if (!str_casecmp(orderbuf, "rev_pre")) {
                order = RBTREE_ITER_PREORDER;
                reverse = 1;
            } else if (!str_casecmp(orderbuf, "rev_in")) {
                order = RBTREE_ITER_INORDER;
                reverse = 1;
            } else if (!str_casecmp(orderbuf, "rev_post")) {
                order = RBTREE_ITER_POSTORDER;
                reverse = 1;
            } else {
                fprintf(stderr, "%s: unknown order '%s'\n",
                  name, orderbuf);
                return 0;
            }

            if (entries != rbtree_size(rbtree)) {
                fprintf(stderr, "%s: wrong number of entries (%u vs %u)\n", 
                  name, entries, rbtree_size(rbtree));
                return 0;
            }

            if ((iter = rbtree_iter_new(rbtree, order, reverse))) {
                unsigned long int *ptr;
                unsigned int items = 0;

                while (((tmpret 
                    = rbtree_iter_luint_luint_next(iter, &key, &ptr))
                      == RBTREE_OK)
                  && (fscanf(fp, "%lu %lu", &tmpkey, &tmpdata) == 2) 
                  && (tmpkey == key) && (tmpdata == *ptr)) {

                    if (params.verbose) {
                        printf("%s: matched %lu %lu\n", name, key, *ptr);
                    }
                    items++;
                }

                if ((tmpret == RBTREE_ITER_END) 
                  && (items == rbtree_size(rbtree))) {
                    if (params.verbose) {
                        printf("%s: matched %u entries\n", name, items);
                    }
                } else {
                    fprintf(stderr, "%s: ls matching failed\n", name);
                    return 0;
                }
                rbtree_iter_delete(iter);
            } else {
                fprintf(stderr, "%s: iteration over rbtree failed\n", name);
                return 0;
            }
        } else if ((*pos != '#') && str_len(pos)) {
            fprintf(stderr, "%s: unknown command '%s'\n", name, pos);
            return 0;
        }
    }

    assert(!rand);
    if (rbtree) {
        rbtree_delete(rbtree);
    }

    return 1;
}

