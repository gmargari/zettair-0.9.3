/* _mem.h declares macros to quickly implement memory operations
 *
 * written nml 2003-12-29
 *
 */

#include "config.h"

#ifndef PRIVATE_MEM_H
#define PRIVATE_MEM_H

#ifdef __cplusplus
extern "C" {
#endif

#define MEM_PTRDIFF(one, two) (((char*) (one)) - ((char*) (two)))

#define MEM_PTRADD(ptr, size) ((void*)(((char*) (ptr)) + (size)))

#define MEM_PTRADDR(ptr) (((char*) (ptr)) - ((char*) 0))

/* mem_hton (since big-endian and little-endian schemes are symmetric, we can
 * use the same macro to perform both conversions) */
#define MEM_HTON MEM_NTOH

/* mem_ntoh */
#ifdef WORDS_BIGENDIAN

/* big endian version */
#define MEM_NTOH(dst, src, size)                                              \
    if (1) {                                                                  \
        uint8_t *mem_ntoh_dst = (uint8_t *) dst;                              \
                                                                              \
        switch (size) {                                                       \
        case 8:                                                               \
            *(mem_ntoh_dst + 7) = *(((uint8_t *) src) + 7);                   \
            *(mem_ntoh_dst + 6) = *(((uint8_t *) src) + 6);                   \
            *(mem_ntoh_dst + 5) = *(((uint8_t *) src) + 5);                   \
            *(mem_ntoh_dst + 4) = *(((uint8_t *) src) + 4);                   \
        case 4:                                                               \
            *(mem_ntoh_dst + 3) = *(((uint8_t *) src) + 3);                   \
            *(mem_ntoh_dst + 2) = *(((uint8_t *) src) + 2);                   \
        case 2:                                                               \
            *(mem_ntoh_dst + 1) = *(((uint8_t *) src) + 1);                   \
        case 1:                                                               \
            *mem_ntoh_dst = *(((uint8_t *) src));                             \
        case 0:                                                               \
            break;                                                            \
                                                                              \
        default:                                                              \
            if (1) {                                                          \
                unsigned int mem_ntoh_i;                                      \
                                                                              \
                for (mem_ntoh_i = 0; mem_ntoh_i < size; mem_ntoh_i++) {       \
                    mem_ntoh_dst[mem_ntoh_i]                                  \
                      = ((uint8_t *) src)[mem_ntoh_i];                        \
                }                                                             \
            }                                                                 \
            break;                                                            \
        }                                                                     \
    } 

#else

/* little endian version */
#define MEM_NTOH(dst, src, size)                                              \
    if (1) {                                                                  \
        uint8_t *mem_ntoh_dst = (uint8_t *) dst;                              \
                                                                              \
        switch (size) {                                                       \
        case 8:                                                               \
            *(mem_ntoh_dst + 7) = *(((uint8_t *) src) + 0);                   \
            *(mem_ntoh_dst + 6) = *(((uint8_t *) src) + 1);                   \
            *(mem_ntoh_dst + 5) = *(((uint8_t *) src) + 2);                   \
            *(mem_ntoh_dst + 4) = *(((uint8_t *) src) + 3);                   \
            *(mem_ntoh_dst + 3) = *(((uint8_t *) src) + 4);                   \
            *(mem_ntoh_dst + 2) = *(((uint8_t *) src) + 5);                   \
            *(mem_ntoh_dst + 1) = *(((uint8_t *) src) + 6);                   \
            *(mem_ntoh_dst + 0) = *(((uint8_t *) src) + 7);                   \
            break;                                                            \
                                                                              \
        case 4:                                                               \
            *(mem_ntoh_dst + 3) = *(((uint8_t *) src) + 0);                   \
            *(mem_ntoh_dst + 2) = *(((uint8_t *) src) + 1);                   \
            *(mem_ntoh_dst + 1) = *(((uint8_t *) src) + 2);                   \
            *(mem_ntoh_dst + 0) = *(((uint8_t *) src) + 3);                   \
            break;                                                            \
                                                                              \
        case 2:                                                               \
            *(mem_ntoh_dst + 1) = *(((uint8_t *) src) + 0);                   \
            *(mem_ntoh_dst + 0) = *(((uint8_t *) src) + 1);                   \
            break;                                                            \
                                                                              \
        case 1:                                                               \
            *mem_ntoh_dst = *(((uint8_t *) src));                             \
            break;                                                            \
                                                                              \
        case 0:                                                               \
            break;                                                            \
                                                                              \
        default:                                                              \
            if (1) {                                                          \
                unsigned int mem_ntoh_i;                                      \
                                                                              \
                for (mem_ntoh_i = 0; mem_ntoh_i < size; mem_ntoh_i++) {       \
                    mem_ntoh_dst[mem_ntoh_i]                                  \
                      = ((uint8_t *) src)[size - mem_ntoh_i - 1];             \
                }                                                             \
            }                                                                 \
            break;                                                            \
        }                                                                     \
    } 

#endif

/* alignment stuff */

/* call this macro once for each type that you wish to align.  Then you can 
 * call mem_align_(type) with no arguments to get the alignment for each type */
#define MEM_ALIGN_TYPE(type)                                                  \
   unsigned int mem_align_##type () {                                         \
      static struct {                                                         \
         char pad;                                                            \
         type align;                                                          \
      } s;                                                                    \
      return ((unsigned int) &s.align) - ((unsigned int) &s.pad);             \
   }

/* this macro returns what should be the maximum alignment on this platform */
#define MEM_ALIGN_MAX                                                         \
   ((mem_align_long() > mem_align_double()) ? mem_align_long() :              \
     mem_align_double())

/* this macro returns the next place in the given buffer that conforms to the
 * given alignment.  It is assumed that the alignment is a power of two. */
#define MEM_ALIGN(buf, align)                                                 \
   ((void*) (((char*) buf) + (align - (((MEM_PTRADDR((char*) buf) - 1)        \
     & (align - 1)) + 1))))

/* this macro returns the next place in the given buffer that conforms to the
 * given alignment.  It is *not* assumed that the alignment is a power of 
 * two */
#define MEM_ALIGN_SAFE(buf, align)                                            \
   ((void*) (((char*) buf) + (align - (((MEM_PTRADDR((char*) buf) - 1)        \
     % align) + 1))))

#ifdef __cplusplus
}
#endif

#endif

