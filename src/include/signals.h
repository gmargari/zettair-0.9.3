/* signals.h declares a set of functions for convenient handling of signals,
 * primarily during index construction
 *
 * written wew 2004-06-18
 *
 */

#ifndef SIGNALS_H
#define SIGNALS_H

#include "index.h"
#include "pyramid.h"

void signals_set_index_under_construction(struct index * index);
void signals_clear_index_under_construction(void);
void signals_cleanup_handler(int signal);

#endif

