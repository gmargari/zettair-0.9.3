/* storagep.c implements functions declared by storagep.h
 *
 * written nml 2003-02-17
 *
 */

#include "firstinclude.h"

#include "storagep.h"

#include "_mem.h"

#include "bit.h"
#include "def.h"
#include "getmaxfsize.h"
#include "mem.h"
#include "zvalgrind.h"

#include <limits.h>
#include <math.h>

unsigned int storagep_size() {
    return 2 * sizeof(uint16_t) /* uint_fast16_t members */
      + 3 * sizeof(uint32_t)    /* uint_fast32_t members */
      + 3 * sizeof(uint8_t);    /* uint8_t members */
}

#define WRITE_MEMBER(ptr, member, sizetype)                                   \
    VALGRIND_CHECK_READABLE(&member, sizeof(sizetype));                       \
    tmp_##sizetype = member;                                                  \
    mem_hton(ptr, &tmp_##sizetype, sizeof(sizetype));                         \
    ptr += sizeof(sizetype)

int storagep_write(const struct storagep *sp, void *vaddr) {
    char *addr = vaddr;
    uint32_t tmp_uint32_t;
    uint16_t tmp_uint16_t;
    uint8_t tmp_uint8_t;

    WRITE_MEMBER(addr, sp->pagesize, uint32_t);
    WRITE_MEMBER(addr, sp->max_termlen, uint16_t);
    WRITE_MEMBER(addr, sp->max_filesize, uint32_t);
    WRITE_MEMBER(addr, sp->vocab_lsize, uint16_t);
    WRITE_MEMBER(addr, sp->file_lsize, uint32_t);

    WRITE_MEMBER(addr, sp->btleaf_strategy, uint8_t);
    WRITE_MEMBER(addr, sp->btnode_strategy, uint8_t);
    WRITE_MEMBER(addr, sp->bigendian, uint8_t);

    assert((unsigned int) (addr - (const char *) vaddr) == storagep_size());

    return 1;
}

#define READ_MEMBER(ptr, member, sizetype)                                    \
    MEM_NTOH(&tmp_##sizetype, ptr, sizeof(sizetype));                         \
    member = tmp_##sizetype;                                                  \
    ptr += sizeof(sizetype)

int storagep_read(struct storagep *sp, const void *vaddr) {
    const char *addr = vaddr;
    uint32_t tmp_uint32_t;
    uint16_t tmp_uint16_t;
    uint8_t tmp_uint8_t;

    READ_MEMBER(addr, sp->pagesize, uint32_t);
    READ_MEMBER(addr, sp->max_termlen, uint16_t);
    READ_MEMBER(addr, sp->max_filesize, uint32_t);
    READ_MEMBER(addr, sp->vocab_lsize, uint16_t);
    READ_MEMBER(addr, sp->file_lsize, uint32_t);

    READ_MEMBER(addr, sp->btleaf_strategy, uint8_t);
    READ_MEMBER(addr, sp->btnode_strategy, uint8_t);
    READ_MEMBER(addr, sp->bigendian, uint8_t);

    assert((unsigned int) (addr - (const char *) vaddr) == storagep_size());

    return 1;
}

int storagep_defaults(struct storagep *storage, int fd) {
    storage->pagesize = 8192;
    storage->max_termlen = TERMLEN_DEFAULT;

    /* get default maximum filesize limit */
    storage->max_filesize = 4294967295U;        /* 4GB */
    if (!getmaxfsize(fd, storage->max_filesize, &storage->max_filesize)) {
        return -1;
    }

    storage->vocab_lsize = 0;
    storage->file_lsize = storage->max_filesize;
    storage->btleaf_strategy = 1;               /* 1 is sorted, general */
    storage->btnode_strategy = 1;               /* 1 is sorted, general */

    /* bigendian defaults to whatever this machine is */
#ifdef WORDS_BIGENDIAN
    storage->bigendian = 1;
#else
    storage->bigendian = 0;
#endif

    return 0;
}

