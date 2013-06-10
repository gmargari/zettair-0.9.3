/* bit.h declares a lot of macros to do bit manipulation in a
 * portable way
 *
 * make sure the variables you're operating on are *unsigned*, else
 * the results are undefined 
 *
 * written nml 2003-04-29
 *
 */

#ifndef BIT_H
#define BIT_H

#include <assert.h>
#include <string.h>

extern unsigned long int bit_lbits[];

/* basic functions */

/* bit_set sets bit number bit in var to val (0 is lsb), evaluating to the
 * result (var is not changed in place) */
#define BIT_SET(var, bit, val) \
  (assert(((bit) >= 0) && ((bit) <= (8 * sizeof(int)))), \
    (((var) & ~(1 << (bit))) | (!!(val) << (bit))))

/* bit_toggle toggles bit number bit in var (0 is lsb), evaluating to
 * the result (var is not changed in place) */
#define BIT_TOGGLE(var, bit) \
  (assert(((bit) >= 0) && ((bit) <= (8 * sizeof(int)))), \
    ((var) ^ (1 << (bit))))

/* bit_get returns bit number bit in var (0 is lsb) */
#define BIT_GET(var, bit) \
  (assert(((bit) >= 0) && ((bit) <= (8 * sizeof(int)))), \
    ((var) & (1 << (bit))))

/* strength-reduced arithmetic */

/* bit_mul2 performs integer multiplication between num and a power of two 
 * (specified by pow) */
#define BIT_MUL2(num, pow) ((num) << (pow))

/* bit_div2 performs integer division between num and a power of two
 * (specified by pow) */
#define BIT_DIV2(num, pow) ((num) >> (pow))

/* bit_mod2 performs integer modulus between num and a power of two
 * (specified by pow) */
#define BIT_MOD2(num, pow) ((num) & BIT_LMASK((pow)))

/* bit_pow2 performs integer exponentiation of a power of
 * two (specified by pow) (pow must be between 0 and the wordsize of
 * your computer in bits - 1, else 0 is returned) */
#define BIT_POW2(pow) \
  (assert(((pow) >= 0) && ((pow) <= (8 * sizeof(int)))), \
    (unsigned int) (bit_lbits[(pow)] + 1))

/* bit_log2 returns floor(log_2(num)) except if num is 0, in which case 0 is
 * returned. */
unsigned int bit_log2(unsigned long int num);

/* masking */

/* return the a mask for bits upper bits */
#define BIT_UMASK(bits) \
  (assert(((bits) >= 0) && ((bits) <= (8 * sizeof(long)))), \
    (unsigned int)(~bit_lbits[(8 * (sizeof(long))) - (bits)]))

/* return the a mask for bits lower bits */
#define BIT_LMASK(bits) \
  (assert(((bits) >= 0) && ((bits) <= (8 * sizeof(int)))), \
    (unsigned int)(bit_lbits[(bits)]))

/* pointer setting (FIXME: this version is non-portable) */
#define BIT_ARRAY_NULL(arr, size) (memset((arr), 0, (size) * sizeof((arr)[0])))

/* bits to bytes and vice versa */

/* return floor(number of bytes in given bits) */
#define BIT_TO_BYTE(bits) (BIT_DIV2((bits), 3))

/* return number of bits in given bytes */
#define BIT_FROM_BYTE(bytes) (BIT_MUL2((bytes), 3))

/* place a reversed version of src in dst */
#define BIT_REV(dst, src)                                                     \
    do {                                                                      \
        assert(sizeof(dst) == sizeof(src));                                   \
        switch (sizeof(dst)) {                                                \
        case 1:                                                               \
            dst = bit_revbits[(src >> 0) & BIT_LMASK(8)];                     \
            break;                                                            \
                                                                              \
        case 2:                                                               \
            dst = (bit_revbits[(src >> 0) & BIT_LMASK(8)] << 8)               \
                  | bit_revbits[(src >> 8) && BIT_LMASK(8)];                  \
            break;                                                            \
                                                                              \
        case 4:                                                               \
            dst = (bit_revbits[(src >> 0) & BIT_LMASK(8)] << 24)              \
                  | (bit_revbits[(src >> 8) & BIT_LMASK(8)] << 16)            \
                  | (bit_revbits[(src >> 16) & BIT_LMASK(8)] << 8)            \
                  | (bit_revbits[(src >> 24) & BIT_LMASK(8)] << 0);           \
            break;                                                            \
                                                                              \
        case 8:                                                               \
            dst = (bit_revbits[(src >> 0) & BIT_LMASK(8)] << 56)              \
                  | (bit_revbits[(src >> 8) & BIT_LMASK(8)] << 48)            \
                  | (bit_revbits[(src >> 16) & BIT_LMASK(8)] << 40)           \
                  | (bit_revbits[(src >> 24) & BIT_LMASK(8)] << 32)           \
                  | (bit_revbits[(src >> 32) & BIT_LMASK(8)] << 24)           \
                  | (bit_revbits[(src >> 40) & BIT_LMASK(8)] << 16)           \
                  | (bit_revbits[(src >> 48) & BIT_LMASK(8)] << 8)            \
                  | (bit_revbits[(src >> 56) & BIT_LMASK(8)] << 0);           \
            break;                                                            \
                                                                              \
        default:                                                              \
            dst = 0;                                                          \
            for (i = 0; i < sizeof(dst); i++) {                               \
                dst |= bit_revbits[(src >> BIT_FROM_BYTE(i)) & BIT_LMASK(8)]  \
                  << BIT_FROM_BYTE(sizeof(dst) - (i + 1));                    \
            }                                                                 \
        }                                                                     \
    } while (0)

/* wrapper functions */

unsigned int bit_set(unsigned int var, unsigned int bit, unsigned int val);
unsigned int bit_toggle(unsigned int var, unsigned int bit);
unsigned int bit_get(unsigned int var, unsigned int bit);
unsigned int bit_mul2(unsigned int num, unsigned int pow);
unsigned int bit_div2(unsigned int num, unsigned int pow);
unsigned int bit_mod2(unsigned int num, unsigned int pow);
unsigned int bit_pow2(unsigned int pow);
unsigned int bit_umask(unsigned int bits);
unsigned int bit_lmask(unsigned int bits);
void bit_array_null(void *arr, unsigned int nmemb, unsigned int width);
unsigned int bit_to_byte(unsigned int bits);
unsigned int bit_from_byte(unsigned int bits);
unsigned int bit_rev(unsigned int num, unsigned int byte_width);

#endif

