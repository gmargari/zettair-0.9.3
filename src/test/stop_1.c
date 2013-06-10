/*  
 *  unit test for the stop library.
 *
 *  the input is the name of a stoplist file.  Currently, we only 
 *  test creating then destroy this file.
 */

#include "firstinclude.h"
#include "test.h"
#include "error.h"
#include "stop.h"
#include "str.h"

#include <stdio.h>

int test_file(FILE * fp, int argc, char ** argv) {
    char fname[1024];
    struct stop * stop;

    if (!fgets(fname, 1024, fp)) {
        ERROR("reading filename from input stream");
        return 0;
    }
    str_rtrim(fname);
    stop = stop_new_file(NULL, NULL, fname);
    if (!stop) {
        ERROR1("opening stoplist file '%s'", fname);
        return 0;
    }
    stop_delete(stop);
    return 1;
}
