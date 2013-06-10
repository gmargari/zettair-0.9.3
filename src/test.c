/* test.c implements the main function for unit tests, so that it runs
 * correctly under the automake testing functions
 *
 * written nml 2003-10-14
 *
 */

#include "firstinclude.h"

#include "test.h"
#include "getlongopt.h"
#include "zglob.h"

#include <assert.h> 
#include <errno.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* internal function to open a file specified in the arguments */
static FILE *argfile(int argc, char **argv, FILE *output) {
    FILE *fp;
    struct getlongopt *parser;   /* option parser */
    int id;                      /* id of parsed option */
    const char *arg;             /* argument of parsed option */
    enum getlongopt_ret ret;     /* return value from option parser */

    struct getlongopt_opt opts[] = {
        {"input", '\0', GETLONGOPT_ARG_REQUIRED, 'i'}
    };

    if ((parser = getlongopt_new(argc - 1, (const char **) &argv[1], opts, 
        sizeof(opts) / sizeof(*opts)))) {
        /* succeeded, do nothing */
    } else {
        fprintf(output, "failed to initialise options parser\n");
        exit(EXIT_FAILURE);
    }

    while ((ret = getlongopt(parser, &id, &arg)) == GETLONGOPT_OK) {
        switch (id) {
        case 'i':
            /* specified input */
            if ((fp = fopen(arg, "rb"))) {
                getlongopt_delete(parser);
                return fp;
            } else {
                fprintf(output, 
                  "unable to open file '%s': %s\n", arg, strerror(errno));
                exit(EXIT_FAILURE);
            }

        default: assert(0);
        }
    }

    getlongopt_delete(parser);
    return NULL;
}

int main(int argc, char **argv) {
    FILE *fp;
    unsigned int i,
                 len,
                 files = 0,
                 ret = 1;
    char *srcdir;
    char buf[FILENAME_MAX + 1];
    glob_t globbuf;

    /* check for srcdir environmental variable which indicates that
     * we're running in an automake test case, and that we should
     * locate test files ourselves */
    if ((srcdir = getenv("srcdir"))) {
        /* glob files in directory to find ones we want to run over */
        snprintf(buf, FILENAME_MAX, "%s/%s.*", srcdir, *argv);
        globbuf.gl_offs = 1;
        if (glob(buf, 0, NULL, &globbuf) != 0) {
            perror("glob failed");
            return EXIT_FAILURE;
        }
        len = strlen(srcdir) + 1 + strlen(*argv) + 1;

        /* traverse the glob */
        for (i = 0; i < globbuf.gl_pathc; i++) {
            /* don't execute on .c and .o files */
            if (strcmp(globbuf.gl_pathv[i] + len, "c") 
              && strcmp(globbuf.gl_pathv[i] + len, "o")) {
                if ((fp = fopen(globbuf.gl_pathv[i], "rb"))) {
                    files++;
                    if (!(ret = test_file(fp, argc, argv))) {
                        fprintf(stderr, "failed in globbed file %s\n", 
                          globbuf.gl_pathv[i]);
                        /* don't bother cleaning up */
                        return EXIT_FAILURE;
                    }
                    fclose(fp);
                } else {
                    fprintf(stderr, "couldn't open globbed file %s: %s\n", 
                      globbuf.gl_pathv[i], strerror(errno));
                    /* don't bother cleaning up */
                    return EXIT_FAILURE;
                }
            }
        }

        globfree(&globbuf);

        if (!files && !(ret = test_file(NULL, argc, argv))) {
            fprintf(stderr, "testing with no file failed\n");
            return EXIT_FAILURE;
        }

    /* use specified file if there is one */
    } else if ((fp = argfile(argc, argv, stderr))) {
        if (!(ret = test_file(fp, argc, argv))) {
            fclose(fp);
            fprintf(stderr, "specified file failed test\n");
            return EXIT_FAILURE;
        }
        fclose(fp);

    /* fallback to stdin */
    } else {
        if (!(ret = test_file(stdin, argc, argv))) {
            fprintf(stderr, "stdin failed test\n");
            return EXIT_FAILURE;
        }
    }

    if (ret == EXIT_DOESNT_COUNT) {
        return EXIT_DOESNT_COUNT;
    } else {
        return EXIT_SUCCESS;
    }
}

