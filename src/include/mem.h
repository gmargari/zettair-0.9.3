/* mem.h declares functions for manipulating memory
 *
 * written nml 2003-09-17
 *
 */

#ifndef MEM_H
#define MEM_H

#ifdef __cplusplus
extern "C" {
#endif

/* returns the difference between two pointers, assuming that they can be 
 * meaningfully compared */
unsigned long int mem_ptrdiff(const void *b1, const void *b2);

/* returns a (void) pointer location pointer + size */
void *mem_ptradd(const void *b, unsigned int offset);

/* returns the numerical address of a pointer (dodgily) */
unsigned long int mem_ptraddr(const void *b);

/* translate a network-ordered integer at src of size size into a host-ordered 
 * integer at dst */
void mem_ntoh(void *dst, const void *src, unsigned int size);

/* translate a host-ordered integer at src of size size into a network-ordered 
 * integer at dst */
void mem_hton(void *dst, const void *src, unsigned int size);

/* standard stuff that could (possibly) be improved but is time-consuming to
 * reimplement */
#if 0
void *mem_cpy(void *dst, const void *src, unsigned int len);

void *mem_move(void *dst, const void *src, unsigned int len);

void *mem_chr(const void *b, int c, unsigned int len);

int mem_cmp(const void *b1, const void *b2, unsigned int len);

void *mem_set(void *b, int c, unsigned int len);
#endif

/* alignment stuff */

/* get alignments of basic types */
unsigned int mem_align_char();
unsigned int mem_align_short();
unsigned int mem_align_int();
unsigned int mem_align_long();
unsigned int mem_align_float();
unsigned int mem_align_double();
unsigned int mem_align_ptr();

/* return the maximum alignment (probably) for this platform */
unsigned int mem_align_max();

/* return the next point in the buffer that conforms to the given alignment.
 * align is assumed to be a power of two. */
void *mem_align(void *buf, unsigned int align);

#ifdef __cplusplus
}
#endif

#endif

