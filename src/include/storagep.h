/* storagep.h declares a structure that contains all necessary information 
 * regarding how an index is to be physically stored on disk (the
 * storage parameters).  Its a convenience class intended to reduce the number 
 * of parameters passed to some routines.
 *
 * written nml 2003-02-17
 *
 */

#ifndef STORAGEP_H
#define STORAGEP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "zstdint.h"

struct storagep {
    unsigned int pagesize;           /* size of one 'disk block' */
    unsigned int max_termlen;        /* maximum length of one term */
    unsigned int max_filesize;       /* maximum file size */
    unsigned int vocab_lsize;        /* size of inverted lists to try to 
                                      * store in the vocabulary */
    unsigned int file_lsize;         /* maximum size of inverted lists to try 
                                      * to store in files */
    uint8_t btleaf_strategy;         /* btree leaf bucket format */
    uint8_t btnode_strategy;         /* btree internal node bucket format */
    uint8_t bigendian;               /* whether to store stuff in big-endian 
                                      * byte order (if false we store
                                      * little-endian.  I am aware that other
                                      * obscure byte orders exist, but we're
                                      * ignoring them) */
};

/* return the space needed to store a set of storage parameters */
unsigned int storagep_size();

/* read a set of storage parameters from memory (where memory is at least
 * storagep_size bytes long) */
int storagep_read(struct storagep *sp, const void *addr);

/* write a set of storage parameters to memory (where memory is at least
 * storagep_size bytes long) */
int storagep_write(const struct storagep *sp, void *addr);

/* overallocation function that rounds up to the nearest power of two */
unsigned int storagep_power2_overalloc(unsigned int size);

/* overallocation function that overallocates linearly according to
 * parameters given to storagep_linear_overalloc_init (note that only
 * *one* logical instance of this function can be active at once) */
unsigned int storagep_linear_overalloc(unsigned int size);

/* performs exact allocation */
unsigned int storagep_no_overalloc(unsigned int size);

/* initialise storagep_linear_overalloc with parameters for linear
 * overallocation.  If the size given is over thresh, it is multiplied
 * by gradient (must be > 1.0) and has offset added to it.  It is then rounded
 * up to the nearest multiple of granularity. */
void storagep_linear_overalloc_init(unsigned int thresh, float gradient, 
  unsigned int offset, unsigned int granularity);

/* fill a struct with storage default values (fd is a file to provide default
 * maximum file size limits. XXX: a bit of a hack, but the whole problem 
 * promotes that kind of thing).  Returns 0 on success, < 0 on failure. */
int storagep_defaults(struct storagep *storage, int fd);

#ifdef __cplusplus
}
#endif

#endif

