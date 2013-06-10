/* win32/firstinclude.h defines macros to hack basic POSIX stuff so that it
 * works on win32
 *
 * written nml 2004-07-14
 *
 */

#ifndef FIRSTINCLUDE_H
#define FIRSTINCLUDE_H

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64           /* large file support */
#endif
#define _BSD_SOURCE                    /* for snprintf */
#define _POSIX_SOURCE                  /* for lots of stuff */
#define _POSIX_C_SOURCE 199309L

#include "config.h"

#include <direct.h>
#include <io.h>

#define OS_SEPARATOR '\\'

typedef int ssize_t; 
typedef long int off_t; 

/* dummy timeval struct to make things compile */
struct timeval {
    long int tv_sec;
    long int tv_usec;
}; 

int gettimeofday(struct timeval *tp, void *vp);

#endif

