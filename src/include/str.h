/* str.h declares different (hopefully quicker) implementations of the
 * standard library string functions (and some additional extras such
 * as strlcat and strlcpy from the OpenBSD project)
 *
 * this is an attempt to stop everyone writing their own versions of
 * strlen and strcmp for every different project.
 *
 * note that although i'd like to use unsigned char's here, to specify char
 * range [0,255] instead of leaving it possibly [-128,127], but this tends to 
 * mean a lot of casting for three reasons:
 *
 *   - most (other) library calls use char *
 *   - string literals in c are char *
 *   - main argv (to be portable) must be char *
 *
 * unfortunately my solution is merely to deal with the value of extended 
 * characters in the positions locally (i.e. parsers and other stuff that 
 * cares).  There are macros at the bottom of this file to help.
 *
 * written nml 2003-03-26
 *
 */

#ifndef STR_H
#define STR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

/* version of strlen.  calculates the length of the string s *not*
 * including the terminating '\0' */
size_t str_len(const char *s);

/* version of strlen with maximum size */
size_t str_nlen(const char *s, size_t maxlen);

/* version of strcmp.  return an integer less than, equal to or greater
 * than 0 if s1 is less than, equal to or greater than s2 respectively. */
int str_cmp(const char *s1, const char *s2);

/* version of strncmp.  return an integer less than, equal to or greater
 * than 0 if the first size characters of s1 are less than, equal to
 * or greater than the first size characters of s2 respectively
 * (comparison doesn't continue past '\0' terminators however) */
int str_ncmp(const char *s1, const char *s2, size_t size);

/* equivalent to str_ncmp, except that both strings have a limiting length.  If
 * the strings are identical until one ends, the shorter string is considered 
 * smaller.  */
int str_nncmp(const char *s1, size_t size1, const char *s2, size_t size2);

/* version of strcasecmp.  return an integer less than, equal to or greater
 * than 0 if s1 is less than, equal to or greater than s2, ignoring case.
 * If a letter gets compared with []\^_` the letter will be evaluate to more
 * than these characters (equivalent to conversion to lowercase) */
int str_casecmp(const char *s1, const char *s2);

/* version of strncasecmp.  return an integer less than, equal to or greater
 * than 0 if the first size characters of s1 are less than, equal to or greater 
 * than the first size charactesr of s2, ignoring case.
 * If a letter gets compared with []\^_` the letter will be evaluate to more
 * than these characters (equivalent to conversion to lowercase) */
int str_ncasecmp(const char *s1, const char *s2, size_t size);

/* version of strcpy.  Copy src to dst, including '\0' termination.
 * Returns a pointer to dst. */
char *str_cpy(char *dst, const char *src);

/* version of strcat.  Copies src to the end of dst, including '\0'
 * termination.  Returns a pointer to dst */
char *str_cat(char *dst, const char *src);

/* version of strncpy.  Copy src to dst and '\0' fill the rest of the
 * space given by size.  Returns a pointer to dst. */
char *str_ncpy(char *dst, const char *src, 
  size_t size);

/* version of strncat.  Copy src to dst, including '\0' termination,
 * but copy size bytes at most.  Returns a pointer to dst */
char *str_ncat(char *dst, const char *src, 
  size_t size);

/* version of strlcpy (safer version of strncat as produced by OpenBSD).
 * copy src to dst, given size space (which *includes* space for the
 * terminating character).  '\0' termination is guaranteed so long as
 * size is non-zero.  Returns total length (excluding '\0'
 * termination) of the string that was created, assuming no truncation
 * occurred.  Refer to 
 * 'strlcpy and strlcat - consistent, safe, string copy and concatenation' 
 * by Todd C. Miller, Theo de Raadt for more detail. */
size_t str_lcpy(char *dst, const char *src, size_t size);

/* version of strlcat (safer version of strncat as produced by OpenBSD).
 * concatenate src onto the end of dst, given size space (which
 * *includes* space for the terminating character).  '\0' termination
 * is guaranteed so long as size > strlen(dst). 
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).  Refer to 
 * 'strlcpy and strlcat - consistent, safe, string copy and concatenation' 
 * by Todd C. Miller, Theo de Raadt for more detail. */
size_t str_lcat(char *dst, const char *src, size_t size);

/* return a malloc'd copy of s1, or NULL */
char *str_dup(const char *s1);

/* return a malloc'd copy of at most the first size bytes of str, NULL
 * terminated */
char *str_ndup(const char *str, size_t size);

/* return a pointer to the first non-whitespace element of a string (may be the
 * '\0' ending) */
const char *str_ltrim(const char *s);

/* remove trailing whitespace from a string by terminating it over the first
 * character of the last whitespace sequence */
unsigned int str_rtrim(char *s);

/* convert an ASCII string to lowercase (non-ASCII characters are preserved),
 * returning the number of substitutions made. */
unsigned int str_tolower(char *s);

/* convert an ASCII string to lowercase (non-ASCII characters are preserved),
 * returning the number of substitutions made.  At most size characters will be
 * converted. */
unsigned int str_ntolower(char *s, size_t size);

/* convert an ASCII string to uppercase (non-ASCII characters are preserved),
 * returning the number of substitutions made. */
unsigned int str_toupper(char *s);

/* convert an ASCII string to uppercase (non-ASCII characters are preserved),
 * returning the number of substitutions made.  At most size characters will be
 * converted. */
unsigned int str_ntoupper(char *s, size_t size);

/* version of strchr.  returns a pointer to the first instance of c in s, or
 * NULL if it isn't in the string */
char *str_chr(const char *s, int c);

/* version of strchr.  returns a pointer to the last instance of c in s, or
 * NULL if it isn't in the string */
char *str_rchr(const char *s, int c);

/* version of split method common to scripting languages. 
 * split str into parts delimited by characters of delim.  The returned array 
 * is malloc'd and NULL terminated, but
 * contains pointers into original string, which will have delimiters changed to
 * \0 as needed to delimit the split strings.  The size of the returned array 
 * will be written into parts.  If no delim is found, then an array of one is 
 * returned and the string is untouched. errno will be set and NULL returned 
 * if an error occurred. */
char **str_split(char *str, const char *delim, 
  unsigned int *parts);

/* remove all whitespace, control and punctuation characters from str (with the
 * exception of single, internal hyphens.  Returns the number of characters
 * removed from the string */
unsigned int str_strip(char *str);

/* indicates whether characters are signed or not by default (1 if they are, 0
 * if they are not) */
int str_signed_char();
#define STR_SIGNED_CHAR (((int) ((char) 255)) == -1)     /* macro version */

/* get an integer value from a character, forcing character values into [0,255]
 * range */
unsigned int str_from_char(char c);
#define STR_FROM_CHAR(x) (((x) + UCHAR_MAX + 1) & 0xff)  /* macro version */

/* get a character value from [0,255] range integer */
char str_to_char(unsigned int c);
#define STR_TO_CHAR(x) ((char) (x))                      /* macro version */

/* write the name of the directory in src, up to and including the last
 * seperator character, into dst (which is of length dstsize).  If src doesn't
 * contain any seperator characters, the string '.' is written into dst */
char *str_dirname(char *dst, unsigned int dstsize, const char *src);

/* return a pointer to the filename component of a path.  Pretty much like
 * basename in the libgen library, except that we don't alter the string, which
 * prevents us from normalising away seperators that libgen does (e.g.
 * basename("tmp/") = "tmp/" instead of "tmp"). */
const char *str_basename(const char *src);

/* produce a hash value (using Justin Zobel's bitwise hashing function) for the
 * given string */
unsigned int str_hash(const char *str);
unsigned int str_nhash(const char *str, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif

