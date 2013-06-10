/* iobtree_1.c is a unit test for the iobtree module.  Its pretty
 * general, intended to test the btree as a black-box datastructure
 * Input format is:
 *
 * # comments
 * command params
 *
 * commands are:
 *
 *   new name pagesize leaftype nodetype: 
 *     create a new btree with page size pagesize, leaftype and nodetype specify
 *     the leaf and node strategies respectively
 *
 *   add term veclen: 
 *     add a new term (term) to the btree with vector length veclen
 *     and contents vector.  succeed is an integer indicating whether
 *     the operation should succeed or not.  Note that the vector cannot 
 *     start with whitespace.
 *
 *   ls numterms [term veclen vector]*:
 *     list the contents (in order) of the btree.  There should be
 *     numterms entries.  Each entry is a whitespace delimited term
 *     listing, vector length and then vector.
 *
 *   set term veclen vector:
 *     set the vector for a term.  The allocated length should be
 *     veclen at the time else the operation will fail.
 *
 *   realloc term newlen:
 *     change the space allocated to a term to newlen.  succeed
 *     indicates whether the operation should succeed.
 *
 *   rm term succeed
 *     remove a term from the btree.  succeed indicates whether the
 *     operation should succeed (try removing nonexistant terms)
 *
 *   append term
 *     like add, except that term must be lexically the largest in the tree, and
 *     should be more efficient
 *
 *   print
 *     print out current state of the btree and other debug info
 *
 * there is a length limit 65535 on terms and vectors.  Commands
 * should be put on a line by themselves, like this:
 *
 * # this is a comment, followed by a couple of commands
 * new
 *   case01 50 1 1
 * add 
 *   term1 10 xxxxxxxxxx 1
 *
 * written nml 2003-10-13
 *
 */

#include "firstinclude.h"

#include "test.h"

#include "iobtree.h"
#include "fdset.h"
#include "freemap.h"
#include "getmaxfsize.h"
#include "getlongopt.h"
#include "str.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* this stuff is for open */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* XXX: dodgy local declaration of debugging function */
int iobtree_print_index(struct iobtree *btree);

struct params {
    int verbose;
};

static int addfile(void *opaque, unsigned int file, unsigned int *maxsize) {
    int fd;
    struct fdset *fds = opaque;

    if (((fd = fdset_create(fds, 0, file)) >= 0)
      && getmaxfsize(fd, UINT_MAX, maxsize)
      && (fdset_unpin(fds, 0, file, fd) == FDSET_OK)) {
        return 1;
    }

    return 0;
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

int test_file(FILE *fp, int argc, char **argv) {
    char buf[65535 + 1];
    char *pos;
    struct params params = {0};
    char name[256];
    int leaf_strategy,
        node_strategy,
        write;
    struct iobtree *btree = NULL;
    struct fdset *fd = NULL;
    struct freemap *map = NULL;
    unsigned int i,
                 len;

    name[0] = '\0';

    if (!parse_params(argc, argv, &params)) {
        fprintf(stderr, "failed to parse params\n");
        return 0;
    }

    while (fgets((char*) buf, 65535, fp)) {
        str_rtrim(buf);
        pos = (char *) str_ltrim(buf);

        if (!str_casecmp(pos, "new")) {
            /* creating a new btree */
            unsigned int size = -1;

            if (btree) {
                iobtree_delete(btree);
            }
            if (map) {
                freemap_delete(map);
            }
            if (fd) {
                i = 0;
                while ((fdset_name(fd, 0, i, name, 256, &len, &write) 
                  == FDSET_OK) && (name[len] = '\0', unlink(name) == 0)) {
                    i++;
                }
                fdset_delete(fd);
            }

            /* read parameters */
            if ((fscanf(fp, "%255s %u %d %d", name, &size, &leaf_strategy, 
                &node_strategy) == 4)
              && (fd = fdset_new(0644, 0))
              && (fdset_set_type_name(fd, 0, "iobtree", str_len("iobtree"), 1) 
                == FDSET_OK)
              && (map = freemap_new(FREEMAP_STRATEGY_FIRST, 0, fd, addfile))
              && (btree = iobtree_new(size, leaf_strategy, node_strategy, map, 
                  fd, 0))) {

                /* succeeded, do nothing */
            } else {
                fprintf(stderr, "%s: failed to create btree\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "add")) {
            /* adding a term to the btree */
            void *ret;
            unsigned int veclen,
                         len;
            int toobig;
            unsigned int tmplen;

            if (!btree) { return 0; }

            /* read parameters */
            veclen = 0;
            if (((tmplen = fscanf(fp, "%65535s %u", buf, &veclen)) == 2) 
              && (veclen <= 65535)) {

                len = str_len(buf);
                if ((ret = iobtree_alloc(btree, buf, len, veclen, &toobig))) {
                    /* do nothing */
                    if (params.verbose) {
                        printf("added term %s\n", buf);
                    }
                } else {
                    fprintf(stderr, 
                      "%s: failed to add '%s' (len %u data %u) to btree\n", 
                      name, buf, (unsigned int) str_len(buf), veclen);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to add '%s' (%u, %u)\n", name, buf, 
                  veclen, tmplen);
                return 0;
            }
        } else if (!str_casecmp(pos, "append")) {
            /* appending a term to the btree */
            void *ret;
            unsigned int veclen,
                         len;
            int toobig;
            unsigned int tmplen;

            if (!btree) { return 0; }

            /* read parameters */
            veclen = 0;
            if (((tmplen = fscanf(fp, "%65535s %u", buf, &veclen)) == 2) 
              && (veclen <= 65535)) {

                len = str_len(buf);
                if ((ret = iobtree_append(btree, buf, len, veclen, &toobig))) {
                    /* do nothing */
                    if (params.verbose) {
                        printf("added term %s\n", buf);
                    }
                } else {
                    fprintf(stderr, 
                      "%s: failed to add '%s' (len %u data %u) to btree\n", 
                      name, buf, (unsigned int) str_len(buf), veclen);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to add '%s' (%u, %u)\n", name, buf, 
                  veclen, tmplen);
                return 0;
            }
        } else if (!str_casecmp(pos, "ls")) {
            /* matching stuff in the btree */
            unsigned int numterms,
                         i,
                         len,
                         veclen,
                         veclen2,
                         state[3];
            void *addr;
            const char *term;
            int intret;

            state[0] = state[1] = state[2] = 0;

            if (!btree) { return 0; }

            /* iterate along terms in btree, comparing with those given */
            if ((intret = fscanf(fp, "%u", &numterms)) 
              && (iobtree_size(btree) == numterms)) {
                char *tmpstr;

                /* now, take terms from file, comparing them with btree 
                 * entries */
                for (i = 0; i < numterms; i++) {
                    if ((term = iobtree_next_term(btree, state, &len, &addr, 
                        &veclen)) 
                      && fscanf(fp, "%65535s %u ", buf, &veclen2)
                      && (len == str_len(buf))
                      && !str_ncmp(buf, term, len)
                      && (tmpstr = str_dup(buf))) {
                        /* compare vectors */
                        if (fread(buf, veclen, 1, fp)
                          && (veclen == veclen2)
                          && !memcmp(buf, addr, veclen)) {

                            /* locate it through _find and compare again */
                            if ((addr = iobtree_find(btree, tmpstr, 
                                str_len(tmpstr), 0, &veclen))
                              && (veclen == veclen2)
                              && !memcmp(buf, addr, veclen)) {
                                /* do nothing */
                            } else {
                                unsigned int j;

                                fprintf(stderr, "%s: ls failed finding term ", 
                                  name);
                                for (j = 0; j < len; j++) {
                                    putc(term[j], stderr);
                                }
                                fprintf(stderr, "\n");
                                return 0;
                            }
                        } else {
                            unsigned int j;

                            fprintf(stderr, 
                              "%s: ls: different content for term ", name);
                            for (j = 0; j < len; j++) {
                                putc(term[j], stderr);
                            }
                            putc(':', stderr);
                            putc(' ', stderr);
                            for (j = 0; j < veclen; j++) {
                                putc(((const char *) addr)[j], stderr);
                            }
                            fprintf(stderr, " vs %s\n", buf);
                            return 0;
                        }
                        free(tmpstr);
                    } else {
                        unsigned int j;

                        fprintf(stderr, "%s: ls failed on term ", name);
                        for (j = 0; j < len; j++) {
                            putc(term[j], stderr);
                        }
                        fprintf(stderr, " vs %s\n", buf);
                        return 0;
                    }
                }

                if (params.verbose) {
                    printf("successfully matched %u terms\n", numterms);
                }
            } else if (intret) {
                fprintf(stderr, "numterms different than in btree (%u vs %u)\n",
                  numterms, iobtree_size(btree));
                return 0;
            } else {
                return 0;
            }
        } else if (!str_casecmp(pos, "set")) {
            /* setting the vector for a term in the btree */
            unsigned int veclen,
                        reallen;
            void *addr;

            if (!btree) { return 0; }

            /* read parameters */
            if ((fscanf(fp, "%65535s %u ", buf, &veclen) == 2) 
              && (veclen <= 65535)) {

                addr = iobtree_find(btree, buf, str_len(buf), 1, &reallen);

                if (addr && (reallen == veclen) 
                  && fread(addr, 1, veclen, fp)) {
                    /* do nothing */
                } else {
                    fprintf(stderr, "%s: failed to set '%s'!\n", name, buf);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to set '%s'\n", name, buf);
                return 0;
            }
        } else if (!str_casecmp(pos, "realloc")) {
            /* reallocating a term in the btree */
            unsigned int veclen;
            int toobig;

            if (!btree) { return 0; }

            /* read parameters */
            if ((fscanf(fp, "%65535s %u", buf, &veclen) == 2) 
              && (veclen <= 65535)) {

                if (!iobtree_realloc(btree, buf, 
                  str_len(buf), veclen, 
                  &toobig)) {
                    fprintf(stderr, "%s: failed to realloc!\n", name);
                    return 0;
                }
            } else {
                fprintf(stderr, "%s: failed to realloc\n", name);
                return 0;
            }
        } else if (!str_casecmp(pos, "rm")) {
            /* removing something from the btree */
            unsigned int succeed;
            int intret;

            if (!btree) { return 0; }

            if (fscanf(fp, "%65535s %u", buf, &succeed) == 2) {
                if ((!(intret 
                      = iobtree_remove(btree, buf, str_len(buf))) 
                    && succeed) 
                  || (intret && !succeed)) {
                    fprintf(stderr, "%s: failed to rm '%s'\n", name, buf);
                    return 0;
                }
            } else {
                return 0;
            }
        } else if (!str_casecmp(pos, "print")) {
            /* printing out the btree contents */
            unsigned int state[3] = {0, 0, 0},
                         len,
                         veclen;
            const char *term;
            char format[100];
            void *addr;

            if (!btree) { 
                printf("can't print, no btree\n");
            } else {
                do {
                    unsigned int i;

                    term 
                      = iobtree_next_term(btree, state, &len, &addr, &veclen);

                    for (i = 0; term && i < len; i++) {
                        fputc(term[i], stdout);
                    }
                } while (term 
                  && memcpy(buf, term, len)
                  && ((buf[len] = '\0') || 1)
                  && snprintf(format, 100, "(%%u): '%%.%us' (%%u)\n", veclen)
                  && printf(format, len, (char *) addr, veclen));

                if (!state) {
                    printf("(empty)\n");
                }

                printf("\nindex:\n");
                iobtree_print_index(btree);

                printf("%u entries\n\n", iobtree_size(btree));
            }
        } else if ((*pos != '#') && str_len(pos)) {
            fprintf(stderr, "%s: unknown command '%s'\n", name, pos);
            return 0;
        }
    }

    if (btree) {
        iobtree_delete(btree);
    }
    if (map) {
        freemap_delete(map);
    }
    if (fd) {
        i = 0;
        while ((fdset_name(fd, 0, i, name, 256, &len, &write) 
          == FDSET_OK) && (name[len] = '\0', unlink(name) == 0)) {
            i++;
        }
        fdset_delete(fd);
    }

    return 1;
}

