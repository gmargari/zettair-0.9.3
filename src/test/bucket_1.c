/* bucket_1.c is a unit test for the bucket module.  Its pretty
 * general, intended to test all of the individual bucket stuff. 
 * Input format is:
 *
 * # comments
 * command params
 *
 * commands are:
 *
 *   new name buckettype size: 
 *     create a new bucket of size size 
 *     buckettype is the bucketing strategy 
 *
 *   add term veclen succeed: 
 *     add a new term (term) to the bucket with vector length veclen
 *     and contents vector.  succeed is an integer indicating whether
 *     the operation should succeed or not.  Note that the vector cannot 
 *     start with whitespace.
 *
 *   ls numterms [term veclen vector]*:
 *     list the contents (not in order) of the bucket.  There should be
 *     numterms entries.  Each entry is a whitespace delimited term
 *     listing, vector length and then vector.
 *
 *   set term veclen vector:
 *     set the vector for a term.  The allocated length should be
 *     veclen at the time else the operation will fail.
 *
 *   realloc term newlen succeed:
 *     change the space allocated to a term to newlen.  succeed
 *     indicates whether the operation should succeed.
 *
 *   rm term succeed
 *     remove a term from the bucket.  succeed indicates whether the
 *     operation should succeed (try removing nonexistant terms)
 *
 *   print
 *     print out current state of the bucket and other debug info
 *
 * there is a length limit 65535 on terms and vectors.  Commands
 * should be put on a line by themselves, like this:
 *
 * # this is a comment, followed by a couple of commands
 * new
 *   case01 1 100 50
 * add 
 *   term1 10 xxxxxxxxxx 1
 *
 * written nml 2003-10-13
 *
 */

/* suggestions for improvement: 
   - ordered ls (tests for order as well)
   - set on add (tests return pointer from alloc)
 */

#include "firstinclude.h"

#include "test.h"

#include "bucket.h"
#include "str.h"
#include "chash.h"
#include "getlongopt.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

int test_file(FILE *fp, int argc, char **argv) {
    char buf[65535 + 1];
    char *pos;
    unsigned int strategy = 0;  /* what bucketing strategy we're 
                                          using */
    void *ptr = NULL;
    unsigned int bucketsize = 0;
    struct params params = {0};
    struct chash *hash = NULL;
    char name[256];

    if (!parse_params(argc, argv, &params)) {
        fprintf(stderr, "failed to parse params\n");
        return 0;
    }

    while (fgets((char *) buf, 65535, fp)) {
        str_rtrim(buf);
        pos = (char *) str_ltrim(buf);

        if (!str_casecmp(pos, "new")) {

            /* creating a new bucket */
            unsigned int size = -1;

            if (ptr) {
                chash_delete(hash);
                free(ptr);
            }

            /* read parameters */
            if ((fscanf(fp, "%255s %u %u", name, &strategy, &size) == 3)
              && (size <= 65535) 
              && (bucketsize = size)
              && (ptr = malloc(size))
              && (hash = chash_ptr_new(1, 2.0, 
                /* some fn pointer casting dodginess */
                (unsigned int (*)(const void *)) str_len, 
                (int (*)(const void *, const void *)) str_cmp))
              && (bucket_new(ptr, bucketsize, strategy))) {
                /* succeeded, do nothing */
                if (params.verbose) {
                    printf("%s: new bucket with size %u strategy %u\n", name, 
                      size, strategy);
                }
            } else {
                fprintf(stderr, "%s: failed to create bucket\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "add")) {
            /* adding a term to the bucket */
            void *ret;
            unsigned int veclen,
                         succeed,
                         len;
            int toobig;

            if (!ptr) { return 0; }

            /* read parameters */
            if ((fscanf(fp, "%65535s %u %u", buf, &veclen, &succeed) == 3) 
              && (veclen <= 65535)) {

                len = str_len(buf);
                if ((((ret = bucket_alloc(ptr, bucketsize, strategy, buf, len, 
                        veclen, &toobig, NULL))
                      && succeed)
                    || (!ret && !succeed))) {
                    /* do nothing */
                    if (params.verbose) {
                        printf("%s: added term '%s'\n", name, buf);
                    }
                } else if (succeed) {
                    fprintf(stderr, "%s: failed to add '%s' to bucket\n", 
                      name, buf);
                    return 0;
                } else if (!succeed) {
                    fprintf(stderr, "%s: add '%s' succeeded but shouldn't "
                      "have\n", name, buf);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to add\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "ls")) {
            /* matching stuff in the bucket */
            unsigned int numterms,
                         i,
                         len,
                         veclen,
                         veclen2,
                         state;
            void *addr;
            struct chash *tmphash;
            const char *term;
            void **tmpptr,
                  *tmp;

            if (!ptr) { return 0; }

            if (!(tmphash = chash_ptr_new(1, 2.0, 
              /* some fn pointer casting dodginess */
              (unsigned int (*)(const void *)) str_len, 
              (int (*)(const void *, const void *)) str_cmp))) {
                fprintf(stderr, "%s: failed to init hashtable\n", name);
                return 0;
            }

            /* first, fill hashtable with all terms from bucket */
            state = 0;
            while ((term 
              = bucket_next_term(ptr, bucketsize, strategy,
                  &state, &len, &addr, &veclen))) {

                if (!((term = str_ndup(term, len)) 
                  && (chash_ptr_ptr_insert(tmphash, term, (void*) term) 
                      == CHASH_OK))) {

                    fprintf(stderr, "%s: failed to init hashtable\n", name);
                    return 0;
                }
            }

            /* now, take terms from file, comparing them with hashtable 
             * entries */
            if (fscanf(fp, "%u", &numterms)) {
                for (i = 0; i < numterms; i++) {
                    if (fscanf(fp, "%65535s %u ", buf, &veclen)) {
                        if (params.verbose) {
                            printf("%s: ls checking %s\n", name, buf);
                        }
                        
                        if ((addr = bucket_find(ptr, bucketsize, strategy,
                            buf, str_len(buf), &veclen2, NULL))
                          /* remove it from hashtable */
                          && chash_ptr_ptr_find(tmphash, buf, &tmpptr) 
                            == CHASH_OK
                          && chash_ptr_ptr_remove(tmphash, *tmpptr, &tmp) 
                            == CHASH_OK
                          && (free(tmp), 1)
                          && (veclen <= 65535)
                          && (veclen2 == veclen)
                          && fread(buf, veclen, 1, fp)
                          && ((buf[veclen] = '\0'), 1)
                          && (!params.verbose 
                            || printf("%s: ls check read '%s'\n", name, buf))
                          && !memcmp(buf, addr, veclen)) {
                            /* do nothing */
                        } else {
                            unsigned int j;

                            fprintf(stderr, "%s: ls failed cmp '%s' with '", 
                              name, buf);
                            for (j = 0; j < veclen; j++) {
                                putc(((char *) addr)[j], stderr);
                            }
                            fprintf(stderr, "'\n");
                            return 0;
                        }
                    } else {
                        fprintf(stderr, "%s: ls failed\n", name);
                        return 0;
                    }
                }

                if (chash_size(tmphash)) {
                    fprintf(stderr, "%s: ls failed\n", name);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: ls failed\n", name);
                return 0;
            }

            chash_delete(tmphash);

            if (params.verbose) {
                printf("%s: matched all (%u) entries\n", name, numterms);
            }
        } else if (!str_casecmp(pos, "set")) {
            /* setting the vector for a term in the bucket */
            unsigned int veclen,
                        reallen;
            void *addr;

            if (!ptr) { return 0; }

            /* read parameters */
            if ((fscanf(fp, "%65535s %u ", buf, &veclen) == 2) 
              && (veclen <= 65535)) {

                addr = bucket_find(ptr, bucketsize, strategy, buf, 
                  str_len(buf), &reallen, NULL);

                if (addr && (reallen == veclen) 
                  && fread(addr, 1, veclen, fp)) {
                    /* do nothing */
                    if (params.verbose) {
                        unsigned int j;

                        printf("%s: set term '%s' to '", name, buf);
                        for (j = 0; j < reallen; j++) {
                            putc(((char *) addr)[j], stdout);
                        }
                        printf("'\n");
                    }
                } else {
                    fprintf(stderr, "%s: failed to set!\n", name);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to set\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "realloc")) {
            /* reallocating a term in the bucket */
            unsigned int veclen,
                         succeed;
            int toobig;

            if (!ptr) { return 0; }

            /* read parameters */
            if ((fscanf(fp, "%65535s %u %u", buf, &veclen, &succeed) == 3) 
              && (veclen <= 65535)) {

                if (!bucket_realloc(ptr, bucketsize, strategy, buf, 
                  str_len(buf), veclen, &toobig)) {
                    fprintf(stderr, "%s: failed to realloc!\n", name);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to realloc\n", name);
                return 0;
            }

            if (params.verbose) {
                printf("%s: realloc'd term '%s'\n", name, buf);
            }
        } else if (!str_casecmp(pos, "rm")) {
            /* removing something from the bucket */
            unsigned int succeed;

            if (!ptr) { return 0; }

            if (fscanf(fp, "%65535s %u", buf, &succeed) == 2) {
                if (succeed) {

                    if (!(bucket_remove(ptr, bucketsize, strategy, buf, 
                      str_len(buf)))) {
                        fprintf(stderr, "%s: failed to rm '%s'\n", name, 
                          buf);
                        return 0;
                    } else if (params.verbose) {
                        printf("%s: rm term '%s'\n", name, buf);
                    }
                } else if (succeed) {
                    fprintf(stderr, "%s: failed to rm\n", name);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to rm\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "print")) {
            /* printing out the bucket contents */
            unsigned int state = 0,
                         len,
                         veclen;
            const char *term;
            char format[100];
            void *addr;

            if (!ptr) { 
                printf("can't print, no bucket\n");
            } else {
                do {
                    term 
                      = bucket_next_term(ptr, bucketsize, strategy, &state, 
                        &len, &addr, &veclen);
                } while (term 
                  && memcpy(buf, term, len)
                  && ((buf[len] = '\0') || 1)
                  && snprintf(format, 100, "%%.%us (%%u): '%%.%us' (%%u) "
                    "(off %%u)\n", len, veclen) 
                  && printf(format, term, len, (char*) addr, veclen, 
                    ((char *) addr) - (char *) ptr));

                if (!state) {
                    printf("(empty)\n");
                }

                printf("%u entries, %u data, %u string, %u overhead, %u free\n", 
                  bucket_entries(ptr, bucketsize, strategy), 
                  bucket_utilised(ptr, bucketsize, strategy), 
                  bucket_string(ptr, bucketsize, strategy), 
                  bucket_overhead(ptr, bucketsize, strategy),
                  bucket_unused(ptr, bucketsize, strategy));
            }
        } else if (!str_casecmp(pos, "match")) {
            unsigned int veclen,
                         veclen2;
            void *addr;

            if (fscanf(fp, "%65535s %u ", buf, &veclen)) {
                if ((addr = bucket_find(ptr, bucketsize, strategy,
                    buf, str_len(buf), &veclen2, NULL))
                  && (veclen <= 65535)
                  && (veclen2 >= veclen)
                  && (!params.verbose 
                    || printf("%s: match on '%s' ", name, buf))
                  && fread(buf, veclen, 1, fp)
                  && !memcmp(buf, addr, veclen)) {
                    if (params.verbose) {
                        printf("content succeeded\n");
                    }
                } else {
                    fprintf(stderr, "%s: match failed (%s vs %s)\n", name, buf,
                      (char *) addr);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: match failed\n", name);
                return 0;
            }
        } else if ((*pos != '#') && str_len(pos)) {
            fprintf(stderr, "%s: unknown command '%s'\n", name, pos);
            return 0;
        }
    }

    if (ptr) {
        chash_delete(hash);
        free(ptr);
    }

    return 1;
}

