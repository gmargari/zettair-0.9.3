/* various utility functions and objects for the regression tests.
 *
 * XXX these functions are oriented towards quickly writing standalone
 * test programs, and should _not_ be used in a production system.
 * In particular, the ones with names ending "_or_die" will abort on
 * failure.
 *
 * written wew 2004-10-12
 *
 */

#ifndef TESTUTILS_H
#define TESTUTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lcrand.h"

/* Random number generation. */

extern struct lcrand * tu_rand;

void tu_init_rand_or_die(unsigned seed);

unsigned int tu_rand_limit(unsigned limit);

double tu_rand_double_limit(double limit);

/* Input data. */

#define TU_SAMPLE_DATA_LEN (2u * 1024u * 1024u)

extern char tu_sample_data[];

extern int tu_sample_data_inited;

void tu_sample_data_rand_init();

int tu_sample_data_file_init(const char * fname);

char * tu_get_rand_data(unsigned len);

char * tu_get_rand_data_rand_len(unsigned max_len, unsigned * len);

/* Storing sample data. */

struct tu_data_element {
    char * data;
    unsigned len;
};

struct tu_data_elements {
    struct tu_data_element * elements;
    unsigned num_elements;
    struct tu_data_elements * next;
};

struct tu_data_elements_list {
    struct tu_data_elements * head;
    struct tu_data_elements * tail;
};

struct tu_data_elements * tu_generate_elements_or_die(unsigned num_elements,
  unsigned max_len);

struct tu_data_elements * tu_generate_null_elements_or_die(unsigned
  num_elements);

void tu_delete_elements(struct tu_data_elements * elements);

void tu_add_elements_to_list(struct tu_data_elements_list * list,
  struct tu_data_elements * elements);

#define TU_DATA_ELEMENTS_EMPTY_LIST { NULL, NULL }

#ifdef __cplusplus
}
#endif

#endif

