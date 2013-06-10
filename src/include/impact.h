/* Adding impact-ordered querying of inverted lists 
 *
 * written jyiannis 2005-01-25
 *
 */

#ifndef IMPACT_H
#define IMPACT_H

#ifdef __cplusplus
extern "C" {
#endif

struct index;
struct query;
struct chash;
struct alloc;

int impact_ord_eval(struct index *idx, struct query *query, 
  struct chash *accumulators, unsigned int acc_limit, struct alloc *alloc, 
  unsigned int mem);

#ifdef __cplusplus
}
#endif

#endif

