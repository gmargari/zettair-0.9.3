/* vec.c implements the byte vector described in vec.h 
 *
 * based on code by Hugh Williams
 *
 * written nml 2003-02-28
 *
 */

#include "firstinclude.h"

#include "vec.h"  

#include "bit.h"  

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>

/* internal array of the maximum number that will fit in each vbyte byte */
static const unsigned int vbyte_range[] = {
    /* this detects the size of integers on this architecture (a bit ugly, 
     * yes).  Having this array lets us encode/decode vbytes faster */
#if (UINT_MAX <= 4294967295)
    127U,
    16383U,
    2097151U,
    268435455U,
    4294967295U
#elif (UINT_MAX == 18446744073709551615)
    127U,
    16383U,
    2097151U,
    268435455U,
    34359738367U,
    4398046511103U,
    562949953421311U,
    72057594037927935U,
    9223372036854775807U,
    18446744073709551615U
#else
#error "unknown integer size"
#endif
};

unsigned int vec_vbyte_write(struct vec* v, unsigned long int n) {
    if (n <= vbyte_range[0]) {
        if (v->pos < v->end) {
            *v->pos++ = (char) n;
            return 1;
        } else {
            return 0;
        }
    } else if (n <= vbyte_range[1]) {
        if (v->pos + 1 < v->end) {
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = (char) n;
            return 2;
        } else {
            return 0;
        }
    } else if (n <= vbyte_range[2]) {
        if (v->pos + 2 < v->end) {
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = (char) n;
            return 3;
        } else {
            return 0;
        }
    } else if (n <= vbyte_range[3]) {
        if (v->pos + 3 < v->end) {
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = (char) n;
            return 4;
        } else {
            return 0;
        }
    } else if (n <= vbyte_range[4]) {
        if (v->pos + 4 < v->end) {
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = (char) n;
            return 5;
        } else {
            return 0;
        }
#if (UINT_MAX > 4294967295)
    } else if (n <= vbyte_range[5]) {
        if (v->pos + 5 < v->end) {
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = (char) n;
            return 6;
        } else {
            return 0;
        }
    } else if (n <= vbyte_range[6]) {
        if (v->pos + 6 < v->end) {
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = (char) n;
            return 7;
        } else {
            return 0;
        }
    } else if (n <= vbyte_range[7]) {
        if (v->pos + 7 < v->end) {
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = (char) n;
            return 8;
        } else {
            return 0;
        }
    } else if (n <= vbyte_range[8]) {
        if (v->pos + 8 < v->end) {
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = (char) n;
            return 9;
        } else {
            return 0;
        }
    } else if (n <= vbyte_range[9]) {
        if (v->pos + 9 < v->end) {
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = 0x80 | (char) (n & 0x7f); n >>= 7;
            *v->pos++ = (char) n;
            return 10;
        } else {
            return 0;
        }
#endif
    } else {
        assert("can't get here" && 0);
        return 0;
    }
}

unsigned int vec_vbyte_read(struct vec* v, unsigned long int* n) {
    unsigned long int localn = 0;
    unsigned int bytes = 0,
                 count = 0, 
                 get;

    while (v->pos < v->end) {
        bytes++;
        if (((get = ((unsigned char) *v->pos++)) & 0x80) == 0x80) {
            /* For each time we get a group of 7 bits, need to left-shift the 
               latest group by an additional 7 places, since the groups were 
               effectively stored in reverse order.*/
            localn |= ((get & 0x7f) << count);
            count += 7;
        } else if ((count + 7 < sizeof(*n) * 8)
          || ((count < sizeof(*n) * 8) 
            && (get < (1U << ((sizeof(*n) * 8 - count) + 1))))) {

            /* Now get the final 7 bits, which need to be left-shifted by a 
               factor of 7. */
            *n = localn | (get << count);
            return bytes;
        } else {
            /* we've overflowed, return error */
            v->pos -= bytes;
            return 0;
        }
    }

    /* we've run out of space, return error */
    v->pos -= bytes;
    return 0;
}

unsigned int vec_vbyte_scan(struct vec* v, unsigned int num, 
  unsigned int *byteptr) {
    unsigned int nread = 0,        /* number of vbytes scanned */
                 bytes = 0,        /* number of bytes in current vbyte */
                 sumbytes = 0;     /* number of total bytes scanned */

    while (num--) {
        assert(bytes == 0);

        while (v->pos < v->end) {
            bytes++;
            if (!(*v->pos++ & 0x80)) {
                nread++;
                sumbytes += bytes;
                bytes = 0;
                break;
            }
        }

        /* note that this case doesn't just catch situations where there wasn't
         * enough bytes to scan a number, but it doesn't matter because 
         * it works out the same (bytes will be 0) */
        if (v->pos >= v->end) {
            v->pos -= bytes;
            *byteptr = sumbytes;
            return nread;
        }
    }

    *byteptr = sumbytes;
    return nread;
}

unsigned int vec_vbyte_len(unsigned long int n) {
    if (n <= vbyte_range[0]) {
        return 1;
    } else if (n <= vbyte_range[1]) {
        return 2;
    } else if (n <= vbyte_range[2]) {
        return 3;
    } else if (n <= vbyte_range[3]) {
        return 4;
    } else if (n <= vbyte_range[4]) {
        return 5;
#if (UINT_MAX > 4294967295)
    } else if (n <= vbyte_range[5]) {
        return 6;
    } else if (n <= vbyte_range[6]) {
        return 7;
    } else if (n <= vbyte_range[7]) {
        return 8;
    } else if (n <= vbyte_range[8]) {
        return 9;
    } else if (n <= vbyte_range[9]) {
        return 10;
#endif
    } else {
        assert("can't get here" && 0);
        return 0;
    }
}

unsigned int vec_byte_read(struct vec *v, char *dst, unsigned int n) {
    if (((unsigned int) VEC_LEN(v)) < n) {
        n = VEC_LEN(v);
    }

    memcpy(dst, v->pos, n);
    v->pos += n;
    return n;
}

unsigned int vec_byte_write(struct vec *v, char *src, unsigned int n) {
    if (((unsigned int) VEC_LEN(v)) < n) {
        n = VEC_LEN(v);
    }

    memcpy(v->pos, src, n);
    v->pos += n;
    return n;
}

unsigned int vec_byte_scan(struct vec *v, unsigned int n) {
    if (((unsigned int) VEC_LEN(v)) < n) {
        n = VEC_LEN(v);
    }

    v->pos += n;
    return n;
}

unsigned int vec_flt_write(struct vec *v, float flt, unsigned int precision) {
    int exp;
    unsigned int biased_exp,
                 quant_frac;
    float frac = (float) frexpf(flt, &exp);
    char *pos = v->pos;
  
    assert(precision);

    /* convert signed exp into an unsigned integer (requires 8 bits) */
    if (exp >= 0) {
        biased_exp = exp << 1;
    } else {
        biased_exp = (-exp << 1) | 1;
    }
    assert(biased_exp < 256);

    /* convert signed fraction into an unsigned integer (requires 
     * precision + 1 bits) */
    if (flt >= 0) {
        quant_frac = ((unsigned int) (frac * bit_lmask(precision))) << 1;
    } else {
        quant_frac 
          = ((unsigned int) ((-frac * bit_lmask(precision))) << 1) | 1;
    }
    assert(quant_frac <= ((bit_lmask(precision) << 1) | 1));

    if (v->pos < v->end) {
        *v->pos++ = biased_exp;
    }

    while (v->pos < v->end && precision + 1 > 8) {
        *v->pos++ = quant_frac & 0xff;
        quant_frac >>= 8;
        precision -= 8;
    }

    if (v->pos < v->end) {
        /* note that we *have* to write out another byte because we have the
         * sign bit remaining */
        assert(quant_frac <= 0xff);
        *v->pos++ = quant_frac;
        return v->pos - pos;
    } else {
        v->pos = pos;
        return 0;
    }
}

unsigned int vec_flt_read(struct vec *v, float *flt, unsigned int precision) {
    int exp;
    unsigned char biased_exp;
    unsigned int quant_frac = 0,
                 bits = 0;
    float frac;
    char *pos = v->pos;

    assert(precision);

    if (v->pos < v->end) {
        biased_exp = (unsigned char) *v->pos++;
        while (v->pos < v->end && bits + 8 < precision + 1) {
            quant_frac |= ((unsigned char) *v->pos++) << bits;
            bits += 8;
        }

        if (v->pos < v->end) {
            quant_frac |= ((unsigned char) *v->pos++) << bits;

            if (!(biased_exp & 1)) {
                exp = biased_exp >> 1;
            } else {
                exp = -(biased_exp >> 1);
            }

            if (!(quant_frac & 1)) {
                frac = ((float) (quant_frac >> 1)) / bit_lmask(precision);
            } else {
                frac = -((float) (quant_frac >> 1)) / bit_lmask(precision);
            }

            *flt = (float) ldexpf(frac, exp);
            return v->pos - pos;
        }
    }

    v->pos = pos;
    return 0;
}

unsigned int vec_len(struct vec *v) {
    return VEC_LEN(v);
}

/* macro to generate code for array reading/writing functions for different
 * unsigned integer types */
#define VEC_INTARR(name, type)                                                \
    unsigned int vec_##name##_arr_read(struct vec *v, type *arr,              \
      unsigned int arrlen, unsigned int *bytes_ptr) {                         \
        unsigned char get = 0;                                                \
        unsigned int count = 0,                                               \
                     bytes = 0,                                               \
                     nbytes,                                                  \
                     bitcount;                                                \
        char *pos;                                                            \
        type n,                                                               \
             tmp,                                                             \
             *end = arr + arrlen;                                             \
                                                                              \
        while (v->pos < v->end && arr < end) {                                \
            n = 0;                                                            \
            bitcount = 0;                                                     \
            nbytes = 0;                                                       \
            pos = v->pos;                                                     \
                                                                              \
            while (v->pos < v->end && ((get = *v->pos) & 0x80)) {             \
                tmp = get & 0x7f;                                             \
                tmp <<= bitcount;                                             \
                n |= tmp;                                                     \
                bitcount += 7;                                                \
                bytes++;                                                      \
                v->pos++;                                                     \
            }                                                                 \
                                                                              \
            if (v->pos < v->end) {                                            \
                tmp = get;                                                    \
                tmp <<= bitcount;                                             \
                n |= tmp;                                                     \
                bytes++;                                                      \
                count++;                                                      \
                *arr++ = n;                                                   \
                v->pos++;                                                     \
            } else {                                                          \
                v->pos = pos;                                                 \
                *bytes_ptr = bytes;                                           \
                return count;                                                 \
            }                                                                 \
        }                                                                     \
                                                                              \
        *bytes_ptr = bytes;                                                   \
        return count;                                                         \
    }                                                                         \
                                                                              \
    unsigned int vec_##name##_arr_write(struct vec *v, type *arr,             \
      unsigned int arrlen, unsigned int *bytes_ptr) {                         \
        unsigned int bytes = 0,                                               \
                     count = 0,                                               \
                     nbytes;                                                  \
        char *pos;                                                            \
        type n,                                                               \
             *end = arr + arrlen;                                             \
                                                                              \
        while (v->pos < v->end && arr < end) {                                \
            n = *arr;                                                         \
            nbytes = 0;                                                       \
            pos = v->pos;                                                     \
                                                                              \
            while (v->pos < v->end && n > 127) {                              \
                nbytes++;                                                     \
                *v->pos++ = 0x80 | (char) (n & 0x7f);                                \
                n >>= 7;                                                      \
            }                                                                 \
                                                                              \
            if (v->pos < v->end) {                                            \
                bytes += nbytes + 1;                                          \
                count++;                                                      \
                *v->pos++ = (char) n;                                                \
                arr++;                                                        \
            } else {                                                          \
                v->pos = pos;                                                 \
                *bytes_ptr = bytes;                                           \
                return count;                                                 \
            }                                                                 \
        }                                                                     \
                                                                              \
        *bytes_ptr = bytes;                                                   \
        return count;                                                         \
    }

VEC_INTARR(vbyte, unsigned long int)
VEC_INTARR(int, unsigned int)
VEC_INTARR(maxint, uintmax_t)

unsigned int vec_flt_arr_read(struct vec *v, float *arr, unsigned int arrlen,
  unsigned int precision, unsigned int *bytes_ptr) {
    unsigned int bytes = 0,
                 len,
                 count = 0;
    float *end = arr + arrlen;

    while (arr < end && (len = vec_flt_read(v, arr, precision))) {
        arr++;
        bytes += len;
        count++;
    }

    *bytes_ptr = bytes;
    return count;
}

unsigned int vec_flt_arr_write(struct vec *v, float *arr, unsigned int arrlen,
  unsigned int precision, unsigned int *bytes_ptr) {
    unsigned int bytes = 0,
                 len,
                 count = 0;
    float *end = arr + arrlen;

    while (arr < end && (len = vec_flt_write(v, *arr, precision))) {
        arr++;
        bytes += len;
        count++;
    }

    *bytes_ptr = bytes;
    return count;
}

