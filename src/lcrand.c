/* lcrand.c implements a simple linear congruential psuedo-random
 * number generator.  See lcrand.h for more detail.
 *
 * written nml 2004-08-27
 *
 */

#include "firstinclude.h"

#include "lcrand.h"

#include "zstdint.h"

#include <stdlib.h>

struct lcrand {
    unsigned int a;
    unsigned int b;
    uint32_t state;
    uint32_t seed;
};

struct lcrand *lcrand_new(unsigned int seed) {
    struct lcrand *prng;

    if ((prng = malloc(sizeof(*prng)))) {
        /* recommended by numerical recipes in c */
        prng->a = 1664525;
        prng->b = 1013904223;
        prng->state = prng->seed = seed;
    }

    return prng;
}

struct lcrand *lcrand_new_custom(unsigned int seed, unsigned int a, 
  unsigned int b) {
    struct lcrand *prng;

    if ((prng = malloc(sizeof(*prng)))) {
        prng->a = a;
        prng->b = b;
        prng->state = prng->seed = seed;
    }

    return prng;
}

void lcrand_delete(struct lcrand *prng) {
    free(prng);
}

unsigned int lcrand(struct lcrand *prng) {
    return prng->state = prng->a * prng->state + prng->b;
}

unsigned int lcrand_limit(struct lcrand *prng, unsigned int limit) {
    return (unsigned) ((double) limit * lcrand(prng)/(LCRAND_MAX + 1.0));
}

unsigned int lcrand_seed(struct lcrand *prng) {
    return prng->seed;
}

#ifdef LCRAND_TEST

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

int main(int argc, char **argv) {
    struct lcrand *rand;
    unsigned int num,
                 i;

    if ((rand = lcrand_new((num = time(NULL))))) {
        printf("seed = %u\n", num);

        for (i = 0; i < 100; i++) {
            num = lcrand(rand);
            printf("%u\n", num);
        }

        lcrand_delete(rand);
        return EXIT_SUCCESS;
    } else {
        fprintf(stderr, "failed to initialise lcrand PRNG\n");
        return EXIT_SUCCESS;
    }
}

#endif

