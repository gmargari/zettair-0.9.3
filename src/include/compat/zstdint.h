#ifndef ZSTDINT_H
#define ZSTDINT_H

#include "config.h"

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif 

#include <limits.h>

#ifndef UINT32_MAX
#define UINT32_MAX UINT_MAX
#endif

#ifndef UINT16_MAX
#define UINT16_MAX USHRT_MAX
#endif

/* define an appropriate string for printing off_t quantities */
#if _FILE_OFFSET_BITS == 64
 #define ZPRI_OFF PRId64
#else
 #define ZPRI_OFF PRId32
#endif

#endif

