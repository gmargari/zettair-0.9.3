/* heap_1.c tests the heap code by using it to heapsort.  The input
 * format is a simple line based format:
 *
 * # comment
 * nel seed
 *
 * lines starting with hash are treated as comments and ignored.
 * other lines should contain one or two unsigned integers.  nel gives
 * the number of elements to sort and seed gives a random seed to use
 * (XXX: should really write a simple random number generator to
 * ensure the same sequence is generated in all locations)
 *
 * written nml 2003-02-25
 *
 */

#include "firstinclude.h"

#include "test.h"

#include "heap.h"
#include "str.h"
#include "lcrand.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void fillarr(unsigned int *arr, unsigned int elements, 
  unsigned int seed) {
    unsigned int i;
    struct lcrand *rand = lcrand_new(seed);

    assert(rand);

    for (i = 0; i < elements; i++) {
        arr[i] = lcrand(rand) % (elements << 1);
    }

    return;
}

static int cmp_int(const void *vone, const void *vtwo) {
    const unsigned int *one = vone,
                       *two = vtwo;

    if (*two < *one) {
        return 1;
    } else if (*one < *two) {
        return -1;
    } else {
        return 0;
    }
}

int test_file(FILE *fp, int argc, char **argv) {
    char buf[1024];
    char *ptr;
    unsigned int nel,
                 seed,
                 *arr;

    while (fgets((char *) buf, 1023, fp)) {
        ptr = (char *) str_ltrim(buf);
        str_rtrim(ptr);

        if (*ptr == '#') {
            /* its a comment, ignore it */
        } else if (sscanf((char *) ptr, "%u %u", &nel, &seed) == 2) {
            
            if (!(arr = malloc(sizeof(*arr) * nel))) {
                fprintf(stderr, "can't get memory\n");
            }

            fillarr(arr, nel, seed);

            /* sort */
            heap_sort(arr, nel, nel, sizeof(*arr), cmp_int);

            if (!heap_issorted(arr, nel, sizeof(*arr), cmp_int, 0)) {
                printf("arr is not sorted (nel %u seed %u)!\n", nel, seed);
            } 

            free(arr);

        } else if (str_len(ptr)) {
            fprintf(stderr, "couldn't understand '%s'\n", (char *) ptr);
            return 0;
        }
    }

    return 1;
}

