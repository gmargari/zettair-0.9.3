/* cat.c implements a tool to cat out an index fileset produced by
 * the search engine
 *
 * written nml 2003-02-25
 *
 */

#include "firstinclude.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "def.h"
#include "index.h"
#include "_index.h"
#include "docmap.h"
#include "getlongopt.h"
#include "iobtree.h"
#include "str.h"
#include "vec.h"
#include "vocab.h"
#include "fdset.h"
#include "bucket.h"

int cat_vocab(FILE *output, struct index *idx, int verbose) {
    unsigned int state[3] = {0, 0, 0};
    const char *term;
    void *addr;
    unsigned int i,
                 termlen;
    unsigned int terms = 0;
    struct vocab_vector vocab;
    struct vec v;
    enum vocab_ret ret;

    while ((term 
        = iobtree_next_term(idx->vocab, state, &termlen, &addr, &i))) {

        v.pos = addr;
        v.end = v.pos + i;

        /* decode the vocab stuff */
        while ((ret = vocab_decode(&vocab, &v)) == VOCAB_OK) {
            putc('\'', output);
            for (i = 0; i < termlen; i++) {
                putc(term[i], output);
            }
            putc('\'', output);
            putc(' ', output);

            if (verbose) {
                switch (vocab.location) {
                case VOCAB_LOCATION_VOCAB:
                    fprintf(output, "(location vocab) ");
                    break;

                default:
                    fprintf(output, "(location %u %lu %u) ", 
                      vocab.loc.file.fileno, 
                      vocab.loc.file.offset, vocab.loc.file.capacity);
                    break;
                };
            }
            fprintf(output, "docs %lu occurrances %lu length %lu last %lu\n", 
              vocab.header.doc.docs, vocab.header.doc.occurs, 
              vocab.size, vocab.header.doc.last);
        }

        if (ret != VOCAB_END) {
            fprintf(stderr, "failed to decode vocab entry\n");
            return 0;
        }
        terms++;
    }

    if (term || (terms != iobtree_size(idx->vocab))) {
        fprintf(stderr, "couldn't read all terms (%u vs %u)\n", terms, 
          iobtree_size(idx->vocab));
        return 0;
    }

    fprintf(output, "\n%u terms total\n", terms);

    return 1;
}

/* small function to output an off_t, avoiding (extremely) irritating warning
 * when using ll printf conversion and gcc */
static void off_out(FILE *output, off_t offset) {
    char buf[20]; /* note that a 64-bit number can only have 20 digits */
    unsigned int pos = 0;

    if (!offset) {
        putc('0', output);
        return;
    }

    if (offset < 0) {
        putc('-', output);
        offset *= -1;
    }

    while (offset > 0) {
        assert(pos <= 20);
        buf[pos++] = (offset % 10) + '0';
        offset /= 10;
    }

    while (pos--) {
        putc(buf[pos], output);
    }

    return;
}

int cat_docmap(FILE *output, struct index *idx, int verbose) {
    off_t offset;
    unsigned long int size,
                      i;
    unsigned int sourcefile,
                 bytes,
                 words,
                 aux_len,
                 dwords;
    enum docmap_flag flags;
    double weight;
    char aux[4096];
    enum docmap_ret dm_ret;
    enum mime_types type;

    size = docmap_entries(idx->map);

    for (i = 0; i < size; i++) {
        if ( (dm_ret = docmap_get_location(idx->map, i, &sourcefile,
                  &offset, &bytes, &type, &flags) != DOCMAP_OK)
          || (dm_ret = docmap_get_trecno(idx->map, i, aux, sizeof(aux) - 1,
                  &aux_len) != DOCMAP_OK)
          || (dm_ret = docmap_get_words(idx->map, i, &words) != DOCMAP_OK)
          || (dm_ret = docmap_get_distinct_words(idx->map, i,
                  &dwords) != DOCMAP_OK)
          || (dm_ret = docmap_get_weight(idx->map, i, &weight) != DOCMAP_OK))
            return 0;

        aux[aux_len] = '\0';
        /* output document stats */
        if (verbose) {
            fprintf(output, "%lu location (%u ", i, sourcefile);
            off_out(output, offset);
            fprintf(output, "), "
              "size %u (%u words, %u distinct words, %f weight) type %s%s%s\n", 
              bytes, words, dwords, weight, 
              mime_string(type), aux ? ": " : "", aux ? aux : "");
        } else {
            fprintf(output, "%lu, size %u (%u words, %u distinct words, "
              "%f weight)%s%s\n", i, bytes, words, dwords, weight, 
              aux ? ": " : "", aux ? aux : "");
        }
    }

    fprintf(output, "\n%lu entries\n", size);
    return 1;
}

int cat_index(FILE *output, struct index *idx, int verbose) {
    unsigned long int docno_d,        /* current document number delta */
                      docno,          /* current document number */
                      occurences,     /* number of times term appears in doc */
                      wordno,         /* the word offset of the occurance */
                      wordno_d,       /* delta of word offset */
                      i,
                      j;
    struct vec v;
    struct vec vocabv;
    unsigned int state[3] = {0, 0, 0};
    const char *term;
    void *addr,
         *buf = NULL;
    unsigned int termlen,
                 terms = 0,
                 tmp;
    struct vocab_vector vocab;
    int fd = -1;
    enum vocab_ret ret;

    while ((term 
        = iobtree_next_term(idx->vocab, state, &termlen, &addr, &tmp))) {

        vocabv.pos = addr;
        vocabv.end = vocabv.pos + tmp;
        while ((ret = vocab_decode(&vocab, &vocabv)) == VOCAB_OK) {
            putc('\'', output);
            for (i = 0; i < termlen; i++) {
                putc(term[i], output);
            }

            fprintf(output, "' docs %lu, last %lu, occs %lu", 
              vocab.header.doc.docs, vocab.header.doc.last, 
              vocab.header.doc.occurs);

            switch (vocab.type) {
            case VOCAB_VTYPE_DOC:
                fprintf(output, " doc");
                break;
            case VOCAB_VTYPE_DOCWP:
                fprintf(output, " docwp");
                break;
            case VOCAB_VTYPE_IMPACT:
                fprintf(output, " impact");
                break;
            }
            
            switch (vocab.location) {
            case VOCAB_LOCATION_VOCAB:
                v.pos = ((char *) addr) + vocab_len(&vocab) - vocab.size;
                v.end = v.pos + vocab.size;
                buf = NULL;

                if (verbose) {
                    fprintf(output, " (location vocab, size %lu)", vocab.size);
                }
                break;

            default:
                if (verbose) {
                    fprintf(output, " (location %u %lu %u size %lu)", 
                      vocab.loc.file.fileno, vocab.loc.file.offset, 
                      vocab.loc.file.capacity, vocab.size);
                }
                tmp = 0;
                if (((fd = fdset_pin(idx->fd, idx->index_type, 
                    vocab.loc.file.fileno, vocab.loc.file.offset, SEEK_SET)) 
                      >= 0)
                  && (buf = v.pos = malloc(vocab.size))
                  && (read(fd, buf, vocab.size) == (ssize_t) vocab.size)
                  && (tmp = 1)
                  && (fdset_unpin(idx->fd, idx->index_type, 
                      vocab.loc.file.fileno, fd) == FDSET_OK)) {
                    /* succeeded */
                    v.end = v.pos + vocab.size;
                } else {
                    if (fd >= 0) {
                        fdset_unpin(idx->fd, idx->index_type, 
                          vocab.loc.file.fileno, fd);
                        if (tmp) {
                            free(buf);
                        }
                    }

                    fprintf(stderr, "couldn't read vector\n");
                    return 0;
                }
                break;
            };

            /* decode vector */
            fprintf(output, ":");
            if (vocab.type == VOCAB_VTYPE_DOC 
              || vocab.type == VOCAB_VTYPE_DOCWP) {
                docno = 0;
                for (i = 0; i < vocab.header.doc.docs; i++) {
                    if ((vec_vbyte_read(&v, &docno_d)) 
                      && (vec_vbyte_read(&v, &occurences))) {
                        docno += docno_d;
                        fprintf(output, " (%ld %ld", docno + i, occurences);
                        if (vocab.type == VOCAB_VTYPE_DOCWP) {
                            fprintf(output, " [");
                            wordno = 0;
                            for (j = 0; j < occurences; j++) {
                                if (vec_vbyte_read(&v, &wordno_d)) {
                                    wordno += wordno_d;
                                    fprintf(output, " %ld", wordno + j);
                                } else {
                                    fprintf(stderr, 
                                      "error reading offset from vectors\n");
                                    if (buf) {
                                        free(buf);
                                    }
                                    return 0;
                                }
                            }
                            fprintf(output, " ]");
                        } 
                        fprintf(output, ")");
                    } else {
                        fprintf(stderr, "error reading docno from vectors\n");
                        if (buf) {
                            free(buf);
                        }
                        return 0;
                    }
                }

                if (docno + i - 1 != vocab.header.doc.last) {
                    fprintf(output, " (incorrect last value (%lu vs %lu)!)\n",
                      docno + i - 1, vocab.header.doc.last);
                    if (buf) {
                        free(buf);
                    }
                    return 0;
                }
            } else if (vocab.type == VOCAB_VTYPE_IMPACT) {
                unsigned long int docno,
                                  docno_d;
                unsigned long int blocksize = 0;
                unsigned long int impact;

                while (VEC_LEN(&v)) {
                    docno = -1;
                    vec_vbyte_read(&v, &blocksize);
                    vec_vbyte_read(&v, &impact);
                    fprintf(output, " (%ld %ld [", impact, blocksize);
                    for (i = 0; i < blocksize; i++) {
                        vec_vbyte_read(&v, &docno_d);
                        docno += docno_d + 1;
                        fprintf(output, " %ld", docno);
                    }
                    fprintf(output, " ])");
                }
            }

            if (buf) {
                free(buf);
                buf = NULL;
            }

            putc('\n', output);
        }
 
        if (ret != VOCAB_END) {
            fprintf(stderr, "could decode vocab item\n");
            return 0;
        }

        terms++;
    }

    if (term || (terms != iobtree_size(idx->vocab))) {
        fprintf(stderr, "didn't get all terms\n");
        return 0;
    }

    return 1;
}

/* moved this function in from vbyte.c because we want to get rid of vbyte.c and
 * i can't be bothered updating the intermediate catting functionality to use
 * vec.c instead */
static unsigned int vbyte_read(FILE* fp, unsigned long int* n) {
    unsigned int ret = 1;
    unsigned int count = 0; 
    uint8_t byte;

    *n = 0;
    while (fread(&byte, 1, 1, fp)) {
        if (byte & 0x80) {
            /* For each time we get a group of 7 bits, need to left-shift the 
               latest group by an additional 7 places, since the groups were 
               effectively stored in reverse order.*/
            *n |= ((byte & 0x7f) << count);
            ret++;
            count += 7;
        } else if (ret > (sizeof(*n))) {
            if (ret == (sizeof(*n) + 1) 
              && (byte < (1 << (sizeof(*n) + 1)))) {
                /* its a large, but valid number */
                *n |= byte << count;
                return ret;
            } else {
                /* we've overflowed */

                /* pretend we detected it as it happened */
                fseek(fp, - (int) (ret - (sizeof(*n) + 1)), SEEK_CUR);
                errno = EOVERFLOW;
                return 0;
            }
        } else {
            *n |= byte << count;
            return ret;
        }
    }

    /* got a read error */
    return 0;
}

int cat_intermediate(FILE* output, FILE* idx, int verbose) {
    char term[TERMLEN_MAX + 1];          /* the current index word */
    unsigned long int numdocs,           /* number of docs word occurs in */
                      termlen,           /* length of current index word */
                      docno_d,           /* current document number delta */
                      docno,             /* current document number */
                      occurs,            /* total number of times term occurs */
                      occurences,        /* number of times term is in doc */
                      wordno,            /* the word offset of the occurance */
                      wordno_d,          /* delta of word offset */
                      last,              /* last docno encoded */
                      size,              /* size of the vector */
                      i,
                      j;
    struct vec v;
    char *buf;

    /* read word length ... */
    while (((vbyte_read(idx, &termlen)) > 0) && (termlen <= TERMLEN_MAX) 
      /* ...and the word... */
      && (fread(term, 1, termlen, idx) == termlen)
      /* ...and the number of documents its in... */
      && ((vbyte_read(idx, &numdocs)) > 0)
      /* ...and the total number of occurrances... */
      && ((vbyte_read(idx, &occurs)) > 0)
      /* ...and the last value ... */
      && (vbyte_read(idx, &last))
      /* ...and read the size of the vector */
      && ((vbyte_read(idx, &size)) > 0)) {
        term[termlen] = '\0';
        fprintf(output, "%s occurs in %lu docs (%lu total)", term, numdocs, 
          occurs);
        docno = 0;
        if (verbose) {
            fprintf(output, " (last %lu)", last);
        }
        fprintf(output, ":");
        /* read the vector */
        if ((buf = malloc(size)) && fread(buf, size, 1, idx)) {
            v.pos = buf;
            v.end = v.pos + size;

            fprintf(output, " (len %lu)", size);
            for (i = 0; i < numdocs; i++) {
                if ((vec_vbyte_read(&v, &docno_d)) 
                  && (vec_vbyte_read(&v, &occurences))) {
                    docno += docno_d;
                    fprintf(output, " (%ld %ld [", docno + i, occurences);
                    wordno = 0;

                    for (j = 0; j < occurences; j++) {
                        if (vec_vbyte_read(&v, &wordno_d)) {
                            wordno += wordno_d;
                            fprintf(output, " %ld", wordno + j);
                        } else {
                            fprintf(stderr, 
                              "error reading offset from vectors "
                              "(at %lu docno)\n", docno + i);
                            return 0;
                        }
                    }
                    fprintf(output, " ])");
                } else {
                    fprintf(stderr, "error reading docno from vectors "
                      "(at %u of %lu bytes, %lu of %lu docs)\n", 
                      (unsigned int) (v.pos - buf), size, i, numdocs);
                    return 0;
                }
            }

            if (docno + numdocs - 1 != last) {
                fprintf(output, "(wrong last value %lu vs %lu!)\n", 
                  docno + i, last);
                return 0;
            }

            free(buf);
        } else {
            fprintf(stderr, "error reading vector\n");
            return 0;
        }
        fprintf(output, "\n");
    } 

    return 1;
}

FILE* file_open(const char* prefix, const char* suffix) {
    FILE* fp = NULL;
    unsigned int len = strlen(prefix) + strlen(suffix);
    char* buf = malloc(len + 1);

    if (buf) {
        strcpy(buf, prefix);
        strcat(buf, suffix);
        fp = fopen(buf, "rb");
        free(buf);
    }

    return fp;
}

void print_usage(FILE *stream, const char *progname) {
    fprintf(stream, "usage: %s index\n", progname);
    fprintf(stream, "  options:\n");
    fprintf(stream, "    -i,--intermediate: treat given file as intermediate "
      "merge file\n");
    fprintf(stream, "    -d,--docmap: print document map for given index\n");
    fprintf(stream, "    -x,--vocab: print vocabulary for given index\n");
    fprintf(stream, "    -v,--verbose: verbose output\n");
    fprintf(stream, "    -h,--help: this message\n");
}

int main(int argc, char** argv) {
    FILE* vectors;
    int intermediate = 0,
        docmap = 0,
        verbose = 0,
        idx_flag = 0,
        id;
    struct index *idx;
    struct getlongopt *parser;
    enum getlongopt_ret ret;
    const char *arg;
    unsigned int ind;

    struct getlongopt_opt opts[] = {
        {"help", 'h', GETLONGOPT_ARG_NONE, 'h'},
        {"verbose", 'v', GETLONGOPT_ARG_NONE, 'v'},
        {"intermediate", 'i', GETLONGOPT_ARG_NONE, 'i'},
        {"docmap", 'd', GETLONGOPT_ARG_NONE, 'd'},
        {"vocab", 'x', GETLONGOPT_ARG_NONE, 'x'}
    };

    if ((parser = getlongopt_new(argc - 1, (const char **) &argv[1], opts, 
        sizeof(opts) / sizeof(*opts)))) {

        while ((ret = getlongopt(parser, &id, &arg)) == GETLONGOPT_OK) {
            switch (id) {
            case 'h':
                print_usage(stdout, argv[0]);
                return EXIT_SUCCESS;

            case 'x':
                idx_flag = 1;
                break;

            case 'v':
                verbose = 1;
                break;

            case 'i':
                intermediate = 1;
                break;

            case 'd':
                docmap = 1;
                break;

            default:
                /* shouldn't happen */
                assert(0);
            }
        }
    } else {
        fprintf(stderr, "failed to initialise option parser\n");
        return EXIT_FAILURE;
    }

    ind = getlongopt_optind(parser) + 1;
    getlongopt_delete(parser);

    if (ret == GETLONGOPT_END) {
        /* succeeded, do nothing */
    } else {
        if (ret == GETLONGOPT_UNKNOWN) {
            fprintf(stderr, "unknown option '%s'\n", argv[ind]);
        } else if (ret == GETLONGOPT_MISSING_ARG) {
            fprintf(stderr, "missing argument to option '%s'\n", argv[ind]);
        } else if (ret == GETLONGOPT_ERR) {
            fprintf(stderr, "unexpected error parsing options (around '%s')\n",
              argv[ind]);
        } else {
            /* shouldn't happen */
            assert(0);
        }
        return EXIT_FAILURE;
    }

    /* rest of arguments should be files */
    if (ind == (unsigned int) argc) {
        print_usage(stderr, *argv);
        return EXIT_SUCCESS;
    } else {
        while (ind < (unsigned int) argc) {
            if (intermediate) {
                if ((vectors = file_open(argv[ind], ""))) {
                    if (!cat_intermediate(stdout, vectors, verbose)) {
                        fprintf(stderr, "unable to cat index for '%s'\n", 
                          argv[ind]);
                        fclose(vectors);
                        return EXIT_FAILURE;
                    }
                    fclose(vectors);
                } else {
                    fprintf(stderr, "unable to open intermediate file %s\n", 
                      argv[ind]);
                    return EXIT_FAILURE;
                }
            } else if (docmap) {
                if ((idx = index_load(argv[ind], MEMORY_DEFAULT, 
                    INDEX_LOAD_NOOPT, NULL))) {

                    if (!cat_docmap(stdout, idx, verbose)) {
                        fprintf(stderr, "unable to cat docmap for '%s'\n", 
                          argv[ind]);
                        index_delete(idx);
                        return EXIT_FAILURE;
                    }
                    index_delete(idx);
                } else {
                    fprintf(stderr, "unable to open index %s\n", argv[ind]);
                    return EXIT_FAILURE;
                }
            } else if (idx_flag) {
                if ((idx = index_load(argv[ind], MEMORY_DEFAULT, 
                    INDEX_LOAD_NOOPT, NULL))) {

                    if (!cat_vocab(stdout, idx, verbose)) {
                        fprintf(stderr, "unable to cat vocab for '%s'\n", 
                          argv[ind]);
                        index_delete(idx);
                        return EXIT_FAILURE;
                    }
                    index_delete(idx);
                } else {
                    fprintf(stderr, "unable to open index %s\n", argv[ind]);
                    return EXIT_FAILURE;
                }
            } else {
                if ((idx = index_load(argv[ind], MEMORY_DEFAULT, 
                    INDEX_LOAD_NOOPT, NULL))) {

                    if (!cat_index(stdout, idx, verbose)) {
                        fprintf(stderr, "unable to cat index for '%s'\n", 
                          argv[ind]);
                        index_delete(idx);
                        return EXIT_FAILURE;
                    }
                    index_delete(idx);
                } else {
                    fprintf(stderr, "unable to open index %s\n", argv[ind]);
                    return EXIT_FAILURE;
                }
            }
            ind++;
        }
    }

    return EXIT_SUCCESS;
}

