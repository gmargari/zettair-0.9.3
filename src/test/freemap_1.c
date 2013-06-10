/* freemap_1.c implements a unit test for the freemap module.  It aims
 * to test the entire interface.  Input format is:
 *
 * # comments
 * command params
 *
 * commands are:
 *
 *   new name append files [size]
 *     create a new freemap that can append at most append bytes to any
 *     non-exact allocation, and grows to at most files number of
 *     files, each of which is of size given in the size array
 *
 *   print
 *     print the freemap to stdout
 *
 *   malloc exact size ret
 *     allocate some memory of size at least size from the freemap, getting the 
 *     exact amount of space if exact is 1.  ret indicates whether the
 *     operation will succeed or fail.
 *
 *   locmalloc exact fileno offset size ret
 *     allocate some memory from a specific location.  like malloc,
 *     except that the allocation MUST occur from specified fileno and
 *     offset if it is to succeed.
 *
 *   free fileno offset size ret
 *     free memory allocated at fileno, offset, of size size.  ret
 *     indicates whether this action should succeed.
 *
 *   autofree num
 *     free memory allocated by call num
 *
 *   finish
 *     free all memory, check that map is sensible, delete it
 *
 *   realloc exact fileno offset size additional ret
 *     resize an existing allocation at fileno, offset of current size
 *     size, with additional specifying how much extra to get (XXX:
 *     should be able to shrink allocations with this too)
 *
 * Commands should be put on a line by themselves, like this:
 *
 * # this is a comment, followed by a couple of commands
 * new
 *   case01 1 1 100
 * malloc 
 *   0 10 1
 *
 * written nml 2004-08-24
 *
 */

#include "test.h"

#include "freemap.h"
#include "str.h"
#include "getlongopt.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
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

struct uint_arr {
    unsigned int size;
    unsigned int *arr;
};

int newfile(void *opaque, unsigned int file, unsigned int *maxsize) {
    struct uint_arr *files = opaque;
    
    if (file < files->size) {
        *maxsize = files->arr[file];
        return 1;
    } else {
        return 0;
    }
}

struct allocation {
    unsigned int fileno;
    unsigned long int offset;
    unsigned int size;
};

int allocation_grow(struct allocation **alloc, unsigned int *size) {
    void *ptr = realloc(*alloc, (*size * 2 + 1) * sizeof(**alloc));

    if (ptr) {
        *alloc = ptr;
        *size = *size * 2 + 1;
        return 1;
    } else {
        return 0;
    }
}

int test_file(FILE *fp, int argc, char **argv) {
    char *pos;
    unsigned long int tmp;
    struct params params = {0};
    struct freemap *map = NULL;
    unsigned int append;
    struct uint_arr files = {0, NULL};
    char name[256];
    int ret,
        exact;
    char buf[1024 + 1];
    unsigned int fileno,
                 tmpfileno;
    unsigned long int offset,
                      tmpoffset;
    unsigned int size;
    unsigned int additional,
                 i;
    struct allocation *alloc = NULL;
    unsigned int allocs = 0;
    unsigned int alloc_cap = 0;


    if (!parse_params(argc, argv, &params)) {
        fprintf(stderr, "failed to parse params\n");
        return 0;
    }

    while (fgets((char *) buf, 1024, fp)) {
        str_rtrim(buf);
        pos = (char *) str_ltrim(buf);

        if (!str_casecmp(pos, "new")) {
            if (map) {
                freemap_delete(map);
            }
            if (files.arr) {
                free(files.arr);
            }
            allocs = 0;
            if (params.verbose) {
                printf("\n");
            }

            /* read parameters */
            if ((fscanf(fp, "%255s %u %u", name, &append, &files.size) == 3)
              && (files.arr = malloc(sizeof(*files.arr) * files.size))
              && (map = freemap_new(FREEMAP_STRATEGY_FIRST, append, &files, 
                  newfile))) {

                for (i = 0; i < files.size; i++) {
                    if (!(fscanf(fp, "%u", &files.arr[i]))) {
                        free(files.arr);
                        files.arr = NULL;
                        freemap_delete(map);
                        map = NULL;
                        break;
                    }
                }

                if (i == files.size) {
                    /* succeeded, do nothing */
                    if (params.verbose) {
                        printf("%s: new freemap with append size %u, "
                          "file sizes: ", name, append);
                        for (i = 0; i < files.size; i++) {
                            printf("%u ", files.arr[i]);
                        }
                        printf("\n");
                    }
                } else {
                    /* failed */
                    fprintf(stderr, "%s: failed to read all file sizes\n", name);
                }
            } else {
                fprintf(stderr, "%s: failed to create freemap\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "malloc")) {
            if (!map) { return 0; }

            /* read parameters */
            if (fscanf(fp, "%d %u %d", &exact, &size, &ret) == 3) {
                tmp = size;
                if (freemap_malloc(map, &fileno, &offset, &size, 
                    !!exact * FREEMAP_OPT_EXACT)) {

                    while (allocs >= alloc_cap) {
                        if (!(allocation_grow(&alloc, &alloc_cap))) {
                            fprintf(stderr, "%s: failed to grow allocations\n", 
                              name);
                            return 0;
                        }
                    }

                    /* stuff it into allocations table */
                    alloc[allocs].fileno = fileno;
                    alloc[allocs].offset = offset;
                    alloc[allocs].size = size;
                    allocs++;

                    if (ret) {
                        if (params.verbose) {
                            printf("%s: allocated %u %lu %u\n", name, fileno, 
                              offset, size);
                        }
                    } else {
                        fprintf(stderr, "%s: malloc size %lu should have failed "
                          "but didn't (returned %u %lu %u)\n", name, tmp, 
                            fileno, offset, size);
                        return 0;
                    }
                } else {
                    if (ret) {
                        fprintf(stderr, "%s: malloc size %u should have "
                          "succeeded but didn't\n", name, size);
                        return 0;
                    } else {
                        if (params.verbose) {
                            printf("%s: succeeded in failing to allocate %u "
                              "bytes\n", name, size);
                        }
                    }
                }
            } else {
                fprintf(stderr, "%s: failed to read params for malloc\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "locmalloc")) {
            if (!map) { return 0; }

            /* read parameters */
            if (fscanf(fp, "%d %u %lu %u %d", &exact, &fileno, &offset, &size, 
                &ret) == 5) {

                tmp = size;
                tmpfileno = fileno;
                tmpoffset = offset;
                if (freemap_malloc(map, &fileno, &offset, &size, 
                    !!exact * FREEMAP_OPT_EXACT | FREEMAP_OPT_LOCATION, 
                    fileno, offset)) {

                    assert(fileno == tmpfileno && offset == tmpoffset);

                    while (allocs >= alloc_cap) {
                        if (!(allocation_grow(&alloc, &alloc_cap))) {
                            fprintf(stderr, "%s: failed to grow allocations\n", 
                              name);
                            return 0;
                        }
                    }

                    /* stuff it into allocations table */
                    alloc[allocs].fileno = fileno;
                    alloc[allocs].offset = offset;
                    alloc[allocs].size = size;
                    allocs++;

                    if (ret) {
                        if (params.verbose) {
                            printf("%s: allocated %u %lu %u with loc %u %lu\n",
                              name, fileno, offset, size, tmpfileno, tmpoffset);
                        }
                    } else {
                        fprintf(stderr, "%s: locmalloc size %lu should have "
                          "failed but didn't (returned %u %lu %u)\n", name, tmp, 
                            fileno, offset, size);
                        return 0;
                    }
                } else {
                    if (ret) {
                        fprintf(stderr, "%s: locmalloc size %u should have "
                          "succeeded but didn't\n", name, size);
                        return 0;
                    } else {
                        if (params.verbose) {
                            printf("%s: succeeded in failing to allocate %u "
                              "bytes with loc %u %lu\n", name, size, fileno, 
                              offset);
                        }
                    }
                }
            } else {
                fprintf(stderr, "%s: failed to read params for locmalloc\n", 
                  name);
                return 0;
            }
        } else if (!str_casecmp(pos, "realloc")) {
            if (!map) { return 0; }

            /* read parameters */
            if (fscanf(fp, "%d %u %lu %u %u %d", &exact, &fileno, &offset, 
                &size, &additional, &ret) == 6) {

                if ((tmp = freemap_realloc(map, fileno, offset, size, 
                    additional, !!exact * FREEMAP_OPT_EXACT))) {

                    assert(tmp >= additional);

                    for (i = 0; i < allocs; i++) {
                        if (alloc[i].fileno == fileno 
                          && alloc[i].offset == offset) {
                            assert(alloc[i].size == size);
                            alloc[i].size = size + tmp;
                            break;
                        }
                    }

                    if (i == allocs) {
                        fprintf(stderr, 
                          "%s: failed to update %u %lu in allocations table\n", 
                          name, fileno, offset);
                        return 0;
                    }

                    if (ret) {
                        if (params.verbose) {
                            printf("%s: reallocated %u %lu %u to size %lu\n",
                              name, fileno, offset, size, size + tmp);
                        }
                    } else {
                        fprintf(stderr, "%s: realloc %u %lu %u + %u should have "
                          "failed but didn't (returned %lu)\n", name, fileno, 
                          offset, size, additional, tmp);
                        return 0;
                    }
                } else {
                    if (ret) {
                        fprintf(stderr, "%s: realloc %u %lu %u + %lu should "
                          "have succeeded but didn't\n", name, fileno, offset, 
                          size, tmp);
                        return 0;
                    } else {
                        if (params.verbose) {
                            printf("%s: succeeded in failing to reallocate "
                              "%u %lu %u + %u\n", name, fileno, 
                              offset, size, additional);
                        }
                    }
                }
            } else {
                fprintf(stderr, "%s: failed to read params for realloc\n", 
                  name);
                return 0;
            }
        } else if (!str_casecmp(pos, "autofree")) {
            if (!map) { return 0; }

            /* read parameters */
            if (fscanf(fp, "%lu", &tmp) == 1) {

                if (tmp >= allocs) {
                    fprintf(stderr, "%s: no such allocation %lu\n", name, tmp);
                }

                if (freemap_free(map, alloc[tmp].fileno, alloc[tmp].offset, 
                  alloc[tmp].size)) {
                    if (params.verbose) {
                        printf("%s: autofreed %u %lu %u\n",
                          name, alloc[tmp].fileno, alloc[tmp].offset, 
                          alloc[tmp].size);
                    }
                } else {
                    fprintf(stderr, "%s: autofree %u %lu %u should "
                      "have succeeded but didn't\n", name, alloc[tmp].fileno, 
                      alloc[tmp].offset, alloc[tmp].size);
                    return 0;
                }

                allocs--;
                memmove(&alloc[tmp], &alloc[tmp + 1], 
                  sizeof(*alloc) * (allocs - tmp));
            } else {
                fprintf(stderr, "%s: failed to read params for autofree\n", 
                  name);
                return 0;
            }
        } else if (!str_casecmp(pos, "free")) {
            if (!map) { return 0; }

            /* read parameters */
            if (fscanf(fp, "%u %lu %u %d", &fileno, &offset, &size, &ret) 
                == 4) {

                if (freemap_free(map, fileno, offset, size)) {
                    if (ret) {
                        if (params.verbose) {
                            printf("%s: freed %u %lu %u\n",
                              name, fileno, offset, size);
                        }
                    } else {
                        fprintf(stderr, "%s: free %u %lu %u "
                          "failed but didn't\n", name, fileno, offset, size);
                        return 0;
                    }
                } else {
                    if (ret) {
                        fprintf(stderr, "%s: free %u %lu %u should "
                          "have succeeded but didn't\n", name, fileno, offset, 
                          size);
                        return 0;
                    } else {
                        if (params.verbose) {
                            printf("%s: succeeded in failing to free "
                              "%u %lu %u\n", name, fileno, offset, size);
                        }
                    }
                }
            } else {
                fprintf(stderr, "%s: failed to read params for free\n", 
                  name);
                return 0;
            }
        } else if (!str_casecmp(pos, "finish")) {
            if (!map) { return 0; }

            /* return all autoallocations */
            for (i = 0; i < allocs; i++) {
                if (freemap_free(map, alloc[i].fileno, alloc[i].offset, 
                  alloc[i].size)) {
                } else {
                    fprintf(stderr, "%s: finish autofree %u %lu %u should "
                      "have succeeded but didn't\n", name, alloc[i].fileno, 
                      alloc[i].offset, alloc[i].size);
                    return 0;
                }
            }

            /* we should now be able to locmalloc the original locations we 
             * had */
            for (i = 0; i < files.size; i++) {
                if (!freemap_malloc(map, &fileno, &offset, &files.arr[i], 
                    FREEMAP_OPT_EXACT | FREEMAP_OPT_LOCATION, i, 0)) {

                    fprintf(stderr, "%s: finish locmalloc %u %u %u should "
                      "have succeeded but didn't\n", name, i, 0, files.arr[i]);
                    return 0;
                }
            }

            /* and the map should be completely utilised with no entries */
            if (freemap_utilisation(map) != 1.0 || freemap_entries(map)) {
                fprintf(stderr, "%s: finish map utilisation and entries are "
                  "screwed up (%f and %u)\n", name, freemap_utilisation(map), 
                  freemap_entries(map));
                freemap_print(map, stdout);
                return 0;
            }
 
            if (files.arr) {
                free(files.arr);
                files.arr = NULL;
            }
            if (map) {
                freemap_delete(map);
                map = NULL;
            }
            if (alloc) {
                free(alloc);
                allocs = 0;
                alloc_cap = 0;
                alloc = NULL;
            }
        } else if (!str_casecmp(pos, "printauto")) {
            for (i = 0; i < allocs; i++) {
                printf("%u. %u %lu size %u\n", i, alloc[i].fileno, 
                  alloc[i].offset, alloc[i].size);
            }

            printf("\n%u allocations stored\n\n", allocs);
        } else if (!str_casecmp(pos, "print")) {
            if (!map) { return 0; }

            printf("\n");
            freemap_print(map, stdout);
            printf("\n%u entries, %f utilised out of %f, %f wasted\n\n", 
              freemap_entries(map), freemap_utilisation(map), 
              freemap_space(map), freemap_wasted(map));
        } else if ((*pos != '#') && str_len(pos)) {
            fprintf(stderr, "%s: unknown command '%s'\n", name, pos);
            return 0;
        }
    }
    
    if (files.arr) {
        free(files.arr);
    }
    if (map) {
        freemap_delete(map);
    }
    if (alloc) {
        allocs = 0;
        alloc_cap = 0;
        free(alloc);
        alloc = NULL;
    }

    return 1;
}

