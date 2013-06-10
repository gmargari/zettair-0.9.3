#include "firstinclude.h"
#include "testutils.h"
#include "error.h"
#include "str.h"

#include <assert.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

struct lcrand * tu_rand = NULL;

void tu_init_rand_or_die(unsigned seed) {
    if (tu_rand != NULL)
        lcrand_delete(tu_rand);
    tu_rand = lcrand_new(seed);
    if (tu_rand == NULL) {
        ERROR("allocating tu_rand");
        exit(EXIT_FAILURE);
    }
}

unsigned int tu_rand_limit(unsigned limit) {
    return lcrand_limit(tu_rand, limit);
}

double tu_rand_double_limit(double limit) {
    return ((double) limit * lcrand(tu_rand)/(LCRAND_MAX + 1.0));
}

char tu_sample_data[TU_SAMPLE_DATA_LEN];

int tu_sample_data_inited = 0;

static int tu_sample_data_pos = 0;

void tu_sample_data_rand_init() {
    unsigned i;
    for (i = 0; i < TU_SAMPLE_DATA_LEN; i++) {
        tu_sample_data[i] = tu_rand_limit(256u);
    }
    tu_sample_data_inited = 1;
}

int tu_sample_data_file_init(const char * fname) {
    int fd;
    unsigned total_read = 0;
    int nread;
    fd = open(fname, O_RDONLY);
    if (fd < 0) {
        ERROR1("opening '%s' for sample data", fname);
        return 0;
    }
    while (total_read < TU_SAMPLE_DATA_LEN) {
        nread = read(fd, tu_sample_data + total_read, 
          TU_SAMPLE_DATA_LEN - total_read);
        if (nread < 0) {
            ERROR1("reading data from '%s' for sample data", fname);
            return 0;
        }
        total_read += nread;
        if (total_read < TU_SAMPLE_DATA_LEN) {
            off_t to;
            to = lseek(fd, (off_t) 0, SEEK_SET);
            if (to == (off_t) -1) {
                ERROR1("seeking data file '%s'", fname);
                return 0;
            }
        }
    }
    tu_sample_data_inited = 1;
    return 1;
}

char * tu_get_rand_data(unsigned len) {
    char * sample_pos;
    if (!tu_sample_data_inited) {
        tu_sample_data_rand_init();
    }
    assert(len < TU_SAMPLE_DATA_LEN);
    if (tu_sample_data_pos + len > TU_SAMPLE_DATA_LEN)
        tu_sample_data_pos = 0;
    sample_pos = tu_sample_data + tu_sample_data_pos;
    tu_sample_data_pos += len;
    return sample_pos;
}

char * tu_get_rand_data_rand_len(unsigned max_len, unsigned * len) {
    if (max_len == 0)
        *len = 0;
    else
        *len = tu_rand_limit(max_len);
    return tu_get_rand_data(*len);
}

struct tu_data_elements * tu_generate_elements_or_die(unsigned num_elements,
  unsigned max_len) {
    struct tu_data_elements * els = NULL;
    unsigned i;
    
    if ( (els = malloc(sizeof(*els))) == NULL) {
        perror("ERROR: allocating elems struct");
        exit(EXIT_FAILURE);
    }
    els->num_elements = num_elements;
    if ( (els->elements = malloc(sizeof(*els->elements) * num_elements))
      == NULL) {
        free(els);
        perror("ERROR: allocating elems array");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < num_elements; i++) {
        els->elements[i].data = tu_get_rand_data_rand_len(max_len, 
          &els->elements[i].len);
    }
    return els;
}

struct tu_data_elements * tu_generate_null_elements_or_die(unsigned 
  num_elements) {
    struct tu_data_elements * els = NULL;
    unsigned i;
    
    if ( (els = malloc(sizeof(*els))) == NULL) {
        perror("ERROR: allocating elems struct");
        exit(EXIT_FAILURE);
    }
    els->num_elements = num_elements;
    if ( (els->elements = malloc(sizeof(*els->elements) * num_elements))
      == NULL) {
        free(els);
        perror("ERROR: allocating elems array");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < num_elements; i++) {
        els->elements[i].data = NULL;
        els->elements[i].len = 0;
    }
    return els;
}

void tu_delete_elements(struct tu_data_elements * elements) {
    free(elements->elements);
    free(elements);
}

void tu_add_elements_to_list(struct tu_data_elements_list * list,
  struct tu_data_elements * elements) {
    if (list->head == NULL) {
        assert(list->tail == NULL);
        list->head = list->tail = elements;
    } else {
        assert(list->tail != NULL);
        list->tail->next = elements;
        list->tail = elements;
    }
}
