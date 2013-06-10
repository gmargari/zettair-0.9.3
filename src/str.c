/* str.c implements different (quicker) implementations of the
 * standard library string functions (and some additional extras such
 * as strlcat and strlcpy from the OpenBSD project)
 *
 * written nml 2003-03-26
 *
 */

#include "firstinclude.h"

#include "ascii.h"
#include "str.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>                       /* need malloc and free for strdup */
#include <string.h>

size_t str_len(const char *s) {
    register const char *pos = s;

    while (*pos) {
        pos++;
    }

    return pos - s;
}

size_t str_nlen(const char *s, size_t maxlen) {
    register const char *pos = s,
                        *end = s + maxlen;

    while (*pos && (pos < end)) {
        pos++;
    }

    return pos - s;
}

int str_cmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }

    return (unsigned char) *s1 - (unsigned char) *s2;
}

int str_ncmp(const char *s1, const char *s2, size_t size) {
    while (size && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        size--;
    }

    if (size) {
        return (unsigned char) *s1 - (unsigned char) *s2;
    } else {
        return 0;
    }
}

int str_nncmp(const char *s1, size_t size1, const char *s2, size_t size2) {
    int def;
    unsigned int len;

    if (size1 < size2) {
        def = -1;
        len = size1;
    } else if (size1 > size2) {
        def = 1;
        len = size2;
    } else {
        len = size1;
        def = 0;
    }

    while (len && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        len--;
    }

    if (len) {
        return (unsigned char) *s1 - (unsigned char) *s2;
    } else {
        return def;
    }
}

char *str_cpy(char *dst, const char *src) {
    register char *dstpos = dst;

    while ((*dstpos = *src)) {
        dstpos++;
        src++;
    }

    return dst;
}

char *str_cat(char *dst, const char *src) {
    register char *dstpos = dst;

    while (*dstpos) {
        dstpos++;
    }

    while ((*dstpos = *src)) {
        dstpos++;
        src++;
    }

    return dst;
}

char *str_ncpy(char *dst, const char *src, 
  size_t size) {
    register char *dstpos = dst;

    while (size && (*dstpos = *src)) {
        dstpos++;
        src++;
        size--;
    }

    /* '\0' fill remaining space */
    while (size) {
        *dstpos = '\0';
        size--;
        dstpos++;
    }

    return dst;
}

char *str_ncat(char *dst, const char *src, 
  size_t size) {
    register char *dstpos = dst;

    if (size) {
        while (*dstpos) {
            dstpos++;
        }

        while (size && (*dstpos = *src)) {
            dstpos++;
            src++;
            size--;
        }

        *dstpos = '\0';
    }

    return dst;
}

size_t str_lcpy(char *dst, const char *src, size_t size) {
    register const char *pos = src;

    if (size) {
        /* ensure that size is not zero */
        while (--size && (*dst = *pos)) {
            dst++;
            pos++;
        }

        /* add NUL */
        *dst = '\0';

        /* traverse rest of src to get accurate count */
        while (*pos) {
            pos++;
        }
    } else {
        /* size was 0, but we need to traverse src to get accurate count */
        while (*pos) {
            pos++;
        }
    }

    return pos - src;   /* return count doesn't include '\0' */
}

size_t str_lcat(char *dst, const char *src, size_t size) {
    register const char *srcpos = src;
    register char *dstpos = dst;
    register unsigned int dstlen = 0;

    if (size) {
        /* traverse dst until we reach the end */
        size--;
        while (size && *dstpos) {
            dstpos++;
            size--;
        }
        
        dstlen = dstpos - dst;

        if (size) {
            /* copy src to dst */
            while (size && (*dstpos = *srcpos)) {
                dstpos++;
                srcpos++;
                size--;
            }
        } 

        /* terminate the string */
        *dstpos = '\0';
    }

    /* traverse src to get an accurate return count */
    while (*srcpos) {
        srcpos++;
    }

    return dstlen + (srcpos - src);
}

char *str_dup(const char *s) {
    register const char *str = s;
    register char *dupstr;
    char *dup;

    /* get length of string */
    while (*str) {
        str++;
    }

    if ((dup = malloc(((str - s) + 1) * sizeof(char)))) {
        dupstr = dup;
        while ((*dupstr = *s)) {
            dupstr++;
            s++;
        }

        return dup;
    } else {
        return NULL;
    }
}

char *str_ndup(const char *str, size_t size) {
    register char *copy,
                  *pos;
    unsigned int i,
                 j;

    for (i = 0; (i < size) && str[i]; i++) ;

    if ((pos = copy = malloc(i + 1))) {
        for (j = 0; j < i; j++) {
            *pos = *str;
            pos++;
            str++;
        }
        *pos = '\0';
    }

    return copy;
}

#define CASE_SPACE                                                            \
         ' ': case '\t': case '\n': case '\f': case '\v': case '\r'

const char *str_ltrim(const char *s) {
    do {
        switch (*s) {
        case CASE_SPACE:
            s++;
            break;
    
        default:
            return s;
        }
    } while (1);   /* loop forever */
}

unsigned int str_rtrim(char *s) {
    unsigned int lpos = 0;       /* start of last run we found a space at */
    char *sc = s;

/* state machine that switches between in a whitespace sequence (inseq) and not
 * in a whitespace sequence (outseq).  If the string terminates in a whitespace
 * sequence, then it is reterminated and the number of characters removed is
 * returned.  Otherwise it is untouched. */

outseq_label:
    switch (*sc++) {
    case CASE_SPACE:
        lpos = sc - s;
        goto inseq_label;

    case '\0':
        /* didn't find a non-whitespace character */
        return 0;

    default:
        break;
    }
    goto outseq_label;   /* loop */

inseq_label:
    switch (*sc++) {
    case CASE_SPACE:
        /* do nothing, it just continues sequence */
        break;

    case '\0':
        /* found the end of string in a whitespace sequence, terminate
         * string where we started */
        s[lpos - 1] = '\0';
        return (sc - s) - lpos;

    default:
        goto outseq_label;
        break;
    }
    goto inseq_label;   /* loop */
}

unsigned int str_tolower(char *s) {
    unsigned int converted = 0;

    do {
        switch (*s) {
        case ASCII_CASE_UPPER:
            *s = ASCII_TOLOWER(*s);
            converted++;
            break;

        case '\0':
            return converted;
        }
        s++;
    } while (1);
}

unsigned int str_ntolower(char *s, size_t size) {
    unsigned int converted = 0;

    while (size--) {
        switch (*s) {
        case ASCII_CASE_UPPER:
            *s = ASCII_TOLOWER(*s);
            converted++;
            break;

        case '\0':
            return converted;
        }
        s++;
    }

    return converted;
}

unsigned int str_toupper(char *s) {
    unsigned int converted = 0;

    do {
        switch (*s) {
        case ASCII_CASE_LOWER:
            *s = ASCII_TOUPPER(*s);
            converted++;
            break;

        case '\0':
            return converted;
        }
        s++;
    } while (1);
}

unsigned int str_ntoupper(char *s, size_t size) {
    unsigned int converted = 0;

    while (size--) {
        switch (*s) {
        case ASCII_CASE_LOWER:
            *s = ASCII_TOUPPER(*s);
            converted++;
            break;

        case '\0':
            return converted;
        }
        s++;
    }

    return converted;
}

/* lookup array of lowercase converted characters (note that we'll offset
 * lookups into this array by 128 to get rid of signed/unsigned problems) */
static char lookup[] = {
    /* first comes the extended character set in case chars are signed */
    (char) 128, (char) 129, (char) 130, (char) 131, (char) 132, (char) 133, 
    (char) 134, (char) 135, (char) 136, (char) 137, (char) 138, (char) 139, 
    (char) 140, (char) 141, (char) 142, (char) 143, (char) 144, (char) 145, 
    (char) 146, (char) 147, (char) 148, (char) 149, (char) 150, (char) 151, 
    (char) 152, (char) 153, (char) 154, (char) 155, (char) 156, (char) 157, 
    (char) 158, (char) 159, (char) 160, (char) 161, (char) 162, (char) 163, 
    (char) 164, (char) 165, (char) 166, (char) 167, (char) 168, (char) 169, 
    (char) 170, (char) 171, (char) 172, (char) 173, (char) 174, (char) 175, 
    (char) 176, (char) 177, (char) 178, (char) 179, (char) 180, (char) 181, 
    (char) 182, (char) 183, (char) 184, (char) 185, (char) 186, (char) 187, 
    (char) 188, (char) 189, (char) 190, (char) 191, (char) 192, (char) 193, 
    (char) 194, (char) 195, (char) 196, (char) 197, (char) 198, (char) 199, 
    (char) 200, (char) 201, (char) 202, (char) 203, (char) 204, (char) 205, 
    (char) 206, (char) 207, (char) 208, (char) 209, (char) 210, (char) 211, 
    (char) 212, (char) 213, (char) 214, (char) 215, (char) 216, (char) 217, 
    (char) 218, (char) 219, (char) 220, (char) 221, (char) 222, (char) 223, 
    (char) 224, (char) 225, (char) 226, (char) 227, (char) 228, (char) 229, 
    (char) 230, (char) 231, (char) 232, (char) 233, (char) 234, (char) 235, 
    (char) 236, (char) 237, (char) 238, (char) 239, (char) 240, (char) 241, 
    (char) 242, (char) 243, (char) 244, (char) 245, (char) 246, (char) 247, 
    (char) 248, (char) 249, (char) 250, (char) 251, (char) 252, (char) 253, 
    (char) 254, (char) 255,

    /* then the regular 7-bit character set */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 
    21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, '!', '"', '#', '$', '%', 
    '&', '\'', '(', ')', '*', '+', ',', '-', '.', '/', '0', '1', '2', '3', '4', 
    '5', '6', '7', '8', '9', ':', ';', '<', '=', '>', '?', '@', 'a', 'b', 'c', 
    'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 
    's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '[', '\\', ']', '^', '_', '`', 'a', 
    'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 
    'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '{', '|', '}', '~', 127, 
   
    /* then the extended character set again in case chars are unsigned */
    (char) 128, (char) 129, (char) 130, (char) 131, (char) 132, (char) 133, 
    (char) 134, (char) 135, (char) 136, (char) 137, (char) 138, (char) 139, 
    (char) 140, (char) 141, (char) 142, (char) 143, (char) 144, (char) 145, 
    (char) 146, (char) 147, (char) 148, (char) 149, (char) 150, (char) 151, 
    (char) 152, (char) 153, (char) 154, (char) 155, (char) 156, (char) 157, 
    (char) 158, (char) 159, (char) 160, (char) 161, (char) 162, (char) 163, 
    (char) 164, (char) 165, (char) 166, (char) 167, (char) 168, (char) 169, 
    (char) 170, (char) 171, (char) 172, (char) 173, (char) 174, (char) 175, 
    (char) 176, (char) 177, (char) 178, (char) 179, (char) 180, (char) 181, 
    (char) 182, (char) 183, (char) 184, (char) 185, (char) 186, (char) 187, 
    (char) 188, (char) 189, (char) 190, (char) 191, (char) 192, (char) 193, 
    (char) 194, (char) 195, (char) 196, (char) 197, (char) 198, (char) 199, 
    (char) 200, (char) 201, (char) 202, (char) 203, (char) 204, (char) 205, 
    (char) 206, (char) 207, (char) 208, (char) 209, (char) 210, (char) 211, 
    (char) 212, (char) 213, (char) 214, (char) 215, (char) 216, (char) 217, 
    (char) 218, (char) 219, (char) 220, (char) 221, (char) 222, (char) 223, 
    (char) 224, (char) 225, (char) 226, (char) 227, (char) 228, (char) 229, 
    (char) 230, (char) 231, (char) 232, (char) 233, (char) 234, (char) 235, 
    (char) 236, (char) 237, (char) 238, (char) 239, (char) 240, (char) 241, 
    (char) 242, (char) 243, (char) 244, (char) 245, (char) 246, (char) 247, 
    (char) 248, (char) 249, (char) 250, (char) 251, (char) 252, (char) 253, 
    (char) 254, (char) 255
};

int str_casecmp(const char *s1, const char *s2) {
    const char *us1 = (const char*) s1,
                        *us2 = (const char*) s2;

    while (*us1 && (lookup[*us1 + 128] == lookup[*us2 + 128])) {
        us1++;
        us2++;
    }

    return (unsigned char) lookup[*us1 + 128] 
      - (unsigned char) lookup[*us2 + 128];
}

int str_ncasecmp(const char *s1, const char *s2, 
  size_t size) {
    const char *us1 = (const char*) s1,
                        *us2 = (const char*) s2;

    while (size && *us1 && (lookup[*us1 + 128] == lookup[*us2 + 128])) {
        us1++;
        us2++;
        size--;
    }

    if (size) {
        return (unsigned char) lookup[*us1 + 128] 
          - (unsigned char) lookup[*us2 + 128];
    } else {
        return 0;
    }
}

char *str_chr(const char *s, int c) {
    do {
        if (*s == c) {
            return (char*) s;
        } else if (!*s) {
            return NULL;
        } else {
            s++;
        }
    } while (1);
}

char *str_rchr(const char *s, int c) {
    char *last = NULL;

    do {
        if (*s == c) {
            last = (char*) s;
        } 

        /* can't else here otherwise we can't detect c as \0 */
        if (!*s) {
            return last;
        } else {
            s++;
        }
    } while (1);
}

char **str_split(char *str, const char *delim, 
  unsigned int *parts) {
    char **arr = malloc(sizeof(char*) + sizeof(char*));
    void *ptr;
    char *pos;
    unsigned int i,
                 len = str_len(delim);

    /* advance string past leading delimeters */
    while (*str) {
        for (i = 0; i < len; i++) {
            if (*str == delim[i]) {
                break;
            }
        }

        if (i == len) {
            break;
        } else {
            str++;
        }
    } 

    if (arr) {
        *arr = pos = str;
        *parts = 1;
    } else {
        return NULL;
    }

/* outside of a delimiter string */
out_delim_label:
    while (*pos) {
        for (i = 0; i < len; i++) {
            if (*pos == delim[i]) {
                /* found a delimiter, need to split */
                if ((ptr = realloc(arr, sizeof(char*) * (*parts + 2)))) {
                    (*parts)++;
                    arr = ptr;
                    *pos = '\0';
                    pos++;

                    /* we anticipate that the next character will be the first 
                     * character in the split string (and if not we push it 
                     * again) */
                    arr[*parts - 1] = pos;

                    goto in_delim_label;
                } else {
                    if (arr) {
                        free(arr);
                    }
                    return NULL;
                }
            }
        }
        pos++;
    }

    arr[*parts] = NULL;      /* NULL terminate */
    return arr;

/* in a delimiter string */
in_delim_label:
    while (*pos) {
        for (i = 0; i < len; i++) {
            if (*pos == delim[i]) {
                *pos = '\0';
                break;
            }
        }

        pos++;

        if (i == len) {
            /* it wasn't a delimiter, switch states */
            goto out_delim_label;
        }

        /* we anticipate that the next character will be the first character 
         * in the split string (and if not we push it again) */
        arr[*parts - 1] = pos;
    }

    /* there ended up not being another split string, so decrement parts */
    (*parts)--;

    arr[*parts] = NULL;      /* NULL terminate */
    return arr;
}

int str_signed_char() {
    return STR_SIGNED_CHAR;
}

unsigned int str_from_char(char c) {
    return STR_FROM_CHAR(c);
}

char str_to_char(unsigned int c) {
    return STR_TO_CHAR(c);
}

unsigned int str_strip(char *str) {
    char *readpos = str,
         *writepos = str;

    /* haven't received any acceptable characters yet */
    while (1) {
        switch (*readpos) {
        case '\0':
            /* end of string */
            *writepos++ = *readpos++;
            return readpos - writepos;

        case ASCII_CASE_UPPER:
            *writepos++ = ASCII_TOLOWER(*readpos++);
            goto inword_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* preserve */
            *writepos++ = *readpos++;
            goto inword_label;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
        case CASE_SPACE:
        case ASCII_CASE_PUNCTUATION:
            /* strip */
            readpos++;
            break;

        /* shouldn't happen, default should be the null set */
        default:
            assert(0);
        }
    }

/* just parsed a preserved char, so we can accept a hyphen if it comes */
inword_label:
    while (1) {
        switch (*readpos) {
        case '\0':
            /* end of string */
            *writepos++ = *readpos++;
            return readpos - writepos;

        case ASCII_CASE_UPPER:
            *writepos++ = ASCII_TOLOWER(*readpos++);
            break;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* preserve */
            *writepos++ = *readpos++;
            break;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
        case CASE_SPACE:
        case '-':
        default:  /* default is the rest of the punctuation here */
            /* strip */
            readpos++;
            break;
        }
    }
}

char *str_dirname(char *dst, unsigned int dstsize, const char *src) {
    char *end = str_rchr(src, OS_SEPARATOR);

    if (!end) {
        str_lcpy(dst, ".", dstsize);

    /* figure out if string will fit in dst, stepping over the seperator
     * character (++end) beforehand */
    } else if ((unsigned int) (++end - src) >= dstsize) {
        memcpy(dst, src, dstsize - 1);
        dst[dstsize - 1] = '\0';
    } else {
        memcpy(dst, src, end - src);
        dst[end - src] = '\0';
    }

    return dst;
}

const char *str_basename(const char *src) {
    char *end = str_rchr(src, OS_SEPARATOR);

    if (!end) {
        return src;
    } else {
        if (!end[1]) {
            /* points to trailing slash, need to back up to either start 
             * of string or previous slash.  Unlike reference basename, 
             * we don't strip trailing slashes off. */

            /* first, back up until we have something thats not a directory
             * seperator */
            while (end >= src && *end == OS_SEPARATOR) {
                end--;
            }

            if (end >= src) {
                /* now, skip back until the next seperator */
                while (end >= src && *end != OS_SEPARATOR) {
                    end--;
                }
            }
        }
        return &end[1];
    }
}

#define LARGEPRIME 100000007

unsigned int str_hash(const char *str) {
    /* Author J. Zobel, April 2001. */
    char c;
    unsigned int h = LARGEPRIME;
    const char* word = str;

    for (; (c = *word) != '\0'; word++) {
        h ^= ((h << 5) + c + (h >> 2));
    }

    return h;
}

unsigned int str_nhash(const char *str, unsigned int len) {
    /* Author J. Zobel, April 2001. */
    char c;
    unsigned int h = LARGEPRIME,
                 i;
    const char* word = str;

    for (i = 0; (i < len) && ((c = *word) != '\0'); word++, i++) {
        h ^= ((h << 5) + c + (h >> 2));
    }

    return h;
}

