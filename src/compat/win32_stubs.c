/* win32_stubs.c creates trivial versions of some functions that we need for
 * portability
 *
 * written nml 2004-07-16
 *
 */

#include "firstinclude.h"

#include "def.h"
#include "config.h"

#include <time.h>

/* on windows we can use up to 4GB
 * (see http://support.microsoft.com/default.aspx?scid=kb;en-us;93496).  We
 * have to ifdef out the other stuff because windows doesn't have ftruncate. */
int getmaxfsize(int fd, unsigned int knownlimit, unsigned int *limit) {
    if (knownlimit > 4294967295U) {
        *limit = 4294967295U;
    } else {
        *limit = knownlimit;
    }
    return 1;
}

int gettimeofday(struct timeval *tp, void *vp) {
    tp->tv_sec = time(NULL);
    tp->tv_usec = 0;
    return 0;
}



