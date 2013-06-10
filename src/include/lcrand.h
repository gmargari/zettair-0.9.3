/* lcrand.h declares a module to generate linear congruential
 * psuedo-random (henceforth PR) numbers.  Its included in the search engine
 * more for repeatability of some algorithms that require random numbers than
 * anything else.  Linear congruential generators take the form 
 *
 *   x(n) = (a * x(n - 1) + b) mod c
 *
 * where a, b and c are parameters that can be altered to form
 * different PR sequences.  This module sets c to 2^32, which should be
 * efficient on modern hardware.  Bad choices of a and b can result in
 * extremely non-random sequences.  Some good defaults have been
 * provided below to get long PR sequences.  The generated numbers have 
 * something resembling uniform distribution.
 *
 * Linear-congruential generators have been shown to be shockingly bad
 * psuedo-random number generators (PRNG's) from a number of
 * perspectives.  Even the best choices of parameters cannot result in
 * a non-repeating sequence of longer than 2^32, and they expose a lot of 
 * state as they generate numbers, making them totally unsuitable for anything
 * resembling cryptographic use.  PRNG's like the (currently popular)
 * mersenne twister are both faster and more secure than linear
 * congruential generators.  However, its a hell of a lot more effort
 * to write ;o).  Linear congruential generators can be random enough
 * for some purposes.  One thing to be careful of in particular is that
 * the low-order bits of the number can show less randomness than the
 * high-order bits.  Numerical Recipes in c has this to say:
 *
 *            "If you want to generate a random integer between 1
 *            and 10, you should always do it by using high-order
 *            bits, as in
 *
 *                   j=1+(int) (10.0*rand()/(RAND_MAX+1.0));
 *
 *            and never by anything resembling
 *
 *                   j=1+(rand() % 10);
 *
 *            (which uses lower-order bits)."
 *
 * Note in this case RAND_MAX is LCRAND_MAX.
 *
 * More information on linear congruential generators can be found at 
 * http://en.wikipedia.org/wiki/Linear_congruential_generator, and
 * PRNG's in general at http://en.wikipedia.org/wiki/PRNG.  Theres lots
 * of maths associated with PRNGs, which can be found in good number
 * theory textbooks.
 *
 * "Anyone who considers arithmetical methods of producing random digits is, 
 * of course, in a state of sin".
 *
 *   -- John von Neumann
 *
 * written nml 2004-08-27
 *
 */

#ifndef LCRAND_H
#define LCRAND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>

/* define the largest number that can be generated */
#define LCRAND_MAX UINT_MAX

struct lcrand;

/* create a new PRNG with default parameters and given seed */
struct lcrand *lcrand_new(unsigned int seed);

/* create a new PRNG with given parameters and given seed */
struct lcrand *lcrand_new_custom(unsigned int seed, unsigned int a, 
  unsigned int b);

/* delete the random number generator */
void lcrand_delete(struct lcrand *prng);

/* get the next psuedo-random number */
unsigned int lcrand(struct lcrand *prng);

/* get a psuedo-random unsigned int n the range [0, limit) */
unsigned int lcrand_limit(struct lcrand *prng, unsigned int limit);

/* get the seed used to seed the random number generator */
unsigned int lcrand_seed(struct lcrand *prng);

#ifdef __cplusplus
}
#endif

#endif

