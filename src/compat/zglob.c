/* zglob.c implements something approximating POSIX glob calls using
 * win32 system calls.
 *
 * written nml 2004-11-23
 *
 */

#include "firstinclude.h"

#include "glob.h"

#include "str.h"

#include <assert.h>
#include <windows.h>

int glob(const char *pattern, int flags, 
  int(*errfunc)(const char *epath, int eerrno), glob_t *pglob) {
    WIN32_FIND_DATA fdata;
    HANDLE findh = INVALID_HANDLE_VALUE;
    int err;
    unsigned int startmatches = 0;
    char *copy;

    if ((flags & GLOB_APPEND)) {
        startmatches = pglob->gl_pathc;
    } else {
        unsigned int alloc,
                     i;

        if (!(flags & GLOB_DOOFFS)) {
            pglob->gl_offs = 0;
        }

        pglob->gl_pathsize = 1;
        pglob->gl_pathc = 0;
        alloc = pglob->gl_offs + pglob->gl_pathsize;

        if ((pglob->gl_pathv = malloc(alloc * sizeof(char *)))) {
            for (i = 0; i < pglob->gl_offs; i++) {
                pglob->gl_pathv[i] = NULL;
            }
        } else {
            return GLOB_NOSPACE;
        }
    }

    if ((findh = FindFirstFile(pattern, &fdata)) != INVALID_HANDLE_VALUE) {
        do {
            /* copy matching filename */
            if ((fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
              && (flags & GLOB_MARK)) {
                unsigned int len = str_nlen(fdata.cFileName, MAX_PATH);

                /* copy and mark directory */
                if ((copy = malloc(len + 2))) {
                    memcpy(copy, fdata.cFileName, len);
                    copy[len] = '\\';
                    copy[len + 1] = '\0';
                } else {
                    pglob->gl_pathc = startmatches;
                    return GLOB_NOSPACE;
                }
            } else {
                /* copy and mark directory */
                if ((copy = str_ndup(fdata.cFileName, MAX_PATH))) {
                    /* copy succeded, do nothing */
                } else {
                    pglob->gl_pathc = startmatches;
                    return GLOB_NOSPACE;
                }
            }

            /* expand array if necessary */
            if (pglob->gl_pathc + pglob->gl_offs >= pglob->gl_pathsize) {
                void *ptr;
                unsigned int newsize 
                  = pglob->gl_offs + (pglob->gl_pathsize * 2);

                if ((ptr = realloc(pglob->gl_pathv, 
                    sizeof(char *) * newsize))) {

                    pglob->gl_pathv = ptr;
                    pglob->gl_pathsize = newsize;

                } else {
                    pglob->gl_pathc = startmatches;
                    free(copy);
                    return GLOB_NOSPACE;
                }
            }

            assert(copy);
            assert(pglob->gl_pathc + pglob->gl_offs < pglob->gl_pathsize);

            /* insert match */
            pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc++] = copy;

        } while (FindNextFile(findh, &fdata) != 0);

        err = GetLastError();
        FindClose(findh);

        if (err == ERROR_NO_MORE_FILES) {
            return GLOB_OK;
        } else {
            pglob->gl_pathc = startmatches;
            printf("glob abort unexpected\n");
            return GLOB_ABORTED;
        }
    } else {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            /* nothing matched */
            if (flags & GLOB_NOCHECK) {
                if ((copy = str_dup(pattern))) {
                    /* expand array if necessary */
                    if (pglob->gl_pathc + pglob->gl_offs 
                      >= pglob->gl_pathsize) {
                        void *ptr;
                        unsigned int newsize 
                          = pglob->gl_offs + (pglob->gl_pathsize * 2);

                        if ((ptr = realloc(pglob->gl_pathv, 
                            sizeof(char *) * newsize))) {

                            pglob->gl_pathv = ptr;
                            pglob->gl_pathsize = newsize;

                        } else {
                            pglob->gl_pathc = startmatches;
                            free(copy);
                            return GLOB_NOSPACE;
                        }
                    }

                    /* insert pattern */
                    pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc++] = copy;
                    return GLOB_OK;
                } else {
                    return GLOB_NOSPACE;
                }
            } else {
                return GLOB_NOMATCH;
            }
        } else {
            return GLOB_ABORTED;
        }
    }
}

void globfree(glob_t *pglob) {
    unsigned int i;

    if (pglob->gl_pathv) {
        for (i = pglob->gl_offs; i < pglob->gl_offs + pglob->gl_pathc; i++) {
            if (pglob->gl_pathv[i]) {
                free(pglob->gl_pathv[i]);
            }
        }
        free(pglob->gl_pathv);
    }
}

#ifdef GLOB_TEST
/* small application to test globbing */
int main(int argc, char **argv) {
    glob_t g;
    unsigned int i;
    enum glob_error gret;

    for (i = 1; i < argc; i++) {
        gret 
          = glob(argv[i], GLOB_MARK | ((i == 1) ? 0 : GLOB_APPEND), NULL, &g);

        switch (gret) {
        case GLOB_OK:
            break;

        case GLOB_ABORTED:
            fprintf(stderr, "error globbing '%s'\n", argv[i]);
            return EXIT_FAILURE;

        case GLOB_NOMATCH:
            printf("no match for '%s'\n", argv[i]);
            break;

        case GLOB_NOSPACE:
            fprintf(stderr, "failed to allocated space for glob\n");
            globfree(&g);
            return EXIT_FAILURE;
        };
    }

    if (argc > 1) {
        for (i = 0; i < g.gl_pathc; i++) {
            printf("%s\n", g.gl_pathv[i]);
        }
        printf("\n");
        globfree(&g);
    }

    return EXIT_SUCCESS;
}

#endif

