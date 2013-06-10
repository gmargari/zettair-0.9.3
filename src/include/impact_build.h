/* Adding impact-ordered inverted lists to an index.
 *
 * written wew 2005-01-19
 */

#ifndef IMPACT_BUILD_H
#define IMPACT_BUILD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "index.h"

#define IMPACT_DEFAULT_PIVOT      0.2
#define IMPACT_DEFAULT_SLOPE      0.0
#define IMPACT_DEFAULT_QUANT_BITS 5

enum impact_ret {
    IMPACT_OK = 0,
    IMPACT_FMT_ERROR = -1,   /* error with format of input index */
    IMPACT_IO_ERROR = -2,    /* I/O error */
    IMPACT_MEM_ERROR = -3,   /* out of memory */
    IMPACT_OTHER_ERROR = -10 /* some other error */
};

enum impact_ret impact_order_index(struct index *idx);

double impact_normalise(double impact, double norm_B, double slope, 
  double max_impact, double min_impact);

unsigned int impact_quantise(double impact, unsigned int quant_bits, 
  double max_impact, double min_impact);

#ifdef __cplusplus
}
#endif

#endif

