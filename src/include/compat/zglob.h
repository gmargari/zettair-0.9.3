/* zglob.h declares an interface similar to POSIX glob functionality,
 * using win32 system calls.  Note that we can't use the name glob, because
 * otherwise we can't include the system glob header file.
 *
 * written nml 2004-11-23
 *
 */

#ifndef ZGLOB_H
#define ZGLOB_H

#ifndef WIN32
/* we just use the provided implementation on most platforms */

#include <glob.h>

#else
/* we provide our own implementation on win32 */

#include <stdlib.h>   /* for size_t */

typedef struct {
    size_t gl_pathsize;         /* capacity of pathv array */
    size_t gl_pathc;            /* current size of pathv array */
    char **gl_pathv;            /* array of matching filenames */
    size_t gl_offs;             /* number of leading NULL's to insert into 
                                 * gl_pathv if GLOB_DOOFFS is set */
} glob_t;

/* create or add to a specific glob structure (given by pglob).  The
 * glob structure must point to a valid structure with correct
 * alignment and with enough space to hold a glob_t.  Returns 0 on
 * success, or an element from glob_error on failure.  glob structures
 * are guaranteed to be usable after both errors and successful
 * returns.  See glob_flags for various flags and their effects.  
 * Note that this version of glob *always* behaves as if GLOB_ERR is
 * on, so errfunc isn't used and all errors halt the glob */
int glob(const char *pattern, int flags, 
  int (*errfunc)(const char *epath, int eerrno), glob_t *pglob);

/* free memory associated with a pglob structure */
void globfree(glob_t *pglob);

enum glob_flags {
    GLOB_APPEND = (1 << 0),     /* append to previous glob() results */
    GLOB_DOOFFS = (1 << 1),     /* reserve gl_offs number of spaces in gl_pathv
                                 * before glob results (must be used
                                 * consistently for multiple glob()
                                 * calls with GLOB_APPEND) */
    GLOB_MARK = (1 << 3),       /* mark directories with trailing '/' */
    GLOB_NOCHECK = (1 << 4)     /* if the pattern doesn't match anything, 
                                 * then glob() returns a single result, 
                                 * which is the original pattern */
#if 0
    /* these options are in POSIX glob, but currently not supported in
     * this version */
    GLOB_ERR = (1 << 2),        /* stop on errors */
    GLOB_NOESCAPE = (1 << 5),   /* disable backslash escaping */
    GLOB_NOSORT = (1 << 6)      /* don't sort according to LC_COLLATE */
#endif
};

enum glob_error {
    GLOB_OK = 0,                /* successful */
    GLOB_ABORTED = -1,          /* GLOB_ERR was set or errfunc returned 
                                 * non-zero */
    GLOB_NOMATCH = -2,          /* no matches, and GLOB_NOCHECK wasn't set */
    GLOB_NOSPACE = -3,          /* failed to allocate memory */
};

#endif

#endif

