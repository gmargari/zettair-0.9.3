/* diff.c implements a tool to compare two index files produced by
 * the search engine
 *
 * written nml 2003-05-06
 *
 */

#include "firstinclude.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "def.h"
#include "docmap.h"
#include "str.h"
#include "vec.h"
#include "index.h"
#include "_index.h"
#include "iobtree.h"
#include "vocab.h"
#include "bucket.h"
#include "fdset.h"

/* internal function to load a list into a buffer, pointing ptr vec at it.
 * Returns 1 on success, 0 on error, < 0 if no futher vectors could be loaded */
static int getvec(struct index *idx, struct vec *v, void **buf, 
  unsigned int *bufsize, struct vec *ptr, const char *term, 
  unsigned int termlen) {
    unsigned int readsize;
    struct vocab_vector vv;
    int fd;
    int ret;

    if ((ret = vocab_decode(&vv, v)) == VOCAB_OK) {
        readsize = vv.size;

        switch (vv.location) {
        case VOCAB_LOCATION_VOCAB:
            ptr->pos = vv.loc.vocab.vec;
            ptr->end = ptr->pos + vv.size;
            return 1;

        case VOCAB_LOCATION_FILE:
            /* ensure we have enough space */
            if (*bufsize < readsize) {
                void *ptr;

                if ((ptr = realloc(*buf, readsize))) {
                    *buf = ptr;
                    *bufsize = readsize;
                } else {
                    fprintf(stderr, "getvocab: failed to get memory\n");
                    return 0;
                }
            }

            if (((fd = fdset_pin(idx->fd, idx->index_type, vv.loc.file.fileno, 
                vv.loc.file.offset, SEEK_SET)) >= 0) 
              && (read(fd, *buf, readsize) == (ssize_t) readsize)
              && fdset_unpin(idx->fd, idx->index_type, vv.loc.file.fileno, fd) 
                == FDSET_OK) {

                /* succeeded */
                ptr->pos = *buf;
                ptr->end = ptr->pos + vv.size;
                return 1;
            }
            break;
        }
    } else if (ret == VOCAB_END) {
        return -1;
    }
    fprintf(stderr, "getvocab: failed to decode vocab\n");
    return 0;
}

/* internal function to compare the contents of two vocab entries.  Returns 0 
 * if the vocab entries are different, > 0 if they're identical and < 0 if an
 * error occurred. */
static int vvdiff(struct index *one, struct index *two, struct vec *vone, 
  struct vec *vtwo, FILE *output, const char *term, unsigned int termlen, 
  unsigned int *discon_one, unsigned int *discon_two, 
  
  void **buf1, unsigned int *bufsize1, void **buf2, unsigned int *bufsize2) {
    struct vec list1,
               list2;
    unsigned int docno1,
                 docno2,
                 wordno1 = 0,
                 wordno2 = 0,
                 docs1,
                 docs2,
                 occurs1,
                 occurs2,
                 o1,
                 o2,
                 i,
                 bytes,
                 entries1,
                 entries2;
    unsigned long int last1,
                      last2,
                      f_dt1 = 0,
                      f_dt2 = 0,
                      delta;
    struct vocab_vector vv1,
                        vv2;
    int ret;

    /* examine first vocab entry */
    last1 = docs1 = occurs1 = 0;
    list1 = *vone;
    entries1 = 0;
    while ((ret = vocab_decode(&vv1, &list1)) == VOCAB_OK) {
        entries1++;

        if ((vv1.type == VOCAB_VTYPE_DOCWP) || (vv1.type == VOCAB_VTYPE_DOC)) {
            docs1 += vv1.header.doc.docs;
            occurs1 += vv1.header.doc.occurs;
            if (last1 < vv1.header.doc.last) {
                last1 = vv1.header.doc.last;
            }
        }
    }
    if (ret != VOCAB_END) {
        assert(!CRASH);
        fprintf(stderr, "failed to decode vector one\n");
        return -1;
    }

    /* examine second vocab entry */
    last2 = docs2 = occurs2 = 0;
    list2 = *vtwo;
    entries2 = 0;
    while ((ret = vocab_decode(&vv2, &list2)) == VOCAB_OK) {
        entries2++;
        if ((vv2.type == VOCAB_VTYPE_DOCWP) || (vv2.type == VOCAB_VTYPE_DOC)) {
            docs2 += vv2.header.doc.docs;
            occurs2 += vv2.header.doc.occurs;
            if (last2 < vv2.header.doc.last) {
                last2 = vv2.header.doc.last;
            }
        }
    }
    if (ret != VOCAB_END) {
        assert(!CRASH);
        fprintf(stderr, "failed to decode vector one\n");
        return -1;
    }

    ret = 1;
    if (docs1 != docs2) {
        fprintf(output, "number of documents for '");
        for (i = 0; i < termlen; i++) {
            putc(term[i], output);
        }
        fprintf(output, "' differ (%u vs %u)\n", docs1, docs2);
        ret = 0;
    } 
    o1 = occurs1;
    o2 = occurs2;
    if (occurs1 != occurs2) {
        fprintf(output, "number of occurrances for '");
        for (i = 0; i < termlen; i++) {
            putc(term[i], output);
        }
        fprintf(output, "' differ (%u vs %u)\n", occurs1, occurs2);
        ret = 0;
    } 
    if (last1 != last2) {
        fprintf(output, "last document number for '");
        for (i = 0; i < termlen; i++) {
            putc(term[i], output);
        }
        fprintf(output, "' differs (%lu vs %lu)\n", last1, last2);
        ret = 0;
    } 

    /* return if we've detected differences here */
    if (!ret) {
        fprintf(output, "(term diff stops...)\n");
        return ret;
    } 
    assert(ret == 1);

    list1.pos = list2.pos = list1.end = list2.end = NULL;
    
    /* shortcut list diff for simple entries */
    if (entries1 == 1 && entries2 == 1) {
        if (getvec(one, vone, buf1, bufsize1, &list1, term, termlen) > 0
          && getvec(two, vtwo, buf2, bufsize2, &list2, term, termlen) > 0) {
            if ((VEC_LEN(&list1) == VEC_LEN(&list2)) 
              && !memcmp(list1.pos, list2.pos, VEC_LEN(&list1))) {
                assert(ret == 1);
                return 1;
            } else {
                fprintf(output, "vectors for '");
                for (i = 0; i < termlen; i++) {
                    putc(term[i], output);
                }
                fprintf(output, "' differ\n");
                return 0;
            }
        } else {
            assert(!CRASH);
            fprintf(stderr, "failed to get a vector\n");
            return -1;
        }
    } else {
        if (!entries1) {
            fprintf(stderr, "no entries in index one for '");
            for (i = 0; i < termlen; i++) {
                putc(term[i], output);
            }
            fprintf(output, "'");
            return -1;
        } else if (entries1 > 1) {
            (*discon_one)++;
        } 

        if (!entries2) {
            fprintf(stderr, "no entries in index two for '");
            for (i = 0; i < termlen; i++) {
                putc(term[i], output);
            }
            fprintf(output, "'");
        } else if (entries2 > 1) {
            (*discon_two)++;
        }
    }

    /* decode the two sets of vectors to ensure that they have the same 
     * content */
    docno1 = docno2 = -1;
    while (occurs1) {
        /* get next occurrance from one */
        if (!f_dt1--) {
            while (!vec_vbyte_read(&list1, &delta)) {
                int getvecret;
                /* need to read next list location and load it */
                if ((getvecret 
                  = getvec(one, vone, buf1, bufsize1, &list1, term, 
                    termlen)) > 0) {

                    docno1 = -1;
                } else if (getvecret < 0) {
                    fprintf(output, "index one lacks occurrances for '");
                    for (i = 0; i < termlen; i++) {
                        putc(term[i], output);
                    }
                    fprintf(output, "' (%u vs %u)\n", o1, o2);
                    return 0;
                } else {
                    assert(!CRASH);
                    fprintf(stderr, "failed to get next vector one\n");
                    return -1;
                }
            }
            bytes = vec_vbyte_read(&list1, &f_dt1); 
            if (!bytes) {
                assert(!CRASH);
                fprintf(stderr, "failed to read from vector one\n");
                return -1;
            }
            f_dt1--;
            docno1 += delta + 1;
            wordno1 = -1;
        }
        bytes = vec_vbyte_read(&list1, &delta); assert(bytes);
        if (!bytes) {
            assert(!CRASH);
            fprintf(stderr, "failed to read occ from vector one\n");
            return -1;
        }
        wordno1 += delta + 1;
        occurs1--;

        /* get next occurrance from two */
        if (!f_dt2--) {
            while (!vec_vbyte_read(&list2, &delta)) {
                int getvecret;
                /* need to read next list location and load it */
                if (!(getvecret 
                  = getvec(two, vtwo, buf2, bufsize2, &list2, term, 
                    termlen))) {

                    assert(!CRASH);
                    fprintf(stderr, "failed to get next vector two\n");
                    return -1;
                } else if (getvecret < 0) {
                    fprintf(output, "index two lacks occurrances for '");
                    for (i = 0; i < termlen; i++) {
                        putc(term[i], output);
                    }
                    fprintf(output, "' (%u vs %u)\n", o1, o2);
                    return 0;
                }
                docno2 = -1;
            }
            bytes = vec_vbyte_read(&list2, &f_dt2); assert(bytes);
            if (!bytes) {
                assert(!CRASH);
                fprintf(stderr, "failed to read from vector two\n");
                return -1;
            }
            f_dt2--;
            docno2 += delta + 1;
            wordno2 = -1;
        }
        bytes = vec_vbyte_read(&list2, &delta); assert(bytes);
        if (!bytes) {
            assert(!CRASH);
            fprintf(stderr, "failed to read occ from vector two\n");
            return -1;
        }
        wordno2 += delta + 1;
        occurs2--;

        if (docno1 == docno2 && f_dt1 == f_dt2 && wordno1 == wordno2) {
            /* occurrance is the same */
        } else if (docno1 != docno2) {
            fprintf(output, "docno's %u and %u differ in '", docno1, docno2);
            for (i = 0; i < termlen; i++) {
                putc(term[i], output);
            }
            fprintf(output, "'\n");
            return 0;
        } else if (f_dt1 != f_dt2) {
            fprintf(output, "f_dt's %lu and %lu differ in '", f_dt1, f_dt2);
            for (i = 0; i < termlen; i++) {
                putc(term[i], output);
            }
            fprintf(output, "'\n");
            return 0;
        } else {
            fprintf(output, "wordno's %u and %u differ in '", wordno1, wordno2);
            for (i = 0; i < termlen; i++) {
                putc(term[i], output);
            }
            fprintf(output, "'\n");
            return 0;
        }
    }

    assert(ret == 1);
    return 1;
}

int diff(struct index *one, struct index *two, FILE *output) {
    const char *term1,
               *term2;
    unsigned int termlen1,
                 termlen2,
                 discon1 = 0, 
                 discon2 = 0,
                 bufsize1 = 0,
                 bufsize2 = 0,
                 len1,
                 len2,
                 i,
                 size;
    int ret = 0;
    unsigned int state1[3] = {0, 0, 0},
                 state2[3] = {0, 0, 0};
    struct vec v1,
               v2;
    void *buf1 = NULL,
         *buf2 = NULL;

    size = docmap_entries(one->map);
    if (docmap_entries(two->map) < size) {
        size = docmap_entries(two->map);
    }

    /* check the document mappings */
    for (i = 0; i < size; i++) {
        unsigned int sourcefile1,
                     sourcefile2,
                     bytes1,
                     bytes2,
                     dwords1,
                     dwords2,
                     aux_len1,
                     aux_len2,
                     words1,
                     words2;
        off_t offset1,
              offset2;
        enum docmap_flag flags1,
                          flags2;
        enum mime_types mtype1,
                        mtype2;
        double weight1,
               weight2;
        char aux1[4096], aux2[4096];

        if (docmap_get_location(one->map, i, &sourcefile1, &offset1, 
              &bytes1, &mtype1, &flags1) != DOCMAP_OK
          || docmap_get_location(two->map, i, &sourcefile2, &offset2, 
              &bytes2, &mtype2, &flags2) != DOCMAP_OK
          || docmap_get_trecno(one->map, i, aux1, sizeof(aux1) - 1, 
              &aux_len1) != DOCMAP_OK
          || docmap_get_trecno(two->map, i, aux2, sizeof(aux2) - 1,
              &aux_len2) != DOCMAP_OK
          || docmap_get_words(one->map, i, &words1) != DOCMAP_OK
          || docmap_get_words(two->map, i, &words2) != DOCMAP_OK
          || docmap_get_distinct_words(one->map, i, &dwords1) != DOCMAP_OK
          || docmap_get_distinct_words(two->map, i, &dwords2) != DOCMAP_OK
          || docmap_get_weight(one->map, i, &weight1) != DOCMAP_OK
          || docmap_get_weight(two->map, i, &weight2) != DOCMAP_OK)
            return 0;

        aux1[aux_len1] = '\0';
        aux2[aux_len2] = '\0';

        /* sourcefile and offset are allowed to be different, but the rest must
         * be the same */
        if (bytes1 != bytes2) {
            fprintf(output, "docno %u contains different number of bytes "
              "(%u and %u) in indexes\n", i, bytes1, bytes2);
            ret = 1;
        }
        if (mtype1 != mtype2) {
            fprintf(output, "docno %u is of different types "
              "(%s and %s) in indexes\n", i, mime_string(mtype1),
              mime_string(mtype2));
            ret = 1;
        }
        if (dwords1 != dwords2) {
            fprintf(output, "docno %u contains different number of distinct "
              "words (%u and %u) in indexes\n", i, dwords1, dwords2);
            ret = 1;
        }
        if (words1 != words2) {
            fprintf(output, "docno %u contains different number of words "
              "(%u and %u) in indexes\n", i, words1, words2);
            ret = 1;
        }
        if (((aux1 || aux2) && !(aux1 && aux2))  /* logical xor */
          || (aux1 && (str_cmp(aux1, aux2)))) {
            fprintf(output, "docno %u contains different auxilliary strings "
              "('%s' and '%s') in indexes\n", i, 
              aux1 ? (const char *) aux1 : "(null)", 
              aux2 ? (const char *) aux2 : "(null)");
            ret = 1;
        }
        if ((weight1 != weight2) 
          && ((weight1 > (weight2 * 1.05)) || (weight1 < (weight2 * 0.95)))) {
            fprintf(output, "docno %u contains different weights "
              "(%f and %f) in indexes\n", i, weight1, weight2);
            ret = 1;
        }
    }
    /* check number of entries match */
    if (docmap_entries(one->map) != docmap_entries(two->map)) {
        fprintf(output, "indexes have different number of documents "
          "(%lu and %lu)\n", docmap_entries(one->map), 
          docmap_entries(two->map));
        ret = 1;
    }
    fflush(output);

    /* check the inverted lists */
    while ((term1 
        = iobtree_next_term(one->vocab, state1, &termlen1, 
            (void **) &v1.pos, &len1))
      && (term2
        = iobtree_next_term(two->vocab, state2, &termlen2, 
            (void **) &v2.pos, &len2))) {
        int vvret;

        v1.end = v1.pos + len1;
        v2.end = v2.pos + len2;

        if (!str_nncmp(term1, termlen1, term2, termlen2)) {
            if ((vvret = vvdiff(one, two, &v1, &v2, output, term1, termlen1, 
                  &discon1, &discon2, &buf1, &bufsize1, &buf2, &bufsize2)) 
                > 0) {

                /* compared identical */
            } else if (vvret == 0) {
                /* differed */
                ret = 1;
            } else {
                /* error */
                assert(!CRASH);
                fprintf(stderr, "error while comparing vocab entries for '");
                for (i = 0; i < termlen1; i++) {
                    putc(term1[i], stderr);
                }
                fprintf(stderr, "'\n");
                return 1;
            }
        } else if (str_nncmp(term1, termlen1, term2, termlen2) < 0) {
            fprintf(output, "index two lacks term '");
            for (i = 0; i < termlen1; i++) {
                putc(term1[i], output);
            }
            printf("' (diff stops...)\n");
            return 1;
        } else {
            fprintf(output, "index one lacks term '");
            for (i = 0; i < termlen2; i++) {
                putc(term2[i], output);
            }
            printf("' (diff stops...)\n");
            return 1;
        }
    }

    if (discon1 || discon2) {
        printf("informational: %u vs %u discontiguous vectors\n", discon1, 
          discon2);
    }

    if (buf1) {
        free(buf1);
    }
    if (buf2) {
        free(buf2);
    }

    return ret;
}

int main(int argc, char **argv) {
    struct index *one,
                 *two;

    if (argc != 3) {
        fprintf(stderr, "usage: %s file1 file2\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* open first index */
    if (!(one = index_load(argv[1], 0, INDEX_LOAD_NOOPT, NULL))) {
        fprintf(stderr, "couldn't open %s: %s\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    /* open second index */
    } else if (!(two = index_load(argv[2], 0, INDEX_LOAD_NOOPT, NULL))) {
        index_delete(one);
        fprintf(stderr, "couldn't open %s: %s\n", argv[2], strerror(errno));
        return EXIT_FAILURE;
    }

    diff(one, two, stdout);

    index_delete(one);
    index_delete(two);
    return EXIT_SUCCESS;
}

