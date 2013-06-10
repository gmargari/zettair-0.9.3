/* test.h declares a basic test harness structure (that is compatible
 * with the automake test suite stuff that we're currently using)
 *
 * written nml 2003-10-14
 *
 */

#ifndef TEST_H
#define TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

/* automake understands a return code of 77 to mean that the test doesn't 
 * count */
#define EXIT_DOESNT_COUNT 77

/* test a module using the contents of fp.  fp can be NULL if no
 * matching file is found.  argc and argv are the normal parameters
 * received by the executing program.  Individual unit tests are responsible for
 * implementing this function to test whatever it is they have to test.  Return
 * value is taken as an indication of success, with EXIT_DOESNT_COUNT indicating
 * that the test case result should be ignored. */
int test_file(FILE *fp, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif

