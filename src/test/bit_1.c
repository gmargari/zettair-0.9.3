/* test_bit.c test the facilities implemented in bit.c, which implement fast
 * logical and mathematical operations based on the base 2 operations available
 * on pretty much all CPU's
 *
 * to run a set of automated tests, simply run the executable.  
 *
 * ./test_bit
 *
 * Otherwise the executable can be used for debugging by getting it to print 
 * out a table of the values of each of the functions available using 
 * ascending arguments
 *
 * ./test_bit [set|toggle|get|log2|mul2|div2|mod2|pow2|
 *   umask|lmask|tobyte|frombyte] var n
 *
 * where the first argument prints out a table containing the result of the
 * named function.  The bit manipulation functions (set, toggle, get)
 * can't accept values of n greater than 32.  
 * n specifies the size of the table printed out.
 *
 * written nml 2004-01-21
 *
 */

#include "bit.h"

#include <stdio.h>
#include <stdlib.h>

#define TEST(success)                                                         \
    if (!(success)) {                                                         \
        fprintf(stderr, "test on line %u failed in %s\n", __LINE__,           \
          __FILE__);                                                          \
        err = 1;                                                              \
    } else

int test_file(FILE *fp, int argc, char **argv) {
    int err = 0;

    if (argc < 2) {
        /* run automated test suite */
        if (fp) {
            fprintf(stderr, "file provided to stand-alone test bit_1\n");
            return 0;
        }

        /* set                          set0 */
        TEST(bit_set(1, 0, 1) == 0x01); TEST(bit_set(0xff, 0, 0) == 0xfe);
        TEST(bit_set(1, 1, 1) == 0x03); TEST(bit_set(0xff, 1, 0) == 0xfd);
        TEST(bit_set(1, 2, 1) == 0x05); TEST(bit_set(0xff, 2, 0) == 0xfb);
        TEST(bit_set(1, 3, 1) == 0x09); TEST(bit_set(0xff, 3, 0) == 0xf7);
        TEST(bit_set(1, 4, 1) == 0x11); TEST(bit_set(0xff, 4, 0) == 0xef);
        TEST(bit_set(1, 5, 1) == 0x21); TEST(bit_set(0xff, 5, 0) == 0xdf);

        /* toggle */
        TEST(bit_toggle(1, 0) == 0x00); TEST(bit_toggle(1, 1) == 0x03);
        TEST(bit_toggle(1, 2) == 0x05); TEST(bit_toggle(1, 3) == 0x09);
        TEST(bit_toggle(1, 4) == 0x11); TEST(bit_toggle(1, 5) == 0x21);
        TEST(bit_toggle(0xa5, 0) == 0xa4); TEST(bit_toggle(0xa5, 1) == 0xa7);
        TEST(bit_toggle(0xa5, 2) == 0xa1); TEST(bit_toggle(0xa5, 3) == 0xad);
        TEST(bit_toggle(0xa5, 4) == 0xb5); TEST(bit_toggle(0xa5, 5) == 0x85);

        /* get */
        TEST(bit_get(0xa5, 0) == 0x01); TEST(bit_get(0xa5, 1) == 0);
        TEST(bit_get(0xa5, 2) == 0x04); TEST(bit_get(0xa5, 3) == 0);
        TEST(bit_get(0xa5, 4) == 0); TEST(bit_get(0xa5, 5) == 0x20);

        /* log2 */
        TEST(bit_log2(0) == 0); TEST(bit_log2(1) == 0);
        TEST(bit_log2(2) == 1); TEST(bit_log2(3) == 1);
        TEST(bit_log2(4) == 2); TEST(bit_log2(5) == 2);
        TEST(bit_log2(6) == 2); TEST(bit_log2(7) == 2);
        TEST(bit_log2(8) == 3); TEST(bit_log2(15) == 3);
        TEST(bit_log2(16) == 4); TEST(bit_log2(17) == 4);
        TEST(bit_log2(31) == 4); TEST(bit_log2(32) == 5);
        TEST(bit_log2(33) == 5); TEST(bit_log2(1023) == 9);
        TEST(bit_log2(1024) == 10); TEST(bit_log2(1025) == 10);

        /* pow2 */
        TEST(bit_pow2(0) == 1); TEST(bit_pow2(1) == 2);
        TEST(bit_pow2(2) == 4); TEST(bit_pow2(3) == 8);
        TEST(bit_pow2(4) == 16); TEST(bit_pow2(5) == 32);
        TEST(bit_pow2(10) == 1024); TEST(bit_pow2(20) == 1048576);

        /* mul2 */
        TEST(bit_mul2(1, 0) == 1); TEST(bit_mul2(1, 1) == 2);
        TEST(bit_mul2(1, 2) == 4); TEST(bit_mul2(1, 3) == 8);
        TEST(bit_mul2(1, 4) == 16); TEST(bit_mul2(1, 5) == 32);
        TEST(bit_mul2(14, 4) == 224);

        /* div2 */
        TEST(bit_div2(0xff, 0) == 0xff); TEST(bit_div2(0xff, 1) == 0x7f);
        TEST(bit_div2(0xff, 2) == 0x3f); TEST(bit_div2(0xff, 3) == 0x1f);
        TEST(bit_div2(0xff, 4) == 0x0f); TEST(bit_div2(0xff, 5) == 0x07);
        TEST(bit_div2(5349784, 4) == 334361);

        /* mod2 */
        TEST(bit_mod2(168070, 0) == 0); TEST(bit_mod2(168070, 1) == 0);
        TEST(bit_mod2(168070, 2) == 2); TEST(bit_mod2(168070, 3) == 6);
        TEST(bit_mod2(168070, 4) == 6); TEST(bit_mod2(168070, 5) == 6);

        /* umask */
        TEST(bit_umask(0) == 0); TEST(bit_umask(1) == 0x80000000);

        /* lmask */
        TEST(bit_lmask(0) == 0); TEST(bit_lmask(1) == 0x01);
        TEST(bit_lmask(2) == 0x03); TEST(bit_lmask(3) == 0x07);
        TEST(bit_lmask(4) == 0x0f); TEST(bit_lmask(5) == 0x1f);

        /* tobyte */
        TEST(bit_to_byte(0 * 8) == 0); TEST(bit_to_byte(1 * 8) == 1);
        TEST(bit_to_byte(2 * 8) == 2); TEST(bit_to_byte(3 * 8) == 3);
        TEST(bit_to_byte(4 * 8) == 4); TEST(bit_to_byte(5 * 8) == 5);
 
        /* frombyte */
        TEST(bit_from_byte(0) == 0 * 8); TEST(bit_from_byte(1) == 1 * 8);
        TEST(bit_from_byte(2) == 2 * 8); TEST(bit_from_byte(3) == 3 * 8);
        TEST(bit_from_byte(4) == 4 * 8); TEST(bit_from_byte(5) == 5 * 8);

        TEST(bit_to_byte(bit_from_byte(43)) == 43);
        TEST(bit_to_byte(bit_from_byte(355)) == 355);
        TEST(bit_to_byte(bit_from_byte(6346)) == 6346);

        if (!err) {
            return 1;
        } else {
            return 0;
        }
    } else if ((argc == 4)) {
        unsigned int i,
                     n = atoi(argv[3]),
                     var = atoi(argv[2]);

        if (!strcmp(argv[1], "set")) {
            for (i = 0; i < n; i++) {
                printf("%u (0x%xh): %u (0x%xh)\n", i, i, 
                  bit_set(var, i, 1), bit_set(var, i, 1));
            }
            return 1;
        } else if (!strcmp(argv[1], "set0")) {
            for (i = 0; i < n; i++) {
                printf("%u (0x%xh): %u (0x%xh)\n", i, i, 
                  bit_set(var, i, 0), bit_set(var, i, 0));
            }
            return 1;
        } else if (!strcmp(argv[1], "toggle")) {
            for (i = 0; i < n; i++) {
                printf("%u (0x%xh): %u (0x%xh)\n", i, i, 
                  bit_toggle(var, i), bit_toggle(var, i));
            }
            return 1;
        } else if (!strcmp(argv[1], "get")) {
            for (i = 0; i < n; i++) {
                printf("%u (0x%xh): %u (0x%xh)\n", i, i, 
                  bit_get(var, i), bit_get(var, i));
            }
            return 1;
        } else if (!strcmp(argv[1], "log2")) {
            for (i = 0; i < n; i++) {
                printf("%u (0x%xh): %u (0x%xh)\n", i, i, 
                  bit_log2(i), bit_log2(i));
            }
            return 1;
        } else if (!strcmp(argv[1], "mul2")) {
            for (i = 0; i < n; i++) {
                printf("%u (0x%xh): %u (0x%xh)\n", i, i, 
                  bit_mul2(var, i), bit_mul2(var, i));
            }
            return 1;
        } else if (!strcmp(argv[1], "div2")) {
            for (i = 0; i < n; i++) {
                printf("%u (0x%xh): %u (0x%xh)\n", i, i, 
                  bit_div2(var, i), bit_div2(var, i));
            }
            return 1;
        } else if (!strcmp(argv[1], "mod2")) {
            for (i = 0; i < n; i++) {
                printf("%u (0x%xh): %u (0x%xh)\n", i, i, 
                  bit_mod2(var, i), bit_mod2(var, i));
            }
            return 1;
        } else if (!strcmp(argv[1], "pow2")) {
            for (i = 0; i < n; i++) {
                printf("%u (0x%xh): %u (0x%xh)\n", i, i, 
                  bit_pow2(i), bit_pow2(i));
            }
            return 1;
        } else if (!strcmp(argv[1], "umask")) {
            for (i = 0; i < n; i++) {
                printf("%u (0x%xh): %u (0x%xh)\n", i, i, 
                  bit_umask(i), bit_umask(i));
            }
            return 1;
        } else if (!strcmp(argv[1], "lmask")) {
            for (i = 0; i < n; i++) {
                printf("%u (0x%xh): %u (0x%xh)\n", i, i, 
                  bit_lmask(i), bit_lmask(i));
            }
            return 1;
        } else if (!strcmp(argv[1], "tobyte")) {
            for (i = 0; i < n; i++) {
                printf("%u (0x%xh): %u (0x%xh)\n", i, i, 
                  bit_to_byte(i), bit_to_byte(i));
            }
            return 1;
        } else if (!strcmp(argv[1], "frombyte")) {
            for (i = 0; i < n; i++) {
                printf("%u (0x%xh): %u (0x%xh)\n", i, i, 
                  bit_from_byte(i), bit_from_byte(i));
            }
            return 1;
        } else {
            /* unrecognised */
            fprintf(stderr, "unrecognised operation\n");
        }
    }

    printf("%s [set|toggle|get|log2|mul2|div2|mod2|pow2|umask|lmask|"
      "tobyte|frombyte] n (var)\n", *argv);

    return EXIT_FAILURE;
}

