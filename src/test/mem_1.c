/* mem_1 does some (static) tests to ensure that the mem functions and macros
 * work properly
 *
 * written nml 2003-02-03
 *
 */

#include "firstinclude.h"

#include "test.h"
#include "mem.h"
#include "_mem.h"

#include <inttypes.h>

int test_file(FILE *fp, int argc, char **argv) {
    unsigned int i,
                 num,
                 num2;

    union {
        unsigned int unum;
        unsigned char uchar[1];
    } u;

    /* ensure that we aren't testing from a file */
    if ((fp && (fp != stdin)) || (argc > 1)) {
        return 0;
    }

    /* assign number in network byte order */
    for (i = 0; i < sizeof(int); i++) {
        u.uchar[i] = i + 1;         /* assign 1 2 3 4 etc to num, big-endian */
    }

    /* convert it */
    mem_ntoh(&num, &u.unum, sizeof(num));

    /* read it in host byte order */
    for (i = 0; i < sizeof(int); i++) {
        if (((num >> (8 * i)) & 0xff) != sizeof(int) - i) {
            return 0;
        }
    }

    /* convert it */
    u.unum = 0;
    mem_hton(&u.unum, &num, sizeof(num2));
    for (i = 0; i < sizeof(int); i++) {
        if (u.uchar[i] != i + 1) {
            return 0;
        }
    }

    /* assign number in network byte order */
    for (i = 0; i < sizeof(int); i++) {
        u.uchar[i] = i + 1;         /* assign 1 2 3 4 etc to num, big-endian */
    }

    /* convert it */
    MEM_NTOH(&num, &u.unum, sizeof(num));

    /* read it in host byte order */
    for (i = 0; i < sizeof(int); i++) {
        if (((num >> (8 * i)) & 0xff) != sizeof(int) - i) {
            return 0;
        }
    }

    /* convert it */
    u.unum = 0;
    MEM_HTON(&u.unum, &num, sizeof(num2));
    for (i = 0; i < sizeof(int); i++) {
        if (u.uchar[i] != i + 1) {
            return 0;
        }
    }

    return 1;
}

