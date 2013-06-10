/* getmaxfsize.c implements a function to determine the maximum file size
 * of an open file.  We'll do this by seeking it to some different
 * locations and reading/writing to it.  Its important that we don't
 * alter the file during this process.
 *
 * written nml 2003-02-22
 *
 */

#include "firstinclude.h" 

#include "getmaxfsize.h"

#include "def.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

int getmaxfsize(int fd, unsigned int knownlimit, unsigned int *limit) {
    off_t fsize;                    /* current size of the file */
    off_t pos;                      /* current position in file */
    char buf = '\0';                /* writing buffer */

    /* we can't actually implement this function in an efficient, generalised
     * way without operating system help, so we're just going to try
     * some hueristics.  The big thing to worry about is the 2GB
     * filesize limit thats so prevalent on linux.  Basically we're
     * going to return the a limit of knownlimit, 2GB or 4GB,
     * depending on what works.  In other words, this function is just one big
     * hack. */

    /* XXX: we should really restore the previous signal handler before exiting
     * this function, but don't */
    if (signal(SIGXFSZ, SIG_IGN) == SIG_ERR) {
        return 0;
    }

    /* determine current position and size of the file */
    errno = 0;
    if ((pos = lseek(fd, 0, SEEK_CUR)) == (off_t) -1) {
        assert(errno);
        assert(!CRASH);
        return 0;
    }
    if ((fsize = lseek(fd, 0, SEEK_END)) == (off_t) -1) {
        assert(errno);
        assert(!CRASH);
        lseek(fd, pos, SEEK_SET);
        return 0;
    }

    /* test 2GB limit */
    if ((fsize < 2147483647) 
      && (lseek(fd, 2147483647 - 1, SEEK_SET) != (off_t) -1)) {
        errno = 0;
        if ((write(fd, &buf, 1) == -1) && (errno == EFBIG)) {
            /* limited to less than 2GB, binary search downward to
             * find an acceptable size */
            unsigned int size;

            if (knownlimit < 2147483647) {
                *limit = knownlimit;
                return (lseek(fd, pos, SEEK_SET) != (off_t) -1)
                  && (ftruncate(fd, fsize) != -1);
            }

            for (size = 2147483647 / 2; size > fsize; size /= 2) {
                if (lseek(fd, size - 1, SEEK_SET) != (off_t) -1) {
                    if (write(fd, &buf, 1) != -1) {
                        if ((ftruncate(fd, fsize) != -1) 
                          && (lseek(fd, pos, SEEK_SET) != (off_t) -1)) {
                            *limit = size;
                            return (lseek(fd, pos, SEEK_SET) != (off_t) -1)
                              && (ftruncate(fd, fsize) != -1);
                        } else {
                            assert(!CRASH);
                            lseek(fd, pos, SEEK_SET);
                            ftruncate(fd, fsize);
                            return 0;
                        }
                    } else if (errno != EFBIG) {
                        assert(!CRASH);
                        lseek(fd, pos, SEEK_SET);
                        ftruncate(fd, fsize);
                        return 0;
                    } 
                } else {
                    assert(!CRASH);
                    lseek(fd, pos, SEEK_SET);
                    ftruncate(fd, fsize);
                    return 0;
                }
            }

            /* couldn't find a limit */
            *limit = fsize;
            return 1;
        } else if (errno == EBADF) {
            /* fd probably isn't open for writing */
            *limit = fsize;
            return (lseek(fd, pos, SEEK_SET) != (off_t) -1)
              && (ftruncate(fd, fsize) != -1);
        }
    } else if ((fsize < 2147483647)) {
        assert(!CRASH);
        lseek(fd, pos, SEEK_SET);
        ftruncate(fd, fsize);
        return 0;
    }
 
    if (knownlimit < 4294967295U) {
        /* if knownlimit is less than 4GB then its probably correct */
        *limit = knownlimit;
        return (lseek(fd, pos, SEEK_SET) != (off_t) -1)
          && (ftruncate(fd, fsize) != -1);
    }

    /* test 4GB limit */
    if ((fsize < 4294967295U) && (((off_t) (4294967295U - 1)) > 0) 
      && (lseek(fd, 4294967295U - 1, SEEK_SET) != (off_t) -1)) {
        if ((write(fd, &buf, 1) == -1) && (errno == EFBIG)) {
            /* limited to less than 4GB, but more than 2GB, just use 2GB */
            *limit = 2147483647;
            return (lseek(fd, pos, SEEK_SET) != (off_t) -1)
              && (ftruncate(fd, fsize) != -1);
        } 
    } else if ((fsize < 4294967295U)) {
        /* check if off_t values higher than 2GB go negative */
        if (((off_t) (4294967295U - 1)) < 0) {
            *limit = 2147483647;
            return (lseek(fd, pos, SEEK_SET) != (off_t) -1)
              && (ftruncate(fd, fsize) != -1);
        } else {
            assert(!CRASH);
            lseek(fd, pos, SEEK_SET);
            ftruncate(fd, fsize);
            return 0;
        }
    }

    /* otherwise, limited by 32-bit offset size, return 4GB */
    *limit = 4294967295U;
    return (lseek(fd, pos, SEEK_SET) != (off_t) -1)
      && (ftruncate(fd, fsize) != -1);
}

#ifdef GETMAXFSIZE_TEST

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

int main(int argc, char **argv) {
    int fd;
    struct rlimit limits;
    unsigned int limit;
    struct sigaction sa;
    int flags = 0;
    unsigned int i;

    if (argc != 3) {
        fprintf(stderr, "usage: %s [r|w|rw] filename\n", *argv);
        return EXIT_FAILURE;
    }

    /* need to arrange to ignore SIGXFSZ, so we don't exit when we try to write
     * past 2GB or whatever the maximum file size is */
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGXFSZ, &sa, NULL) != 0) {
        perror(*argv);
        return EXIT_FAILURE;
    }

    for (i = 0; i < strlen(argv[1]); i++) {
        if ((argv[1][i] == 'r') || (argv[1][i] == 'R')) {
            if (flags & O_WRONLY) {
                flags = O_RDWR;
            } else {
                flags = O_RDONLY;
            }
        } else if ((argv[1][i] == 'w') || (argv[1][i] == 'W')) {
            if (flags & O_RDONLY) {
                flags = O_RDWR;
            } else {
                flags = O_WRONLY;
            }
        } else {
            fprintf(stderr, "usage: %s [r|w|rw] filename\n", *argv);
            return EXIT_FAILURE;
        }
    }

    if ((fd = open(argv[2], flags | O_CREAT, 0xffffffff))) {
        if ((getrlimit(RLIMIT_FSIZE, &limits) == 0) 
          && (getmaxfsize(fd, limits.rlim_cur, &limit))) { 
            printf("limit for file '%s' is %u\n", argv[1], limit);
        } else {
            close(fd);
            perror(*argv);
            return EXIT_FAILURE;
        }
        close(fd);
    } else {
        perror(*argv);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#endif


