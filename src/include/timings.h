/* timings.h includes tools for getting timings
 *
 * written nml 2004-04-04
 *
 */

#ifndef TIMINGS_H
#define TIMINGS_H

#include "def.h"

#ifndef WIN32
#include <sys/time.h>
#endif

/* unfortunately, windows doesn't have the same simple timing functions as
 * pretty much anything else.  For the moment we'll just disable the timing
 * macros on windows */

#if defined(TIME_BUILD) && !defined(WIN32)

#define TIMINGS_DECL() struct timeval timings_now, timings_then;              \
                       unsigned long int timings_diff   

#define TIMINGS_START() gettimeofday(&timings_then, NULL)

#define TIMINGS_END(name)                                                     \
    gettimeofday(&timings_now, NULL);                                         \
    timings_diff = timings_now.tv_sec - timings_then.tv_sec;                  \
    printf("%s time: %02i:%02i:%02i (%lu seconds, %lu millis)\n", name,       \
      (int) timings_diff / 60 / 60, ((int) (timings_diff) / 60) % 60,         \
      ((int) timings_diff) % 60, timings_diff,                                \
      timings_now.tv_usec - timings_then.tv_usec + timings_diff * 1000000)

#else

#define TIMINGS_DECL() while (0)
#define TIMINGS_START() while (0)
#define TIMINGS_END(name) while (0)

#endif

#endif

