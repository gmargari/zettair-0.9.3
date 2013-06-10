/* impactify.c adds impact-ordered inverted lists to index.
 *
 * written wew 2005-01-19
 */

#include "firstinclude.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "index.h"
#include "impact_build.h"
#include "error.h"

int main(int argc, char ** argv) {
    char * name;
    /* will be options later. */
    /*double pivot = DEFAULT_PIVOT;
    double slope = DEFAULT_SLOPE;
    unsigned int quant_bits = DEFAULT_QUANT_BITS; */
    enum impact_ret impact_ret;
    struct index * index;
    struct index_load_opt lopt;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <index-prefix>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    memset(&lopt, 0, sizeof(lopt));

    name = argv[1];
    index = index_load(name, 0, INDEX_LOAD_NOOPT, &lopt);
    if (index == NULL) {
        fprintf(stderr, "Error loading index with prefix '%s'\n", name);
        exit(EXIT_FAILURE);
    }
    impact_ret = impact_order_index(index);
    if (impact_ret != IMPACT_OK) {
        ERROR1("impactification of %s failed", name);
        exit(EXIT_FAILURE);
    }
    return EXIT_SUCCESS;
}
