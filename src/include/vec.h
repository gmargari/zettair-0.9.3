/* vec.h declares an interface to create and access bytevectors, which
 * can be dynamically read from, written to and expanded.
 *
 * If you don't know what a variable-byte integer encoding scheme is,
 * you should read Williams and Zobel, Compressing Integers for Fast
 * File Access (http://www.seg.rmit.edu.au/research/download.php?manuscript=5)
 *
 * based very closely on the old vec.[ch] written by Hugh Williams
 *
 * updated nml 2002-02-28 
 *
 */

#ifndef VEC_H
#define VEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>
#include "zstdint.h"

struct vec {
    char *pos;            /* next read/write memory location */
    char *end;            /* one past the end of valid read/write range */
};

/* macro and function to get the length remaining in a vector 
 * (undefined if v->pos > v->end) */
#define VEC_LEN(v) \
  (assert((v)->pos <= (v)->end), (unsigned int) ((v)->end - (v)->pos))
unsigned int vec_len(struct vec *v);

/* macro to return maximum number of bytes in a vbyte on this platform */
#if (UINT_MAX == 4294967295U)
#define VEC_VBYTE_MAX 5
#else
/* assume 64-bit */
#define VEC_VBYTE_MAX 10
#endif

/* read a vbyte encoded number from the vector v, placing the result in n.  The
 * number of bytes read is returned on success, 0 on failure (failure can only
 * occur because of overflow or because the vector ended too soon.  
 * If VEC_LEN(v) >= vec_vbyte_len(LUINT_MAX) on failure, then overflow 
 * occurred, otherwise insufficient space was provided).  */
unsigned int vec_vbyte_read(struct vec *v, unsigned long int *n);

/* write a vbyte encoded number n to vector v.  Returns number of bytes written
 * or 0 on failure.  Failure can only occur if insufficient space remains in the
 * vector. */
unsigned int vec_vbyte_write(struct vec *v, unsigned long int n);

/* scan over num vbyte numbers in vector v.  Returns how many numbers were
 * successfully scanned over.  This number can only be shorted than the number
 * requested due to insufficient space in the vector or overflow while reading
 * one of the numbers. */
unsigned int vec_vbyte_scan(struct vec *v, unsigned int num, 
  unsigned int *bytes);

/* read and write arrays of integers.  both functions return the number of
 * integers read/written, with the number of bytes used written into bytes */
unsigned int vec_vbyte_arr_write(struct vec *v, unsigned long int *arr, 
  unsigned int arrlen, unsigned int *bytes);
unsigned int vec_vbyte_arr_read(struct vec *v, unsigned long int *arr,
  unsigned int arrlen, unsigned int *bytes);

/* returns the length of a number as a vbyte (in bytes) */
unsigned int vec_vbyte_len(unsigned long int n);

/* read n bytes from vector v, writing them into dst.  Returns number of bytes
 * read with short reads being caused by not enough data in the vector */
unsigned int vec_byte_read(struct vec *v, char *dst, unsigned int n);

/* write n bytes to vector v, reading them from src.  Returns number of bytes
 * written with short reads being caused by not enough space in the vector */
unsigned int vec_byte_write(struct vec *v, char *src, unsigned int n);

/* scan over n bytes in vector v.  Returns number of bytes scanned over, with
 * short scans being caused by not enough data in the vector */
unsigned int vec_byte_scan(struct vec *v, unsigned int n);

/* alternative versions of the above function for alternative data-types */

unsigned int vec_int_arr_read(struct vec *v, unsigned int *arr, 
  unsigned int arrlen, unsigned int *bytes);
unsigned int vec_int_arr_write(struct vec *v, unsigned int *arr, 
  unsigned int arrlen, unsigned int *bytes);
unsigned int vec_maxint_arr_read(struct vec *v, uintmax_t *arr, 
  unsigned int arrlen, unsigned int *bytes);
unsigned int vec_maxint_arr_write(struct vec *v, uintmax_t *arr, 
  unsigned int arrlen, unsigned int *bytes);

/* IEEE standard specifies that single-precision floating point numbers have 23
 * bits of mantissa, making that the maximum precision for storing floats */
#define VEC_FLT_FULL_PRECISION 23

/* the float read/write functions require you to specify the precision with
 * which floating point numbers are stored.  You will be required to supply the
 * same precision to read as you did to write.  The current implementation
 * rounds the precision (given in bits) to an integral number of bytes,
 * including a sign bit.  Thus 7, 15, 23 are sensible values for precision. */
unsigned int vec_flt_read(struct vec *v, float *flt, unsigned int precision);
unsigned int vec_flt_write(struct vec *v, float flt, unsigned int precision);
unsigned int vec_flt_arr_read(struct vec *v, float *arr, unsigned int arrlen,
  unsigned int precision, unsigned int *bytes);
unsigned int vec_flt_arr_write(struct vec *v, float *arr, unsigned int arrlen,
  unsigned int precision, unsigned int *bytes);

/* note that we don't have double operations because they're typically overkill
 * compared to floats, and that it's difficult to manipulate 64-bit 
 * quantities to read/write them */

#ifdef __cplusplus
}
#endif

#endif

