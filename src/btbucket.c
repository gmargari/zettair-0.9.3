/* btbucket.c implements a set of utility functions for manipulating
 * btree buckets
 *
 * written nml 2003-02-17
 *
 */

#include "firstinclude.h"

#include "btbucket.h"
#include "_btbucket.h"

#include "_mem.h"

#include "bit.h"
#include "bucket.h"
#include "str.h"

#include <limits.h>

void btbucket_new(void *btbucket, unsigned int bucketsize, 
  unsigned int sib_fileno, unsigned int sib_offset, int leaf, void *prefix, 
  unsigned int *prefix_size) {
    struct btbucket *b = btbucket;

    leaf = !!leaf;                           /* set leaf to 1 or 0 */

    /* prefix_size can't exceed a 7 bit number, truncate prefix it if it does */
    if (*prefix_size >= BIT_SET(0U, 7, 1)) {
        *prefix_size = BIT_SET(0U, 7, 1) - 1;
    }

    /* combine leaf indication and prefix size into one stored variable */
    b->u.s.prefixsize = BIT_SET(*prefix_size, 7, leaf);

    /* tailsize is the amount of space in the btree bucket not allocated to the
     * bucket itself.  If its a leaf then it has a sibling pointer ... */
    b->u.s.tailsize = leaf * (sizeof(sib_fileno) + sizeof(sib_offset))
      /* ... and all buckets have a prefix which takes space ... */
      + *prefix_size 
      /* ... and all buckets have two bytes overhead at the start ... */
      + (b->u.s.bucket - b->u.mem);

    /* copy in the prefix */
    memcpy(&b->u.s.bucket[BTBUCKET_SIZE(btbucket, bucketsize)], prefix, 
      *prefix_size);

    /* copy sibling pointer in */
    if (leaf) {
        btbucket_set_sibling(btbucket, bucketsize, sib_fileno, sib_offset);
    }
}

void btbucket_sibling(void *btbucket, unsigned int bucketsize, 
  unsigned int *fileno, unsigned long int *offset) {
    struct btbucket *b = btbucket;

    assert(BIT_GET(b->u.s.prefixsize, 7));  /* has to be a leaf */

    MEM_NTOH(fileno, &b->u.mem[bucketsize - sizeof(*fileno) - sizeof(*offset)], 
      sizeof(*fileno));
    MEM_NTOH(offset, &b->u.mem[bucketsize - sizeof(*offset)], sizeof(*offset));
}

void btbucket_set_sibling(void *btbucket, unsigned int bucketsize, 
  unsigned int fileno, unsigned long int offset) {
    struct btbucket *b = btbucket;

    assert(BIT_GET(b->u.s.prefixsize, 7));  /* has to be a leaf */

    MEM_HTON(&b->u.mem[bucketsize - sizeof(fileno) - sizeof(offset)], &fileno,
      sizeof(fileno));
    MEM_HTON(&b->u.mem[bucketsize - sizeof(offset)], &offset, sizeof(offset));
}

int btbucket_set_prefix(void *btbucket, unsigned int bucketsize, void *prefix, 
  unsigned int *prefix_len) {

    assert(0);
    /* FIXME */
    return 0;
}

unsigned int btbucket_size(void *btbucket, unsigned int bucketsize) {
    return BTBUCKET_SIZE(btbucket, bucketsize);
}

void *btbucket_bucket(void *btbucket) {
    return BTBUCKET_BUCKET(btbucket);
}

int btbucket_leaf(void *btbucket, unsigned int bucketsize) {
    return BTBUCKET_LEAF(btbucket, bucketsize);
}

void *btbucket_prefix(void *btbucket, unsigned int bucketsize, 
  unsigned int *prefix_len) {
    return BTBUCKET_PREFIX(btbucket, bucketsize, prefix_len); 
}

unsigned int btbucket_entry_size() {
    return BTBUCKET_ENTRY_SIZE();
}

void btbucket_entry(void *entry, unsigned int *fileno, 
  unsigned long int *offset) {
    BTBUCKET_ENTRY(entry, fileno, offset);
}

void btbucket_set_entry(void *entry, unsigned int fileno, 
  unsigned long int offset) {
    BTBUCKET_SET_ENTRY(entry, fileno, offset);
}

unsigned int btbucket_common_prefix(const char *one, 
  unsigned int onelen, const char *two, unsigned int twolen, 
  char *lastchar) {
    unsigned int i,
                 len = (onelen > twolen) ? twolen : onelen;

    /* string one has to be lexographically smaller than two */
    assert(str_ncmp(one, two, len) <= 0);

    /* note: arr[index - 1 + !index] idiom prevents us from selecting array 
     * offset -1 when index is 0 (selects 0 instead, which may be 
     * not-quite-right but is much safer) */

    /* check all but last character */
    for (i = 0; i + 1 < len; i++) {
        if (one[i] != two[i]) {
            assert(one[i] < two[i]);
            *lastchar = one[i - 1 + !i];
            return i;
        }
    }

    /* check last char (can do one char better if last chars are off by one) */
    if (one[i] != two[i]) {
        assert(one[i] < two[i]);
        if ((one[i] == two[i] - 1) && (len == twolen)) {
            *lastchar = two[i] - 1;
            return i + 1;
        } else {
            *lastchar = one[i - 1 + !i];
            return i;
        }
    }

    *lastchar = one[len - 1 + !len];
    return len;
}

unsigned int btbucket_split_term(const char *one, 
  unsigned int onelen, const char *two, unsigned int twolen, char *lastchar) {
    unsigned int i,
                 len = (onelen > twolen) ? twolen : onelen;

    for (i = 0; i < len; i++) {
        if (one[i] != two[i]) {
            /* pick a split point between the two, slightly right-biased */
            *lastchar = (one[i] + two[i] + 1) / 2;
            return i + 1;
        }
    }

    if (len < onelen) {
        *lastchar = (one[len] + 1) / 2;
        return len + 1;
    } else if (len < twolen) {
        *lastchar = (two[len] + 1) / 2;
        return len + 1;
    } else {
        /* strings are identical, we can't split them */
        assert((onelen != twolen) 
          || str_ncmp(one, two, onelen));
        return 0;
    }
}

/* function to find a stupid split term, which is the second term given */
unsigned int btbucket_split_term_default(const char *one, 
  unsigned int onelen, const char *two, unsigned int twolen, char *lastchar) {
    *lastchar = two[twolen - 1];
    return twolen;
}

/* helper function that prints the contents of a bucket to stdout */
int bucket_print(void *bucket, unsigned int bucketsize, int strategy);
int btbucket_print(void *btbucket, unsigned int bucketsize, int strategy) {
    return bucket_print(BTBUCKET_BUCKET(btbucket), 
      BTBUCKET_SIZE(btbucket, bucketsize), strategy);
}
 
#ifdef BTBUCKET_TEST

#include <stdio.h>
#include <stdlib.h>

void simsplit(const char *one, const char *two) {
    char buf[101];
    char buf1[101];
    char buf2[101];
    char buf3[101];
    char lastchar;
    unsigned int len,
                 len1,
                 len2,
                 len3;

    len3 = btbucket_common_prefix(one, str_len((char *) one), two, 
        str_len((char *) two), &lastchar);

    str_ncpy((char *) buf3, (char *) one, len3 - 1 + !len3);
    buf3[len3 - 1 + !len3] = lastchar;
    buf3[len3] = '\0';

    len = btbucket_split_term(one, str_len((char *) one), two, 
        str_len((char *) two), &lastchar);

    assert(len < 100);

    str_ncpy((char *) buf, (char *) two, len - 1 + !len);
    buf[len - 1 + !len] = lastchar;
    buf[len] = '\0';

    len1 = btbucket_common_prefix(one, str_len((char *) one), buf, 
        str_len((char *) buf), &lastchar);

    str_ncpy((char *) buf1, (char *) one, len1 - 1 + !len1);
    buf1[len1 - 1 + !len1] = lastchar;
    buf1[len1] = '\0';

    len2 = btbucket_common_prefix(buf, str_len((char *) buf), two, 
        str_len((char *) two), &lastchar);

    str_ncpy((char *) buf2, (char *) buf, len2 - 1 + !len2);
    buf2[len2 - 1 + !len2] = lastchar;
    buf2[len2] = '\0';

    printf("[(%s) '%s', '%s') -> [(%s) '%s', '%s')  [(%s) '%s', '%s')\n", buf3, 
      one + len3, two + len3, buf1, one + len1, buf + len1, buf2, buf + len2, 
      two + len2);
    return;
}

void writeprefix(const char *one, const char *two) {
    char buf[101];
    char lastchar;
    unsigned int len;

    len = btbucket_common_prefix(one, str_len((char *) one), two, 
        str_len((char *) two), &lastchar);

    if (len < 100) {
        str_ncpy((char *) buf, (char *) one, len - 1);
        buf[len - 1] = lastchar;
        buf[len] = '\0';
        printf("common prefix of strings [%s, %s) is '%s'\n", (char *) one, 
          (char *) two, buf);
    } else {
        printf("common prefix of strings [%s, %s) is %u chars long (too long "
          "for buf)\n", (char *) one, (char *) two, len);
    }
}

void writesplit(const char *one, const char *two) {
    char buf[101];
    char lastchar;
    unsigned int len;

    len = btbucket_split_term(one, str_len((char *) one), two, 
        str_len((char *) two), &lastchar);

    if (len < 100) {
        str_ncpy((char *) buf, (char *) one, len - 1);
        buf[len - 1] = lastchar;
        buf[len] = '\0';
        printf("split term of strings [%s, %s) is '%s'\n", (char *) one, 
          (char *) two, buf);
    } else {
        printf("split term of strings [%s, %s) is %u chars long (too long "
          "for buf)\n", (char *) one, (char *) two, len);
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s string1 string2\n", *argv);
        return EXIT_FAILURE;
    } else {
        simsplit(argv[1], argv[2]);
        return EXIT_SUCCESS;
    }
}
#endif


