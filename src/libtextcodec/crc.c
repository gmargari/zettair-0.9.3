/* crc.c implements the common cyclic redundancy checksum algorithm.  The
 * implementation below was adapted from the code presented in RFC 1952 
 * (gzip file format specification, in the appendix).
 *
 * written nml 2004-08-26
 *
 */

#include "crc.h"

#include <assert.h>
#include <stdlib.h>

struct crc {
    unsigned int *table;       /* lookup table */
    uint32_t sum;              /* checksum */
};

static unsigned int *crc_table() {
    static unsigned int table[256];
    static int initialised = 0;

    if (!initialised) {
        unsigned int i,
                     j,
                     c;

        for (i = 0; i < 256; i++) {
            c = i;
            for (j = 0; j < 8; j++) {
                if (c & 1) {
                    c = 0xedb88320U ^ (c >> 1);
                } else {
                    c = c >> 1;
                }
            }
            table[i] = c;
        }
        initialised = 1;
    } 

    return table;
}

struct crc *crc_new() {
    struct crc *crc;

    assert(sizeof(unsigned int) >= 4);

    if ((crc = malloc(sizeof(*crc)))) {
        crc->table = crc_table();
        crc->sum = 0;
    }

    return crc;
}

void crc_reinit(struct crc *crc) {
    crc->sum = 0;
}

uint32_t crc_sum(struct crc *crc) {
    return crc->sum;
}

void crc_delete(struct crc *crc) {
    free(crc);
}

void crc(struct crc *crc, const void *next_in, unsigned int avail_in) {
    const unsigned char *buf = next_in;
    unsigned int i;
    uint32_t sum = crc->sum ^ 0xffffffffU;

    for (i = 0; i < avail_in; i++) {
        sum = crc->table[(sum ^ buf[i]) & 0xffU] ^ (sum >> 8);
    }

    crc->sum = sum ^ 0xffffffffU;
}

