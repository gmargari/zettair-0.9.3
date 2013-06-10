/* hashtime.c is a small program to time construction and searching of
 * a chash string hashtable from a file.  Input is two files with
 * line-seperated strings (no other whitespace), the first of which will be
 * used for construction, the second which will be used for searching.
 *
 * written nml 2005-04-20
 *
 */

#include "chash.h"
#include "str.h"
#include "bit.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

void replace_endl(char *str, unsigned int len) {
    unsigned int i;

    for (i = 0; i < len; i++) {
        if (str[i] == '\n') {
            str[i] = '\0';
        }
    }
}

int main(int argc, char **argv) {
    char *fstr,
         *pos,
         *end;
    struct timeval now,
                   then;
    FILE *construct,
         *search;
    unsigned int construct_len,
                 search_len,
                 len,
                 numfound,
                 total,
                 slots,
                 bits;
    struct chash *hash;
    int found;
    void **dummy;
    unsigned long int micros;

    if (argc != 4) {
        fprintf(stderr, "usage: %s constructfile searchfile\n", *argv);
        return EXIT_FAILURE;
    }

    bits = bit_log2(atoi(argv[3])) + 1;
    slots = bit_pow2(bits);

    if (!(construct = fopen(argv[1], "rb"))) {
        fprintf(stderr, "failed to open file '%s': %s\n", argv[1], 
          strerror(errno));
        return EXIT_FAILURE;
    }

    if (!(search = fopen(argv[2], "rb"))) {
        fclose(construct);
        fprintf(stderr, "failed to open file '%s': %s\n", argv[2], 
          strerror(errno));
        return EXIT_FAILURE;
    }

    /* determine lengths of files */
    fseek(construct, 0, SEEK_END);
    fseek(search, 0, SEEK_END);
    if (((construct_len = ftell(construct)) == -1) 
      || (fseek(construct, 0, SEEK_SET) == -1)
      || ((search_len = ftell(search)) == -1) 
      || (fseek(search, 0, SEEK_SET) == -1)) {
        fclose(construct);
        fclose(search);
        fprintf(stderr, "failed to get lengths of files\n");
        return EXIT_SUCCESS;
    }
    printf("%u slots, %u bits\n", slots, bits);
    printf("construct: %s, %u bytes\nsearch: %s, %u bytes\n", argv[1], 
      construct_len, argv[2], search_len);

    /* get enough memory to hold either */
    if (!(fstr = malloc(construct_len > search_len 
        ? construct_len : search_len))) {
        fclose(construct);
        fclose(search);
        fprintf(stderr, "failed to get %u memory\n", 
          construct_len > search_len ? construct_len : search_len);
        return EXIT_SUCCESS;
    }

    /* read construction file */
    fread(fstr, construct_len, 1, construct);
    replace_endl(fstr, construct_len);

    /* construct a hashtable from it */

    /* timings start */
    gettimeofday(&then, NULL);
    hash = chash_str_new(bits, 100000.0, str_nhash);

    pos = fstr;
    total = 0;
    numfound = 0;
    end = fstr + construct_len;
    while ((pos < end) && (len = str_len(pos))) {
        chash_nstr_ptr_find_insert(hash, pos, len, &dummy, NULL, &found);
        pos += len + 1;
        numfound += found;
        total++;
    }

    gettimeofday(&now, NULL);
    printf("construction: %u strings used, %u unique inserted in ", total, 
      total - numfound);
    micros = now.tv_usec - then.tv_usec + (now.tv_sec - then.tv_sec) * 1000000;
    printf("%f seconds (%lu microseconds)\n", micros / 1000000.0, micros);

    /* now search using search file */
    fread(fstr, search_len, 1, search);
    replace_endl(fstr, search_len);

    /* timings start */
    gettimeofday(&then, NULL);

    pos = fstr;
    end = fstr + search_len;
    while ((pos < end) && (len = str_len(pos))) {
        chash_nstr_ptr_find(hash, pos, len, &dummy);
        pos += len + 1;
    }

    gettimeofday(&now, NULL);

    /* do it again to count how many we found */
    pos = fstr;
    total = 0;
    numfound = 0;
    while ((pos < end) && (len = str_len(pos))) {
        enum chash_ret ret = chash_nstr_ptr_find(hash, pos, len, &dummy);
        assert(ret == CHASH_OK || ret == CHASH_ENOENT);
        pos += len + 1;
        numfound += found;
        total++;
        ret = CHASH_OK; /* to stop compiler whining about unused variable */
    }

    gettimeofday(&now, NULL);
    printf("search: %u strings used, %u found in ", total, numfound);
    micros = now.tv_usec - then.tv_usec + (now.tv_sec - then.tv_sec) * 1000000;
    printf("%f seconds (%lu microseconds)\n", micros / 1000000.0, micros);

    chash_delete(hash);
    free(fstr);
    fclose(construct);
    fclose(search);

    return EXIT_SUCCESS;
}

