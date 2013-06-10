/* metric.h contains signatures for all of the metrics currently known
 * in the system.  It contains functions of different prefixes, but it
 * beats having a different header file for each metric, since they
 * consist of a single function
 *
 * written nml 2005-05-31
 *
 */

#ifndef METRIC_H
#define METRIC_H

/* okapi metric */
const struct search_metric *okapi_k3();

/* dirichlet metric */
const struct search_metric *dirichlet();

/* pivoted cosine metric */
const struct search_metric *pcosine();

/* (incredibly simple) cosine metric */
const struct search_metric *cosine();

/* Dave Hawking's AF1 metric */
const struct search_metric *hawkapi();

#endif

