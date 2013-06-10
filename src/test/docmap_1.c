/*
 *  Test the docmap module.
 *
 *
 *  Each line of the input file represents a "script" for
 *  a test run.  Each line is made up of a series of
 *  space-delimited tokens.  The first token is the "identifier"
 *  of the run.  The remaining tokens are keyword "commands", some
 *  of which are followed by "=<arg>".  The commands are:
 *
 *     ADD=<num>
 *          Add <num> number of documents
 *
 *     CHECK
 *          Check docmap against all added docs
 *
 *     RAND_CHECK
 *          Check a random value of a random document
 *
 *     DUMP_LOAD
 *          Dump and load the docmap
 *
 *  The following configuration commands may occur on a line by itself:
 *
 *     @VERBOSE
 *          Switch to verbose mode
 *     @NOVERBOSE
 *          Switch out of verbose mode
 *     @SEED <seed>
 *          Seed the random-number generator with <seed>
 *     @INPUT <filename>
 *          Use <filename> for input, not random data
 *     @MAXFILE <bytes>
 *          Limit files to a maxmimum size of <bytes>
 */

#include "firstinclude.h"
#include "test.h"
#include "testutils.h"
#include "docmap.h"
#include "lcrand.h"
#include "error.h"
#include "str.h"
#include "fdset.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

static int verbose = 0;

#define NEWDOCMAP_TEST_FD_TYPE 0xEADE

#define DEFAULT_SEED 87

#define DEFAULT_FILELEN_MAX (200u * 1024u * 1024u)

#define DEFAULT_SOURCEFILE_MAX 20u
#define DEFAULT_OFFSET_MAX (3u * 1024u * 1024u * 1024u)
#define DEFAULT_BYTES_MAX (2u * 1024u * 1024u)
#define DEFAULT_WEIGHT_MAX (55.0)
#define DEFAULT_AUX_LEN_MAX (2 * 1024u)
#define DEFAULT_MTYPE_MAX (300)

static unsigned filelen_max = DEFAULT_FILELEN_MAX;

static unsigned int bytes_max = DEFAULT_BYTES_MAX;
/* words < bytes, distinct_words < words. */
static double weight_max = DEFAULT_WEIGHT_MAX;
static unsigned int aux_len_max = DEFAULT_AUX_LEN_MAX;
static unsigned int mtype_max = DEFAULT_MTYPE_MAX;

static double get_dbl_rand(double limit) {
    assert(tu_rand != NULL);
    return ((double) limit * lcrand(tu_rand)/(LCRAND_MAX + 1.0));
}

struct docinfo {
    unsigned int sourcefile;
    unsigned long int offset;
    unsigned int bytes;
    enum docmap_flag flags;
    unsigned int words;
    unsigned int distinct_words;
    enum mime_types mtype;
    double weight;
    char * aux;
    unsigned aux_len;
};

static void init_rand_docinfo(struct docinfo * docinfo, struct docinfo *prev) {
    unsigned int i;

    /* note that we generate some numbers in relation to others to satisfy
     * properties that real documents would have */

    docinfo->bytes = tu_rand_limit(bytes_max);
    docinfo->words = tu_rand_limit((docinfo->bytes + 1) / 2);
    docinfo->distinct_words = tu_rand_limit(docinfo->words);
    docinfo->weight = get_dbl_rand(weight_max);
    docinfo->mtype = tu_rand_limit(mtype_max);

    if (!prev) {
        /* first document */
        docinfo->sourcefile = 0;
        docinfo->offset = 0;
        if (tu_rand_limit(2)) {
            docinfo->flags = DOCMAP_COMPRESSED;
        } else {
            docinfo->flags = DOCMAP_NO_FLAGS;
        }
    } else if (tu_rand_limit(10) > 8) {
        /* start new document */
        docinfo->sourcefile = prev->sourcefile + 1;
        docinfo->offset = 0;
        if (tu_rand_limit(2)) {
            docinfo->flags = DOCMAP_COMPRESSED;
        } else {
            docinfo->flags = DOCMAP_NO_FLAGS;
        }
    } else {
        /* continue previous document */
        docinfo->sourcefile = prev->sourcefile;
        docinfo->offset = prev->offset + prev->bytes;
        docinfo->flags = prev->flags;
    }

    docinfo->aux_len = tu_rand_limit(aux_len_max);
    docinfo->aux = tu_get_rand_data(docinfo->aux_len);

    /* convert random aux entry into something more readable */
    for (i = 0; i < docinfo->aux_len; i++) {
        docinfo->aux[i] = 32 + ((unsigned char) docinfo->aux[i]) % 95;
    }
    docinfo->aux[i] = '\0';
}

int do_run(char ** cmds, int num_cmds);

int process_config(char * config);

#define CHECK_FUNC(field) \
static int check_ ## field(struct docmap * docmap, \
  struct docinfo * docinfos, unsigned d)

CHECK_FUNC(aux);
CHECK_FUNC(location);
CHECK_FUNC(bytes);
CHECK_FUNC(words);
CHECK_FUNC(distinct_words);
CHECK_FUNC(weight);

int test_file(FILE * fp, int argc, char ** argv) {
    char buf[4096];
    int line_num = 0;
    char * ptr;
    int all_passed = 1;

    tu_init_rand_or_die(DEFAULT_SEED);
    while (fgets((char *) buf, 4095, fp)) {
        line_num++;
        ptr = (char *) str_ltrim(buf);
        str_rtrim(ptr);
        if (*ptr == '@') {
            if (!process_config(ptr + 1))
                fprintf(stderr, "Error with config on line %d, '%s'\n",
                  line_num, ptr);
        } else if (*ptr != '#' && *ptr != '\0') {
            unsigned int parts = 0;
            char ** cmds = str_split(ptr, " ", &parts);
            int ret;

            ret = do_run(cmds, parts);
            if (ret == -1) {
                all_passed = 0;
                fprintf(stderr, "Error doing run on line %d\n", line_num);
            } else if (ret == 0) {
                all_passed = 0;
            }
            free(cmds);
        }
    }
    return all_passed;
}

int process_config(char * config) {
    if (strncmp(config, "VERBOSE", 7) == 0) {
        verbose = 1;
        fprintf(stderr, "... VERBOSE mode on\n");
    } else if (strncmp(config, "NOVERBOSE", 9) == 0) {
        verbose = 0;
    } else if (strncmp(config, "SEED", 4) == 0) {
        unsigned seed = atoi(config + 4);
        if (seed < 1)
            return 0;
        tu_init_rand_or_die(seed);
    } else if (strncmp(config, "INPUT", 5) == 0) {
        char * fname = (char *) str_ltrim(config + 5);
        if (*fname == '\0')
            return 0;
        str_rtrim(fname);
        tu_sample_data_file_init(fname);
    } else if (strncmp(config, "MAXFILE", 7) == 0) {
        unsigned maxfile = atoi(config + 7);
        if (maxfile < 1)
            return 0;
        filelen_max = maxfile;
    } else {
        return 0;
    }
    return 1;
}

int do_run(char ** cmds, int num_cmds) {
    int c;
    char * id;
    struct fdset * fdset = NULL;
    struct docmap * docmap = NULL;
    enum docmap_ret dm_ret;
    static struct docinfo * docinfos= NULL;
    static unsigned docinfos_size = 0;
    unsigned num_docinfos = 0;
    int ret = 1;

    id = cmds[0];
    if (verbose) {
        fprintf(stderr, "... Starting run '%s'\n", id);
    }
    fdset = fdset_new(0777, 1);
    if (fdset == NULL) {
        fprintf(stderr, "Error creating fdset\n");
        goto ERROR;
    }
    fdset_set_type_name(fdset, NEWDOCMAP_TEST_FD_TYPE, "docmaptest", 
      strlen("docmaptest"), 1 /* writeable */);

    docmap = docmap_new(fdset, NEWDOCMAP_TEST_FD_TYPE, 4096, 0, 
      filelen_max, 0xfffff, &dm_ret);
    if (dm_ret != DOCMAP_OK) {
        ERROR1("Failed to create docmap: code %d\n", dm_ret);
        goto ERROR;
    }

    for (c = 1; c < num_cmds; c++) {
        char * cmd = cmds[c];

        if (verbose)
            fprintf(stderr, "... command '%s'\n", cmd);

        /*
         *  ADD=<num>
         */
        if (strncmp(cmd, "ADD=", 4) == 0) {
            int num_adds = atoi(cmd + 4);
            unsigned d;
            if (num_adds < 1) {
                fprintf(stderr, "Invalid ADD command\n");
                goto ERROR;
            }
            while (num_docinfos + num_adds > docinfos_size) {
                if (docinfos_size == 0) {
                    docinfos_size = 4096;
                } else {
                    docinfos_size *= 2;
                }
                if ( (docinfos = realloc(docinfos, sizeof(*docinfos)
                          * docinfos_size)) == NULL) {
                    perror("ERROR reallocating docinfos");
                    exit(EXIT_FAILURE);
                }
            }
            for (d = 0; d < num_adds; d++) {
                struct docinfo * docinfo = &docinfos[num_docinfos];
                enum docmap_ret dm_ret;
                unsigned long int docno;
                init_rand_docinfo(docinfo, 
                  num_docinfos ? &docinfos[num_docinfos - 1] : NULL);

                dm_ret = docmap_add(docmap, docinfo->sourcefile,
                  docinfo->offset, docinfo->bytes, 
                  docinfo->flags, docinfo->words,
                  docinfo->distinct_words, docinfo->weight,
                  docinfo->aux, docinfo->aux_len, docinfo->mtype, &docno);
                if (dm_ret != DOCMAP_OK) {
                    ERROR1("docmap error: '%s'", docmap_strerror(dm_ret));
                    goto FAILURE;
                }
                if (docno != num_docinfos) {
                    ERROR2("docno returned as '%lu', should be '%u'\n",
                      docno, num_docinfos);
                }
                num_docinfos++;
            }
        }

        /*
         *  CHECK
         */
        else if (strncmp(cmd, "CHECK", 5) == 0) {
            unsigned d;

            for (d = 0; d < num_docinfos; d++) {
#define DO_CHECK(field) \
                if (!check_ ## field(docmap,docinfos, d)) \
                    goto FAILURE
                DO_CHECK(aux);
                DO_CHECK(location);
                DO_CHECK(bytes);
                DO_CHECK(words);
                DO_CHECK(distinct_words);
                DO_CHECK(weight);
#undef DO_CHECK
            }
        }

        /*
         * RAND_CHECK.
         */
        else if (strncmp(cmd, "RAND_CHECK", 10) == 0) {
            unsigned d;
            enum { AUX = 0, LOCATION = 1, BYTES = 2,
                WORDS = 3, DISTINCT_WORDS = 4, WEIGHT = 5,
                END_ENUM = 6 } type;

            d = tu_rand_limit(num_docinfos);
            type = tu_rand_limit(END_ENUM);
#define DO_CHECK(field) \
            if (!check_ ## field(docmap,docinfos, d)) \
                goto FAILURE
            switch (type) {
            case AUX:
                DO_CHECK(aux);
                break;
            case LOCATION:
                DO_CHECK(location);
                break;
            case BYTES:
                DO_CHECK(bytes);
                break;
            case WORDS:
                DO_CHECK(words);
                break;
            case DISTINCT_WORDS:
                DO_CHECK(distinct_words);
                break;
            case WEIGHT:
                DO_CHECK(weight);
                break;
            default:
                assert(0);
            }
#undef DO_CHECK
        }

        /*
         * DUMP_LOAD.
         */
        else if (strncmp(cmd, "DUMP_LOAD", 9) == 0) {
            enum docmap_ret dm_ret;

            dm_ret = docmap_save(docmap);
            if (dm_ret != DOCMAP_OK) {
                ERROR1("return code of '%d'", dm_ret);
                goto FAILURE;
            }
            docmap_delete(docmap);
            docmap = docmap_load(fdset, NEWDOCMAP_TEST_FD_TYPE, 4096, 0, 
                filelen_max, 0xfffffff, &dm_ret);
            if (dm_ret != DOCMAP_OK) {
                ERROR1("docmap error: '%s'\n", docmap_strerror(dm_ret));
                abort();
                goto FAILURE;
            }
        }

        else {
            fprintf(stderr, "Unknown command '%s'\n", cmd);
            goto ERROR;
        }
    }

    goto END;

FAILURE:
    ret = 0;
    goto END;

ERROR:
    ret = -1;

END:
    if (fdset != NULL) {
        int i;
        for (i = 0; i < 256; i++) 
            fdset_unlink(fdset, NEWDOCMAP_TEST_FD_TYPE, i);
        fdset_delete(fdset);
    }
    if (docmap != NULL) {
        docmap_delete(docmap);
    }
    return ret;
}

#define CHECK_RET(ret) \
            if (ret != DOCMAP_OK) { \
                ERROR1("return code of '%d'", ret); \
                return 0; \
            } \
            ret = DOCMAP_OK
#define CHECK_VAL(valname, fmt_code, dno) \
            if (docinfo->valname != valname) { \
                ERROR4("%s is '" fmt_code "', should be '" fmt_code \
                    "' for docno %u", # valname, valname, docinfo->valname, \
                    dno); \
                return 0; \
            }

CHECK_FUNC(aux) {
    unsigned aux_len;
    static char * aux_buf = NULL;
    static unsigned aux_buf_len = 0;
    enum docmap_ret dm_ret;
    struct docinfo * docinfo = &docinfos[d];

    dm_ret = docmap_get_trecno(docmap, d, aux_buf, aux_buf_len,
      &aux_len);
    if (dm_ret == DOCMAP_OK && aux_len > aux_buf_len) {
        aux_buf = realloc(aux_buf, aux_len + 1);
        if (aux_buf == NULL) {
            perror("ERROR reallocating aux_buf");
            exit(EXIT_FAILURE);
        }
        aux_buf_len = aux_len;
        dm_ret = docmap_get_trecno(docmap, d, aux_buf, aux_buf_len,
          &aux_len);
    }

    CHECK_RET(dm_ret);
    CHECK_VAL(aux_len, "%u", d);
    if (memcmp(docinfo->aux, aux_buf, aux_len) != 0) {
        ERROR("returned and entered aux differ");
        return 0;
    }
    return 1;
}

CHECK_FUNC(location) {
    enum docmap_ret dm_ret;
    unsigned int sourcefile;
    off_t offset;
    unsigned int bytes;
    enum docmap_flag flags;
    struct docinfo * docinfo = &docinfos[d];
    enum mime_types mtype;

    dm_ret = docmap_get_location(docmap, d, &sourcefile, &offset, &bytes,
      &mtype, &flags);
    CHECK_RET(dm_ret);
    CHECK_VAL(sourcefile, "%u", d);
    CHECK_VAL(offset, "%lu", d);
    CHECK_VAL(bytes, "%u", d);
    CHECK_VAL(flags, "%u", d);
    CHECK_VAL(mtype, "%d", d);
    return 1;
}

#define CHECK_SINGLE_VAL_FUNC_DEFN(field, type, fmt) \
CHECK_FUNC(field) { \
    enum docmap_ret dm_ret; \
    type field; \
    struct docinfo * docinfo = &docinfos[d]; \
\
    dm_ret = docmap_get_ ## field(docmap, d, &field); \
    CHECK_RET(dm_ret); \
    CHECK_VAL(field, fmt, d); \
    return 1; \
}

#define CHECK_UNSIGNED_FUNC_DEFN(field) \
CHECK_SINGLE_VAL_FUNC_DEFN(field, unsigned int, "%u")
    
CHECK_UNSIGNED_FUNC_DEFN(bytes)
CHECK_UNSIGNED_FUNC_DEFN(words)
CHECK_UNSIGNED_FUNC_DEFN(distinct_words)

CHECK_FUNC(weight) {
    enum docmap_ret dm_ret; 
    double weight; 
    struct docinfo * docinfo = &docinfos[d]; 

    dm_ret = docmap_get_weight(docmap, d, &weight); 
    CHECK_RET(dm_ret); 
    /* floats are converted to mantissa, exponent integer pairs for
       storage, then back to floats on load.  Allow a slight loss of
       precision. */
    if (weight > docinfo->weight * 1.04 || weight < docinfo->weight * 0.96) {
        ERROR3("weight is '%lf', should be '%lf', diff %lf", docinfo->weight, 
          weight, docinfo->weight / weight);
        return 0;
    }
    return 1; 
}
