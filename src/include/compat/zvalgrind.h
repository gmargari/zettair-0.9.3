/* zvalgrind.h is a small wrapper around the valgrind.h functions to make them
 * do nothing when valgrind isn't present.
 *
 * written nml 2004-08-30
 *
 */

#ifndef ZVALGRIND_H
#define ZVALGRIND_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* if they requested use of valgrind */
#ifdef WITH_VALGRIND

#ifdef HAVE_VALGRIND_H
#include <valgrind.h>
#endif
#ifdef HAVE_MEMCHECK_H
#include <memcheck.h>
#else
/* its probably valgrind prior to version 2, should still be able to use it.
 * Check for FREELIKE_BLOCK and MALLOCLIKE_BLOCK, because earlier versions
 * didn't have these */
#ifndef VALGRIND_MALLOCLIKE_BLOCK
#define VALGRIND_MALLOCLIKE_BLOCK(addr, sizeB, rzB, is_zeroed)
#endif
#ifndef VALGRIND_FREELIKE_BLOCK
#define VALGRIND_FREELIKE_BLOCK(addr, rzB)
#endif
#endif

#else
/* haven't got valgrind, define empty macros */

#define VALGRIND_MAKE_NOACCESS(_qzz_addr,_qzz_len) 
#define VALGRIND_MAKE_WRITABLE(_qzz_addr,_qzz_len) 
#define VALGRIND_MAKE_READABLE(_qzz_addr,_qzz_len) 
#define VALGRIND_DISCARD(_qzz_blkindex) 
#define VALGRIND_CHECK_WRITABLE(_qzz_addr,_qzz_len) 
#define VALGRIND_CHECK_READABLE(_qzz_addr,_qzz_len) 
#define VALGRIND_CHECK_DEFINED(__lvalue) 
#define VALGRIND_MAKE_NOACCESS_STACK(_qzz_addr,_qzz_len) /* no value */
#define RUNNING_ON_VALGRIND 0
#define VALGRIND_DO_LEAK_CHECK /* no value */
#define VALGRIND_DISCARD_TRANSLATIONS(_qzz_addr,_qzz_len) /* no value */
#define VALGRIND_MALLOCLIKE_BLOCK(addr, sizeB, rzB, is_zeroed)
#define VALGRIND_FREELIKE_BLOCK(addr, rzB)
#define VALGRIND_CREATE_MEMPOOL(pool, rzB, is_zeroed)
#define VALGRIND_DESTROY_MEMPOOL(pool)
#define VALGRIND_MEMPOOL_ALLOC(pool, addr, size)
#define VALGRIND_MEMPOOL_FREE(pool, addr)

#endif

#ifdef __cplusplus
}
#endif

#endif

