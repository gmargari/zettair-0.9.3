/* str_1.c is a unit test for the string library.  It has test cases
 * hardwired into it, and so doesn't accept any input files.
 *
 * written nml 2003-10-22
 *
 */

#include "firstinclude.h"

#include "test.h"
#include "str.h"

#include <stdlib.h>
#include <string.h>

#define rt_assert(b) frt_assert(b, __LINE__)

void frt_assert(int boolean, unsigned int line) {
    if (!boolean) {
        fprintf(stderr, "str test on line %u failed\n", line);
        exit(EXIT_FAILURE);
    }
}

/* reference versions of ref_strlcat and ref_strlcpy were taken from OpenBSD
 * under the following license */

/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

size_t ref_strlcat(char *dst, const char *src, size_t siz) {
	register char *d = dst;
	register const char *s = src;
	register size_t n = siz;
	size_t dlen;

	/* Find the end of dst and adjust bytes left but don't go past end */
	while (n-- != 0 && *d != '\0')
		d++;
	dlen = d - dst;
	n = siz - dlen;

	if (n == 0)
		return(dlen + strlen(s));
	while (*s != '\0') {
		if (n != 1) {
			*d++ = *s;
			n--;
		}
		s++;
	}
	*d = '\0';

	return(dlen + (s - src));	/* count does not include NUL */
}

size_t ref_strlcpy(char *dst, const char *src, size_t siz) {
	register char *d = dst;
	register const char *s = src;
	register size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0 && --n != 0) {
		do {
			if ((*d++ = *s++) == 0)
				break;
		} while (--n != 0);
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0)
			*d = '\0';		/* NUL-terminate dst */
		while (*s++)
			;
	}

	return(s - src - 1);	/* count does not include NUL */
}

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRMAX 30

int str_len_test(const char *s) {
    return str_len(s) == strlen(s);
}

int str_cmp_test(const char *s1, const char *s2) {
    int ret = str_cmp(s1, s2);

    if (ret > 0) {
        return (strcmp(s1, s2) > 0);
    } else if (ret < 0) {
        return (strcmp(s1, s2) < 0);
    } else {
        return !strcmp(s1, s2);
    }
}

int str_nncmp_test(const char *s1, const char *s2) {
    unsigned int size1 = str_len(s1),
                 size2 = str_len(s2);

    int ret = str_nncmp(s1, size1, s2, size2);
    int cmp;

    if (size1 < size2) {
        if (strncmp(s1, s2, size1) <= 0) {
            cmp = -1;
        } else {
            cmp = 1;
        }
    } else if (size1 > size2) {
        if (strncmp(s1, s2, size2) < 0) {
            cmp = -1;
        } else {
            cmp = 1;
        }
    } else {
        cmp = strncmp(s1, s2, size1);
    }

    return (((ret < 0) && (cmp < 0)) || (!ret && !cmp) 
      || ((ret > 0) && (cmp > 0)));
}

int str_ncmp_test(const char *s1, const char *s2, size_t size) {
    int ret = str_ncmp(s1, s2, size);

    if (ret > 0) {
        return (strncmp(s1, s2, size) > 0);
    } else if (ret < 0) {
        return (strncmp(s1, s2, size) < 0);
    } else {
        return !strncmp(s1, s2, size);
    }
}

int str_casecmp_test(const char *s1, const char *s2) {
    int ret = str_casecmp(s1, s2);

    if (ret > 0) {
        return (strcasecmp(s1, s2) > 0);
    } else if (ret < 0) {
        return (strcasecmp(s1, s2) < 0);
    } else {
        return !strcasecmp(s1, s2);
    }
}

int str_ncasecmp_test(const char *s1, const char *s2, size_t size) {
    int ret = str_ncasecmp(s1, s2, size);

    if (ret > 0) {
        return (strncasecmp(s1, s2, size) > 0);
    } else if (ret < 0) {
        return (strncasecmp(s1, s2, size) < 0);
    } else {
        return !strncasecmp(s1, s2, size);
    }
}

int str_dup_test(const char *s) {
    int ret;
    char *dup = str_dup(s);

    ret = !memcmp(s, dup, strlen(s) + 1);
    free(dup);
    return ret;
}

int str_ndup_test(const char *s, size_t size) {
    char *dup = str_ndup(s, size);
    char buf[STRMAX + 1];

    if (str_len(s) > size) {
        memcpy(buf, s, size);
        buf[size] = '\0';
    } else {
        memcpy(buf, s, str_len(s));
        buf[str_len(s)] = '\0';
    }

    if (str_cmp(dup, buf)) {
        /* they're different, failed */
        free(dup);
        return 0;
    } else {
        free(dup);
        return 1;
    }
}

int str_cpy_test(const char *s) {
    char buf[STRMAX + 1];
    char buf2[STRMAX + 1];

    memset(buf, 0xff, STRMAX + 1);
    memset(buf2, 0xff, STRMAX + 1);

    rt_assert(str_cpy(buf, s) == &buf[0]);
    rt_assert(strcpy(buf2, s) == &buf2[0]);

    return !memcmp(buf, buf2, STRMAX + 1);
}

int str_ncpy_test(const char *s, size_t size) {
    char buf[STRMAX + 1];
    char buf2[STRMAX + 1];

    memset(buf, 0xff, STRMAX + 1);
    memset(buf2, 0xff, STRMAX + 1);

    rt_assert(str_ncpy(buf, s, size) == &buf[0]);
    rt_assert(strncpy(buf2, s, size) == &buf2[0]);

    return !memcmp(buf, buf2, STRMAX + 1);
}

int str_lcpy_test(const char *s, size_t size) {
    char buf[STRMAX + 1];
    char buf2[STRMAX + 1];
    size_t sz, 
           sz2;

    memset(buf, 0xff, STRMAX + 1);
    memset(buf2, 0xff, STRMAX + 1);

    sz = str_lcpy(buf, s, size);
    sz2 = ref_strlcpy(buf2, s, size);

    return (sz == sz2) && !memcmp(buf, buf2, STRMAX + 1);
}

int str_cat_test(const char *s1, const char *s2) {
    char buf[STRMAX + 1];
    char buf2[STRMAX + 1];

    memset(buf, 0xff, STRMAX + 1);
    memset(buf2, 0xff, STRMAX + 1);
    buf[0] = '\0';
    buf2[0] = '\0';

    rt_assert(str_cat(buf, s1) == &buf[0]);
    rt_assert(strcat(buf2, s1) == &buf2[0]);

    rt_assert(!memcmp(buf, buf2, STRMAX + 1));

    rt_assert(str_cat(buf, s2) == &buf[0]);
    rt_assert(strcat(buf2, s2) == &buf2[0]);

    return !memcmp(buf, buf2, STRMAX + 1);
}

int str_ncat_test(const char *s1, const char *s2, size_t size) {
    char buf[STRMAX + 1];
    char buf2[STRMAX + 1];

    memset(buf, 0xff, STRMAX + 1);
    memset(buf2, 0xff, STRMAX + 1);
    buf[0] = '\0';
    buf2[0] = '\0';
    buf[STRMAX] = '\0';
    buf2[STRMAX] = '\0';

    rt_assert(str_ncat(buf, s1, size) == &buf[0]);
    rt_assert(strncat(buf2, s1, size) == &buf2[0]);

    /* adjust size argument */
    if (size > strlen(s1)) {
        size -= strlen(s1);
    } else {
        size = 0;
    }

    rt_assert(!memcmp(buf, buf2, STRMAX + 1));

    rt_assert(str_ncat(buf, s2, size) == &buf[0]);
    rt_assert(strncat(buf2, s2, size) == &buf2[0]);

    return !memcmp(buf, buf2, STRMAX + 1);
}

int str_lcat_test(const char *s1, const char *s2, size_t size) {
    char buf[STRMAX + 1];
    char buf2[STRMAX + 1];
    size_t sz, 
           sz2;

    memset(buf, 0xff, STRMAX + 1);
    memset(buf2, 0xff, STRMAX + 1);
    buf[0] = '\0';
    buf2[0] = '\0';

    sz = str_lcat(buf, s1, size);
    sz2 = ref_strlcat(buf2, s1, size);

    rt_assert(sz == sz2);
    rt_assert(!memcmp(buf, buf2, STRMAX + 1));

    sz = str_lcat(buf, s2, size);
    sz2 = ref_strlcat(buf2, s2, size);

    return (sz == sz2) && !memcmp(buf, buf2, STRMAX + 1);
}

int str_ltrim_test(const char *s1) {
    const char *s2;
    unsigned int i;

    /* find first non-space char in s1 */
    for (i = 0; i < str_len(s1); i++) {
        if (!isspace(s1[i])) {
            break;
        }
    }

    s2 = str_ltrim(s1);

    return s2 == &s1[i];
}

int str_rtrim_test(const char *s1) {
    char buf[STRMAX + 1];
    unsigned int i,
                 last = str_len(s1) - 1,
                 trimmed,
                 len = str_len(s1);

    rt_assert(len < STRMAX);
    strcpy(buf, s1);

    /* find last non-space char in s1 */
    for (i = 0; i < len; i++) {
        if (!isspace(s1[i])) {
            last = i;
        }
    }

    trimmed = str_rtrim(buf);

    return (buf[last + 1] == '\0') && (trimmed + str_len(buf) == len);
}

int str_chr_test(const char *s1, int c) {
    char *one = strchr(s1, c),
         *two = str_chr(s1, c);

    return one == two;
}

int str_rchr_test(const char *s1, int c) {
    char *one = strrchr(s1, c),
         *two = str_rchr(s1, c);

    return one == two;
}

int str_dirname_test(const char *s1, const char *s2) {
    char buf[FILENAME_MAX + 1];

    str_dirname(buf, FILENAME_MAX, s1);

    return !str_cmp(buf, s2);
}

int str_basename_test(const char *s1, const char *s2) {
    return !str_cmp(str_basename(s1), s2);
}

static int char_inset(char c, const char *set) {
    while (*set) {
        if (c == *set) {
            return 1;
        }
        set++;
    }
    return 0;
}

int str_split_test(const char *s1, const char *delim) {
    char *one = str_dup(s1);
    const char *orig_pos = s1,
               *new_pos;
    char **arr;
    int state = 0;   /* 1 for in delimiter, 0 otherwise */
    unsigned int numsplit,
                 i = 0;
    int ret;

    if (one && (arr = str_split(one, delim, &numsplit))) {
        new_pos = arr[i];

        while (*orig_pos && char_inset(*orig_pos, delim)) orig_pos++;

        while (*orig_pos) {
            if (state) {
                /* in a delimiter */
                if (!char_inset(*orig_pos, delim)) {
                    /* switch states and should match first char next split */
                    state = 0;
                    if (i < numsplit + 1) {
                        i++;
                        new_pos = arr[i];
                    } else {
                        free(one);
                        free(arr);
                        return 0;
                    }
                    if (*new_pos != *orig_pos) {
                        free(one);
                        free(arr);
                        return 0;
                    } else {
                        new_pos++;
                    }
                }
            } else {
                /* outside of a delimiter */
                if (char_inset(*orig_pos, delim)) {
                    /* switch states */
                    state = 1;
                } else if (*orig_pos != *new_pos) {
                    free(one);
                    free(arr);
                    return 0;
                } else {
                    new_pos++;
                }
            }
            orig_pos++;
        }
        /* make sure there's no more stuff from the split */
        ret = (i + 1 == numsplit) && (!*new_pos);
        free(one);
        free(arr);
        return ret;
    } else {
        if (one) {
            free(one);
        }
        return 0;
    }
}

int test_file(FILE *fp, int argc, char **argv) {

    if (fp && (fp != stdin)) {
        fprintf(stderr, "string library test run with file, "
          "but contains embedded tests (fp %p stdin %p)\n", 
          (void *) fp, (void *) stdin);
        return EXIT_FAILURE;
    }

    /* test str_len */
    rt_assert(str_len_test(""));
    rt_assert(str_len_test("a"));
    rt_assert(str_len_test("ab"));
    rt_assert(str_len_test("ablaksjdflaksjdflkajsdfljaslfdj"));

    /* test str_cmp */
    rt_assert(str_cmp_test("", ""));
    rt_assert(str_cmp_test("a", ""));
    rt_assert(str_cmp_test("", "a"));
    rt_assert(str_cmp_test("a", "a"));
    rt_assert(str_cmp_test("a", "b"));
    rt_assert(str_cmp_test("b", "a"));
    rt_assert(str_cmp_test("aa", "a"));
    rt_assert(str_cmp_test("a", "aa"));

    /* test str_ncmp */
    rt_assert(str_ncmp_test("", "", strlen("")));
    rt_assert(str_ncmp_test("", "", strlen("") + 1));
    rt_assert(str_ncmp_test("a", "a", strlen("")));
    rt_assert(str_ncmp_test("a", "a", strlen("") + 1));
    rt_assert(str_ncmp_test("a", "a", strlen("") - 1));
    rt_assert(str_ncmp_test("a", "b", strlen("")));
    rt_assert(str_ncmp_test("a", "b", strlen("") + 1));
    rt_assert(str_ncmp_test("a", "b", strlen("") - 1));
    rt_assert(str_ncmp_test("b", "a", strlen("")));
    rt_assert(str_ncmp_test("b", "a", strlen("") + 1));
    rt_assert(str_ncmp_test("b", "a", strlen("") - 1));
    rt_assert(str_ncmp_test("aaaaab", "aaaaaa", strlen("aaaaaa"))); 
    rt_assert(str_ncmp_test("aaaaab", "aaaaaa", strlen("aaaaaa") + 1)); 
    rt_assert(str_ncmp_test("aaaaab", "aaaaaa", strlen("aaaaaa") - 1)); 
    rt_assert(str_ncmp_test("aaaaab", "aaaaaa", strlen("aaaaaa") + 2)); 
    rt_assert(str_ncmp_test("aaaaab", "aaaaaa", strlen("aaaaaa") - 2)); 
    rt_assert(str_ncmp_test("aaaaab", "aaaaaa", 0)); 
    rt_assert(str_ncmp_test("aaaaab", "aaaaaa", STRMAX)); 
    rt_assert(str_ncmp_test("\xff", "aaaaaa", STRMAX)); 

    /* test str_nncmp */
    rt_assert(str_nncmp_test("", ""));
    rt_assert(str_nncmp_test("", "a"));
    rt_assert(str_nncmp_test("a", ""));
    rt_assert(str_nncmp_test("a", "a"));
    rt_assert(str_nncmp_test("a", "b"));
    rt_assert(str_nncmp_test("", "b"));
    rt_assert(str_nncmp_test("a", "ba"));
    rt_assert(str_nncmp_test("aa", "b"));
    rt_assert(str_nncmp_test("baa", "a"));
    rt_assert(str_nncmp_test("b", "abb"));
    rt_assert(str_nncmp_test("ba", "ab"));
    rt_assert(str_nncmp_test("aaaaab", "aaaaaa"));
    rt_assert(str_nncmp_test("aaaaaba", "aaaaaa"));
    rt_assert(str_nncmp_test("aaaaa", "aaaaaa"));
    rt_assert(str_nncmp_test("aaaa", "aaaaaa"));
    rt_assert(str_nncmp_test("aaa", "aaaaaa"));
    rt_assert(str_nncmp_test("aa", "aaaaa"));
    rt_assert(str_nncmp_test("a", "aaaaaa"));
    rt_assert(str_nncmp_test("\xff", "aaaaaa"));
    rt_assert(str_nncmp_test("\xff", "aaaaa"));
    rt_assert(str_nncmp_test("\xff", "aaaa"));
    rt_assert(str_nncmp_test("\xff", "aaa"));
    rt_assert(str_nncmp_test("\xff", "a"));
    rt_assert(str_nncmp_test("\xff", ""));

    /* test str_casecmp */
    rt_assert(str_casecmp_test("", ""));
    rt_assert(str_casecmp_test("a", ""));
    rt_assert(str_casecmp_test("", "a"));
    rt_assert(str_casecmp_test("a", "a"));
    rt_assert(str_casecmp_test("A", "a"));
    rt_assert(str_casecmp_test("a", "A"));
    rt_assert(str_casecmp_test("a", "b"));
    rt_assert(str_casecmp_test("A", "b"));
    rt_assert(str_casecmp_test("a", "B"));
    rt_assert(str_casecmp_test("b", "a"));
    rt_assert(str_casecmp_test("aa", "a"));
    rt_assert(str_casecmp_test("a", "aa"));
    rt_assert(str_casecmp_test("aAa", "aaA"));

    /* test str_ncasecasecmp */
    rt_assert(str_ncasecmp_test("", "", strlen("")));
    rt_assert(str_ncasecmp_test("", "", strlen("") + 1));
    rt_assert(str_ncasecmp_test("a", "a", strlen("")));
    rt_assert(str_ncasecmp_test("a", "a", strlen("") + 1));
    rt_assert(str_ncasecmp_test("a", "a", strlen("") - 1));
    rt_assert(str_ncasecmp_test("A", "a", strlen("")));
    rt_assert(str_ncasecmp_test("A", "a", strlen("") + 1));
    rt_assert(str_ncasecmp_test("A", "a", strlen("") - 1));
    rt_assert(str_ncasecmp_test("a", "A", strlen("")));
    rt_assert(str_ncasecmp_test("a", "A", strlen("") + 1));
    rt_assert(str_ncasecmp_test("a", "A", strlen("") - 1));
    rt_assert(str_ncasecmp_test("a", "B", strlen("")));
    rt_assert(str_ncasecmp_test("a", "B", strlen("") + 1));
    rt_assert(str_ncasecmp_test("a", "B", strlen("") - 1));
    rt_assert(str_ncasecmp_test("a", "b", strlen("")));
    rt_assert(str_ncasecmp_test("a", "b", strlen("") + 1));
    rt_assert(str_ncasecmp_test("a", "b", strlen("") - 1));
    rt_assert(str_ncasecmp_test("b", "A", strlen("")));
    rt_assert(str_ncasecmp_test("b", "A", strlen("") + 1));
    rt_assert(str_ncasecmp_test("b", "A", strlen("") - 1));
    rt_assert(str_ncasecmp_test("b", "a", strlen("")));
    rt_assert(str_ncasecmp_test("b", "a", strlen("") + 1));
    rt_assert(str_ncasecmp_test("b", "a", strlen("") - 1));
    rt_assert(str_ncasecmp_test("aaaaaA", "aaaaaa", strlen("aaaaaa"))); 
    rt_assert(str_ncasecmp_test("aaaaaA", "aaaaaa", strlen("aaaaaa") + 1)); 
    rt_assert(str_ncasecmp_test("aaaaaA", "aaaaaa", strlen("aaaaaa") - 1)); 
    rt_assert(str_ncasecmp_test("aaaaaA", "aaaaaa", strlen("aaaaaa") + 2)); 
    rt_assert(str_ncasecmp_test("aaaaaA", "aaaaaa", strlen("aaaaaa") - 2)); 
    rt_assert(str_ncasecmp_test("aaaaaA", "aaaaaa", 0)); 
    rt_assert(str_ncasecmp_test("aaaaaA", "aaaaaa", STRMAX)); 
    rt_assert(str_ncasecmp_test("aaaaaB", "aaaaaa", strlen("aaaaaa"))); 
    rt_assert(str_ncasecmp_test("aaaaaB", "aaaaaa", strlen("aaaaaa") + 1)); 
    rt_assert(str_ncasecmp_test("aaaaaB", "aaaaaa", strlen("aaaaaa") - 1)); 
    rt_assert(str_ncasecmp_test("aaaaaB", "aaaaaa", strlen("aaaaaa") + 2)); 
    rt_assert(str_ncasecmp_test("aaaaaB", "aaaaaa", strlen("aaaaaa") - 2)); 
    rt_assert(str_ncasecmp_test("aaaaaB", "aaaaaa", 0)); 
    rt_assert(str_ncasecmp_test("aaaaaB", "aaaaaa", STRMAX)); 
    rt_assert(str_ncasecmp_test("\xff", "aaaaaa", STRMAX)); 
    rt_assert(str_ncasecmp_test("\xfa", "aaaaaa", STRMAX)); 
    rt_assert(str_ncasecmp_test("\xf3", "aaaaaa", STRMAX)); 

    /* test str_dup */
    rt_assert(str_dup_test(""));
    rt_assert(str_dup_test("a"));
    rt_assert(str_dup_test("ab"));
    rt_assert(str_dup_test("ablaksjdflaksjdflkajsdfljaslfdj"));

    /* test str_ndup */
    rt_assert(str_ndup_test("", 0));
    rt_assert(str_ndup_test("", 1));
    rt_assert(str_ndup_test("", 10));
    rt_assert(str_ndup_test("a", 0));
    rt_assert(str_ndup_test("a", 1));
    rt_assert(str_ndup_test("a", 2));
    rt_assert(str_ndup_test("ab", 0));
    rt_assert(str_ndup_test("ab", 1));
    rt_assert(str_ndup_test("ab", 2));
    rt_assert(str_ndup_test("ab", 3));
    rt_assert(str_ndup_test("ablaksjdflaksjdflkajsdfljaslfdj", 0));
    rt_assert(str_ndup_test("ablaksjdflaksjdflkajsdfljaslfdj", 10));
    rt_assert(str_ndup_test("ablaksjdflaksjdflkajsdfljaslfdj", 100));

    /* test str_cpy */
    rt_assert(str_cpy_test(""));
    rt_assert(str_cpy_test("a"));
    rt_assert(str_cpy_test("ab"));
    rt_assert(str_cpy_test("ablaksjdflaksjdflkajsdfljaslfdj"));

    /* test str_ncpy */
    rt_assert(str_ncpy_test("", 0));
    rt_assert(str_ncpy_test("", 1));
    rt_assert(str_ncpy_test("", STRMAX));
    rt_assert(str_ncpy_test("a", 0));
    rt_assert(str_ncpy_test("a", strlen("a")));
    rt_assert(str_ncpy_test("a", strlen("a") + 1));
    rt_assert(str_ncpy_test("a", strlen("a") - 1));
    rt_assert(str_ncpy_test("a", STRMAX));
    rt_assert(str_ncpy_test("ab", 0));
    rt_assert(str_ncpy_test("ab", strlen("ab")));
    rt_assert(str_ncpy_test("ab", strlen("ab") + 1));
    rt_assert(str_ncpy_test("ab", strlen("ab") - 1));
    rt_assert(str_ncpy_test("ab", STRMAX));
    rt_assert(str_ncpy_test("ablaksjdflaksjdflkajsdfljaslfdj", 
      strlen("ablaksjdflaksjdflkajsdfljaslfdj")));
    rt_assert(str_ncpy_test("ablaksjdflaksjdflkajsdfljaslfdj", 
      strlen("ablaksjdflaksjdflkajsdfljaslfdj") + 1));
    rt_assert(str_ncpy_test("ablaksjdflaksjdflkajsdfljaslfdj", 
      strlen("ablaksjdflaksjdflkajsdfljaslfdj") - 1));
    rt_assert(str_ncpy_test("ablaksjdflaksjdflkajsdfljaslfdj", STRMAX));

    /* test str_lcpy */
    rt_assert(str_lcpy_test("", 0));
    rt_assert(str_lcpy_test("", 1));
    rt_assert(str_lcpy_test("", STRMAX));
    rt_assert(str_lcpy_test("a", 0));
    rt_assert(str_lcpy_test("a", STRMAX));
    rt_assert(str_lcpy_test("a", strlen("a")));
    rt_assert(str_lcpy_test("a", strlen("a") + 1));
    rt_assert(str_lcpy_test("a", strlen("a") + 2));
    rt_assert(str_lcpy_test("a", strlen("a") - 1));
    rt_assert(str_lcpy_test("ab", 0));
    rt_assert(str_lcpy_test("ab", STRMAX));
    rt_assert(str_lcpy_test("ab", strlen("ab")));
    rt_assert(str_lcpy_test("ab", strlen("ab") + 1));
    rt_assert(str_lcpy_test("ab", strlen("ab") + 2));
    rt_assert(str_lcpy_test("ab", strlen("ab") - 1));
    rt_assert(str_lcpy_test("ablaksjdflaksjdflkajsdfljaslfdj", 
      strlen("ablaksjdflaksjdflkajsdfljaslfdj")));
    rt_assert(str_lcpy_test("ablaksjdflaksjdflkajsdfljaslfdj", 
      strlen("ablaksjdflaksjdflkajsdfljaslfdj") + 1));
    rt_assert(str_lcpy_test("ablaksjdflaksjdflkajsdfljaslfdj", 
      strlen("ablaksjdflaksjdflkajsdfljaslfdj") + 2));
    rt_assert(str_lcpy_test("ablaksjdflaksjdflkajsdfljaslfdj", 
      strlen("ablaksjdflaksjdflkajsdfljaslfdj") - 1));
    rt_assert(str_lcpy_test("ablaksjdflaksjdflkajsdfljaslfdj", STRMAX));

    /* test str_cat */
    rt_assert(str_cat_test("", ""));
    rt_assert(str_cat_test("a", ""));
    rt_assert(str_cat_test("", "a"));
    rt_assert(str_cat_test("aab", "aa"));
    rt_assert(str_cat_test("aabalskjf", "aaalskjdf"));

    /* test str_ncat */
    rt_assert(str_ncat_test("", "", 0));
    rt_assert(str_ncat_test("", "", 1));
    rt_assert(str_ncat_test("", "", STRMAX));
    rt_assert(str_ncat_test("a", "", strlen("a")));
    rt_assert(str_ncat_test("a", "", strlen("a") + 1));
    rt_assert(str_ncat_test("a", "", strlen("a") - 1));
    rt_assert(str_ncat_test("a", "", 0));
    rt_assert(str_ncat_test("a", "", STRMAX));
    rt_assert(str_ncat_test("", "a", strlen("a")));
    rt_assert(str_ncat_test("", "a", strlen("a") + 1));
    rt_assert(str_ncat_test("", "a", strlen("a") - 1));
    rt_assert(str_ncat_test("", "a", 0));
    rt_assert(str_ncat_test("", "a", STRMAX));
    rt_assert(str_ncat_test("aab", "aa", strlen("aab")));
    rt_assert(str_ncat_test("aab", "aa", strlen("aab") + 1));
    rt_assert(str_ncat_test("aab", "aa", strlen("aab") - 1));
    rt_assert(str_ncat_test("aab", "aa", 0));
    rt_assert(str_ncat_test("aab", "aa", STRMAX));
    rt_assert(str_ncat_test("aab", "aa", strlen("aab") + strlen("aa")));
    rt_assert(str_ncat_test("aab", "aa", strlen("aab") + strlen("aa") + 1));
    rt_assert(str_ncat_test("aab", "aa", strlen("aab") + strlen("aa") - 1));
    rt_assert(str_ncat_test("aabalskjf", "aaalskjdf", 
      strlen("aabalskjf") + strlen("aaalskjdf")));
    rt_assert(str_ncat_test("aabalskjf", "aaalskjdf", 
      strlen("aabalskjf") + strlen("aaalskjdf") - 1));
    rt_assert(str_ncat_test("aabalskjf", "aaalskjdf", 
      strlen("aabalskjf") + strlen("aaalskjdf") + 1));
    rt_assert(str_ncat_test("aabalskjf", "aaalskjdf", 0));
    rt_assert(str_ncat_test("aabalskjf", "aaalskjdf", 1));
    rt_assert(str_ncat_test("aabalskjf", "aaalskjdf", STRMAX));
    rt_assert(str_ncat_test("aabalskjf", "aaalskjdf", strlen("aabalskjf")));
    rt_assert(str_ncat_test("aabalskjf", "aaalskjdf", strlen("aabalskjf") + 1));
    rt_assert(str_ncat_test("aabalskjf", "aaalskjdf", strlen("aabalskjf") - 1));
    rt_assert(str_ncat_test("aabalskjf", "aaalskjdf", strlen("aabalskjf") / 2));
    rt_assert(str_ncat_test("aabalskjf", "aaalskjdf", strlen("aabalskjf") * 2));

    /* test str_lcat */
    rt_assert(str_lcat_test("", "", 0));
    rt_assert(str_lcat_test("", "", 1));
    rt_assert(str_lcat_test("", "", STRMAX));
    rt_assert(str_lcat_test("a", "", strlen("a")));
    rt_assert(str_lcat_test("a", "", strlen("a") + 1));
    rt_assert(str_lcat_test("a", "", strlen("a") - 1));
    rt_assert(str_lcat_test("a", "", 0));
    rt_assert(str_lcat_test("a", "", STRMAX));
    rt_assert(str_lcat_test("", "a", strlen("a")));
    rt_assert(str_lcat_test("", "a", strlen("a") + 1));
    rt_assert(str_lcat_test("", "a", strlen("a") - 1));
    rt_assert(str_lcat_test("", "a", 0));
    rt_assert(str_lcat_test("", "a", STRMAX));
    rt_assert(str_lcat_test("aab", "aa", strlen("aab")));
    rt_assert(str_lcat_test("aab", "aa", strlen("aab") + 1));
    rt_assert(str_lcat_test("aab", "aa", strlen("aab") - 1));
    rt_assert(str_lcat_test("aab", "aa", 0));
    rt_assert(str_lcat_test("aab", "aa", STRMAX));
    rt_assert(str_lcat_test("aab", "aa", strlen("aab") + strlen("aa")));
    rt_assert(str_lcat_test("aab", "aa", strlen("aab") + strlen("aa") + 1));
    rt_assert(str_lcat_test("aab", "aa", strlen("aab") + strlen("aa") - 1));
    rt_assert(str_lcat_test("aabalskjf", "aaalskjdf", 
      strlen("aabalskjf") + strlen("aaalskjdf")));
    rt_assert(str_lcat_test("aabalskjf", "aaalskjdf", 
      strlen("aabalskjf") + strlen("aaalskjdf") - 1));
    rt_assert(str_lcat_test("aabalskjf", "aaalskjdf", 
      strlen("aabalskjf") + strlen("aaalskjdf") + 1));
    rt_assert(str_lcat_test("aabalskjf", "aaalskjdf", 0));
    rt_assert(str_lcat_test("aabalskjf", "aaalskjdf", 1));
    rt_assert(str_lcat_test("aabalskjf", "aaalskjdf", STRMAX));
    rt_assert(str_lcat_test("aabalskjf", "aaalskjdf", strlen("aabalskjf")));
    rt_assert(str_lcat_test("aabalskjf", "aaalskjdf", strlen("aabalskjf") + 1));
    rt_assert(str_lcat_test("aabalskjf", "aaalskjdf", strlen("aabalskjf") - 1));
    rt_assert(str_lcat_test("aabalskjf", "aaalskjdf", strlen("aabalskjf") / 2));
    rt_assert(str_lcat_test("aabalskjf", "aaalskjdf", strlen("aabalskjf") * 2));

    /* test ltrim */
    rt_assert(str_ltrim_test(""));
    rt_assert(str_ltrim_test("a"));
    rt_assert(str_ltrim_test("llaskfjl"));
    rt_assert(str_ltrim_test("a "));
    rt_assert(str_ltrim_test("a \n\t"));
    rt_assert(str_ltrim_test("a \n\ta"));
    rt_assert(str_ltrim_test("a \n\t\f\v\r"));
    rt_assert(str_ltrim_test(" \n\t\f\va"));
    rt_assert(str_ltrim_test(" \n\t\f\v\ra \n\t\f\v\r"));
    rt_assert(str_ltrim_test(" \n\t\f\v\r"));
    rt_assert(str_ltrim_test(" \n\t\f\v\r\0\n   "));
    rt_assert(str_ltrim_test(" \n\t\alskfj f\v\r"));

    /* test rtrim */
    rt_assert(str_rtrim_test(""));
    rt_assert(str_rtrim_test("a"));
    rt_assert(str_rtrim_test("llaskfjl"));
    rt_assert(str_rtrim_test("a "));
    rt_assert(str_rtrim_test(" a "));
    rt_assert(str_rtrim_test("a\n\t\r\f\v"));
    rt_assert(str_rtrim_test("a\n\t\r\f\vasldjf"));
    rt_assert(str_rtrim_test("a\n\t\r\f\vasldjf   "));
    rt_assert(str_rtrim_test("a \n\t\f\v"));
    rt_assert(str_rtrim_test(" \n\t\f\va"));
    rt_assert(str_rtrim_test(" \n\t\f\v\ra \n\t\f\v\r"));
    rt_assert(str_rtrim_test(" \n\t\f\v\r"));
    rt_assert(str_rtrim_test(" \n\t\f\v\r\0\n   "));
    rt_assert(str_rtrim_test(" \n\t\alskfj f\v\r"));

    /* test chr */
    rt_assert(str_chr_test("", 'a'));
    rt_assert(str_chr_test("", 'b'));
    rt_assert(str_chr_test("", '\0'));
    rt_assert(str_chr_test("ael;jas f,nerkj", 'a'));
    rt_assert(str_chr_test("ael;jas f,nerkj", 'e'));
    rt_assert(str_chr_test("ael;jas f,nerkj", 'l'));
    rt_assert(str_chr_test("ael;jas f,nerkj", ';'));
    rt_assert(str_chr_test("ael;jas f,nerkj", 'k'));
    rt_assert(str_chr_test("ael;jas f,nerkj", 'j'));
    rt_assert(str_chr_test("ael;jas f,nerkj", '\0'));

    /* test rchr */
    rt_assert(str_rchr_test("", 'a'));
    rt_assert(str_rchr_test("", '\0'));
    rt_assert(str_chr_test("ael;jas jf,nerkj", 'a'));
    rt_assert(str_chr_test("ael;jas jf,nerkj", 'e'));
    rt_assert(str_chr_test("ael;jas jf,nerkj", 'l'));
    rt_assert(str_chr_test("ael;jas jf,nerkj", ';'));
    rt_assert(str_chr_test("ael;jas jf,nerkj", 'j'));
    rt_assert(str_chr_test("ael;jas jf,nerkj", 'k'));
    rt_assert(str_chr_test("ael;jas jf,nerkj", '\0'));

    /* test split (as best we can) */
    rt_assert(str_split_test("", ""));
    rt_assert(str_split_test("a", ""));
    rt_assert(str_split_test(" a ", ""));
    rt_assert(str_split_test("", "asdflasdkfj"));
    rt_assert(str_split_test(" ", " \v\t"));
    rt_assert(str_split_test("\v", " \v\t"));
    rt_assert(str_split_test(" \v", " \v\t"));
    rt_assert(str_split_test(" a ", " \v\t"));
    rt_assert(str_split_test(" a", " \v\t"));
    rt_assert(str_split_test("a ", " \v\t"));
    rt_assert(str_split_test("a b", " \v\t"));
    rt_assert(str_split_test("a b ", " \v\t"));
    rt_assert(str_split_test(" a b", " \v\t"));
    rt_assert(str_split_test(" aa bbbb", " \v\t"));
    rt_assert(str_split_test("aa bbb", " \v\t"));
    rt_assert(str_split_test("aa bbb ", " \v\t"));
    rt_assert(str_split_test(" aa bbb ", " \v\t"));
    rt_assert(str_split_test("the ants go marching", " \v\t"));
    rt_assert(str_split_test("\tthe ants go marching\t ", " \v\t"));
    rt_assert(str_split_test("the ants go marching\t\v ", " \v\t"));

    rt_assert(str_dirname_test("", "."));
    rt_assert(str_dirname_test("/blah", "/"));
    rt_assert(str_dirname_test("/blah/", "/blah/"));
    rt_assert(str_dirname_test("/blah/foo", "/blah/"));
    rt_assert(str_dirname_test("   ", "."));
    rt_assert(str_dirname_test(".", "."));
    rt_assert(str_dirname_test("./", "./"));
    rt_assert(str_dirname_test("..", "."));
    rt_assert(str_dirname_test("../", "../"));

    rt_assert(str_basename_test("", ""));
    rt_assert(str_basename_test(".", "."));
    rt_assert(str_basename_test("./", "./"));
    rt_assert(str_basename_test("..", ".."));
    rt_assert(str_basename_test("../", "../"));
    rt_assert(str_basename_test("/", "/"));
    rt_assert(str_basename_test("///////", "///////"));
    rt_assert(str_basename_test("tmp///////", "tmp///////"));
    rt_assert(str_basename_test("/tmp///////", "tmp///////"));
    rt_assert(str_basename_test("//tmp///////", "tmp///////"));
    rt_assert(str_basename_test("/blah", "blah"));
    rt_assert(str_basename_test("/blah/", "blah/"));
    rt_assert(str_basename_test("/blah/foo", "foo"));
    return 1;
}

