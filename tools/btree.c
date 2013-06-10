/* btree.c implements a small program to build, search and print a
 * btree.
 *
 * written nml 2004-09-22
 *
 */

#include "iobtree.h"
#include "freemap.h"
#include "fdset.h"
#include "getlongopt.h"
#include "str.h"
#include "getmaxfsize.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

/* maximum length of terms read from file */
#define BUFSIZE 1024

enum mode {
    MODE_ERR, MODE_BUILD, MODE_SEARCH, MODE_PRINT
};

struct args {
    char *prefix;               /* btree file name prefix */
    enum mode mode;             /* what we want to do */
    FILE *input;                /* input stream of words */
    unsigned int limit;         /* limit on number of words to read */
    unsigned int pagesize;      /* size of btree pages */
    int leaf_strategy;          /* leaf node bucket strategy */
    int node_strategy;          /* internal node bucket strategy */
    unsigned int entrysize;     /* size of data with each entry */
};

static void free_args(struct args *args) {
    if (args->prefix) {
        free(args->prefix);
        args->prefix = NULL;
    }
    if (args->input) {
        fclose(args->input);
        args->input = NULL;
    }
    return;
}

static struct args *parse_args(unsigned int argc, char **argv, 
  struct args *args, FILE *output) {
    struct getlongopt *parser;   /* option parser */
    int id;                      /* id of parsed option */
    const char *arg;             /* argument of parsed option */
    unsigned int ind;            /* current index in argv array */
    enum getlongopt_ret ret;     /* return value from option parser */

    struct getlongopt_opt opts[] = {
        {"build", 'b', GETLONGOPT_ARG_NONE, 'b'},
        {"search", 's', GETLONGOPT_ARG_NONE, 's'},
        {"print", 'p', GETLONGOPT_ARG_NONE, 'p'},
        {"input", 'i', GETLONGOPT_ARG_REQUIRED, 'i'},
        {"prefix", 'f', GETLONGOPT_ARG_REQUIRED, 'f'},
        {"limit", 'l', GETLONGOPT_ARG_REQUIRED, 'l'},
        {"pagesize", '\0', GETLONGOPT_ARG_REQUIRED, 'P'},
        {"leaf-strategy", '\0', GETLONGOPT_ARG_REQUIRED, 'L'},
        {"node-strategy", '\0', GETLONGOPT_ARG_REQUIRED, 'N'},
        {"entry-size", '\0', GETLONGOPT_ARG_REQUIRED, 'Z'},
    };

    args->mode = MODE_ERR;
    args->input = stdin;
    args->prefix = NULL;
    args->limit = 0;
    args->pagesize = 8096;
    args->leaf_strategy = args->node_strategy = 1;
    args->entrysize = 0;

    if ((parser = getlongopt_new(argc - 1, (const char **) &argv[1], opts, 
        sizeof(opts) / sizeof(*opts)))) {
        /* succeeded, do nothing */
    } else {
        fprintf(output, "failed to initialise options parser\n");
        return NULL;
    }

    while ((ret = getlongopt(parser, &id, &arg)) == GETLONGOPT_OK) {
        switch (id) {
        case 'Z':
            args->entrysize = strtol(arg, NULL, 0);
            break;

        case 'P':
            args->pagesize = strtol(arg, NULL, 0);
            break;

        case 'L':
            args->leaf_strategy = strtol(arg, NULL, 0);
            break;

        case 'N':
            args->node_strategy = strtol(arg, NULL, 0);
            break;

        case 'b':
            /* build btree */
            args->mode = MODE_BUILD;
            break;

        case 's':
            /* search btree */
            args->mode = MODE_SEARCH;
            break;

        case 'p':
            /* print btree */
            args->mode = MODE_PRINT;
            break;

        case 'f':
            /* change prefix */
            if ((args->prefix = str_dup(arg))) {
                /* succeeded, do nothing */
            } else {
                fprintf(output, "failed to dup '%s': %s\n", arg, 
                  strerror(errno));
                return NULL;
            }
            break;

        case 'i':
            /* get input from file */
            if ((args->input = fopen(arg, "rb"))) {
                /* succeeded, do nothing */
            } else {
                fprintf(output, "failed to open '%s': %s\n", arg, 
                  strerror(errno));
                return NULL;
            }
            break;

        case 'l':
            args->limit = strtol(arg, NULL, 0);
            break;
        }
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

    if (!args->prefix && !(args->prefix = str_dup("btree"))) {
        fprintf(output, "couldn't dup\n");
        return NULL;
    }

    /* should be no other arguments */
    if ((ind < argc)) {
        fprintf(output, "too many arguments\n");
        return NULL;
    }

    return args;
}

static int addfile(void *opaque, unsigned int file, unsigned int *maxsize) {
    struct fdset *fds = opaque;
    int fd;

    /* we don't care if the file exists or not... */
    if ((((fd = fdset_create(fds, 0, file)) >= 0)
        || ((fd == -EEXIST) 
          && ((fd = fdset_pin(fds, 0, file, 0, SEEK_CUR)) 
            >= 0)))
      && getmaxfsize(fd, UINT_MAX, maxsize)
      && (fdset_unpin(fds, 0, file, fd) == FDSET_OK)) {
        return 1;
    } else {
        return 0;
    }
}

static void build(struct iobtree *btree, FILE *input, unsigned int limit, 
  unsigned int entrysize) {
    int limited = limit,
        toobig = 0;
    char buf[BUFSIZE + 1];
    unsigned int inserted = 0, 
                 bytes = 0,
                 failed = 0,
                 len;

    buf[BUFSIZE] = '\0';
    while ((!limited || limit) && fgets(buf, BUFSIZE, input)) {
        if (buf[0]) {
            limit--;

            str_rtrim(buf);
            len = str_len(buf);
            if (iobtree_alloc(btree, buf, len, entrysize, &toobig)) {
                bytes += len;
                inserted++;
            } else {
                failed++;
                fprintf(stderr, "failed to insert '%s'\n", buf);
            }
        }
    }

    printf("inserted %u strings (%u bytes), with data length %u.  %u failed\n",
      inserted, bytes, entrysize, failed);
}

static void search(struct iobtree *btree, FILE *input, unsigned int limit) {
    int limited = limit;
    char buf[BUFSIZE + 1];
    unsigned int datalen;
    unsigned int searches = 0,
                 found = 0;

    buf[BUFSIZE] = '\0';
    while ((!limited || limit) && fgets(buf, BUFSIZE, input)) {
        if (buf[0]) {
            limit--;
            searches++;
            if (iobtree_find(btree, buf, str_len(buf), 0, &datalen)) {
                found++;
            }
        }
    }

    printf("searched %u times, found %u, (final data length %u)\n", searches, 
      found, datalen);
}

static void print(struct iobtree *btree, FILE *output) {
    unsigned int state[3] = {0, 0, 0},
                 termlen,
                 datalen,
                 i;
    void *data;
    const char *term;

    while ((term 
        = iobtree_next_term(btree, state, &termlen, &data, &datalen))) {

        fputc('\'', output);
        for (i = 0; i < termlen; i++) {
            fputc(term[i], output);
        }
        fprintf(output, "' (%u) %u bytes data\n", termlen, datalen);
    }
}

int main(int argc, char **argv) {
    struct args args;
    struct fdset *fdset;
    struct iobtree *btree = NULL;
    struct freemap *freemap = NULL;
    unsigned long int offset;
    unsigned int fileno,
                 overhead = sizeof(fileno) + sizeof(offset) 
      + sizeof(args.pagesize) + sizeof(args.leaf_strategy) 
      + sizeof(args.node_strategy);
    int fd = -1;
    FILE *output = stderr;

    if (isatty(STDOUT_FILENO)) {
        output = stdout;
    }

    if (parse_args(argc, argv, &args, output)) {

        /* create fdset and freemap */
        if ((fdset = fdset_new(0644, 0))
          && (fdset_set_type_name(fdset, 0, args.prefix, 
              str_len(args.prefix), 1) == FDSET_OK)
          && (freemap = freemap_new(FREEMAP_STRATEGY_FIRST, 0, fdset, addfile))
          && freemap_malloc(freemap, &fileno, &offset, &overhead, 
              FREEMAP_OPT_LOCATION | FREEMAP_OPT_EXACT, 0, 0)) {
            /* succeded, continue */
        } else {
            fprintf(output, "failed to init fstuff\n");
            if (fdset) {
                if (freemap) {
                    freemap_delete(freemap);
                }
                fdset_delete(fdset);
            }
            free_args(&args);
            return EXIT_FAILURE;
        }

        if (args.mode == MODE_BUILD) {
            if ((btree = iobtree_new(args.pagesize, args.leaf_strategy, 
                args.node_strategy, freemap, fdset, 0))) {

                build(btree, args.input, args.limit, args.entrysize);
                iobtree_flush(btree);
            } else {
                fprintf(output, "btree initialisation failed\n");
            }

            /* write out fileno and offset */
            iobtree_root(btree, &fileno, &offset);
            if (((fd = fdset_pin(fdset, 0, 0, 0, SEEK_SET)) >= 0)
              && (write(fd, &fileno, sizeof(fileno)) == sizeof(fileno))
              && (write(fd, &offset, sizeof(offset)) == sizeof(offset))
              && (write(fd, &args.pagesize, sizeof(args.pagesize)) 
                  == sizeof(args.pagesize))
              && (write(fd, &args.leaf_strategy, sizeof(args.leaf_strategy)) 
                  == sizeof(args.leaf_strategy))
              && (write(fd, &args.node_strategy, sizeof(args.node_strategy)) 
                  == sizeof(args.node_strategy))
              && (fdset_unpin(fdset, 0, 0, fd) == FDSET_OK)) {
                unsigned int pages,
                             leaves,
                             nodes;

                pages = iobtree_pages(btree, &leaves, &nodes);
                assert(pages == leaves + nodes);
                printf("%u leaves, %u nodes, %u pagesize, "
                  "%u disksize (%u overhead)\n", leaves, nodes, args.pagesize,
                  (unsigned int) freemap_space(freemap), overhead);
            } else {
                fprintf(output, "failed to write root info\n");
            }
        } else if (args.mode == MODE_SEARCH) {
            /* load up the btree */
            if (((fd = fdset_pin(fdset, 0, 0, 0, SEEK_SET)) >= 0)
              && (read(fd, &fileno, sizeof(fileno)) == sizeof(fileno))
              && (read(fd, &offset, sizeof(offset)) == sizeof(offset))
              && (read(fd, &args.pagesize, sizeof(args.pagesize)) 
                  == sizeof(args.pagesize))
              && (read(fd, &args.leaf_strategy, sizeof(args.leaf_strategy)) 
                  == sizeof(args.leaf_strategy))
              && (read(fd, &args.node_strategy, sizeof(args.node_strategy)) 
                  == sizeof(args.node_strategy))
              && (fdset_unpin(fdset, 0, 0, fd) == FDSET_OK)
              && (btree = iobtree_load(args.pagesize, args.leaf_strategy, 
                  args.node_strategy, freemap, fdset, 0, fileno, offset))) {
                search(btree, args.input, args.limit);
            } else {
                fprintf(output, "failed to read root info\n");
            }
        } else if (args.mode == MODE_PRINT) {
            /* load up the btree */
            if (((fd = fdset_pin(fdset, 0, 0, 0, SEEK_SET)) >= 0)
              && (read(fd, &fileno, sizeof(fileno)) == sizeof(fileno))
              && (read(fd, &offset, sizeof(offset)) == sizeof(offset))
              && (read(fd, &args.pagesize, sizeof(args.pagesize)) 
                  == sizeof(args.pagesize))
              && (read(fd, &args.leaf_strategy, sizeof(args.leaf_strategy)) 
                  == sizeof(args.leaf_strategy))
              && (read(fd, &args.node_strategy, sizeof(args.node_strategy)) 
                  == sizeof(args.node_strategy))
              && (fdset_unpin(fdset, 0, 0, fd) == FDSET_OK)
              && (btree = iobtree_load(args.pagesize, args.leaf_strategy, 
                  args.node_strategy, freemap, fdset, 0, fileno, offset))) {
                print(btree, stdout);
            } else {
                fprintf(output, "failed to read root info\n");
            }
        } else {
            fprintf(output, 
               "what do you want to do? (--build, --print, --search)\n");
        }

        if (btree) {
            iobtree_delete(btree);
        }
        fdset_delete(fdset);
        freemap_delete(freemap);
        free_args(&args);
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}

