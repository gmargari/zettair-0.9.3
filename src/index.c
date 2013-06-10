/* index.c implements high level operations for a simple, non-distributed 
 * search engine index
 *
 * some operations have been moved to index_search.c and index_update.c
 * to keep each file within reasonable bounds
 *
 * written nml 2003-04-14
 *
 */

#include "firstinclude.h"

#include "_index.h"
#include "index.h"

#include "_mem.h"

#include "def.h"
#include "error.h"
#include "fdset.h"
#include "freemap.h"
#include "docmap.h"
#include "getmaxfsize.h"
#include "iobtree.h"
#include "str.h"
#include "stream.h"
#include "stem.h"
#include "summarise.h"
#include "timings.h"
#include "makeindex.h"
#include "mem.h"
#include "mime.h"
#include "postings.h"
#include "pyramid.h"
#include "psettings.h"
#include "stop.h"
#include "vec.h"
#include "vocab.h"
#include "zvalgrind.h"
#include "impact_build.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Version number for index format.  This gets incremented every time
   a change is made to the format of the on-disk index. */
#define INDEX_FILE_FORMAT_VERSION 0x3141592e

const char *index_doctype_names[] = {"" /* err */, "html", "trec", "inex"};

/* function to return the stemming function used by an index */
void (*index_stemmer(struct index *idx))(void *, char *) {
    if ((idx->flags & INDEX_STEMMED_LIGHT) == INDEX_STEMMED_LIGHT) {
        return (void (*)(void *, char *)) stem_light; 
    } else if (idx->flags & INDEX_STEMMED_PORTERS) {
        return (void (*)(void *, char *)) stem_porters; 
    } else if (idx->flags & INDEX_STEMMED_EDS) {
        return (void (*)(void *, char *)) stem_eds; 
    } else {
        return NULL;
    }
}

/* internal function to atomically perform a read */
ssize_t index_atomic_read(int fd, void *buf, unsigned int size) {
    unsigned int rlen,
                 len = size;
    char *pos = buf;

    while (len && ((rlen = read(fd, pos, len)) > 0)) {
        pos += rlen;
        len -= rlen;
    }

    if (len) {
        return -1;
    } else {
        return size;
    }
}

/* internal function to atomically perform a write */
ssize_t index_atomic_write(int fd, void *buf, unsigned int size) {
    unsigned int wlen,
                 len = size;
    char *pos = buf;

    while (len && ((wlen = write(fd, pos, len)) != -1)) {
        pos += wlen;
        len -= wlen;
    }

    if (len) {
        return -1;
    } else {
        return size;
    }
}

#define INDEX_HEADER ("\035\170" PACKAGE)    /* 1dx package_name */

#define FAIL()                                                                \
    if (1) {                                                                  \
        assert(!CRASH);                                                       \
        free(buf);                                                            \
        fclose(fp);                                                           \
        return 0;                                                             \
    } else

#define READ_MEMBER(fp, member, sizetype)                                     \
    /* note that we have to transfer to a tmp variable after we ntoh into a   \
     * second tmp variable to ensure that the ntoh is on two variables of the \
     * correct size.  This prevents bugs on bigendian architectures. */       \
    if (fread(&tmp_##sizetype, sizeof(sizetype), 1, fp)) {                    \
        mem_ntoh(&tmp_tmp_##sizetype, &tmp_##sizetype, sizeof(sizetype));     \
        member = tmp_tmp_##sizetype;                                          \
    } else if (1) {                                                           \
        FAIL();                                                               \
    } else

#define READ_DOUBLE(fp, member)                                               \
    if (1) {                                                                  \
        READ_MEMBER(fp, mantissa, uint32_t);                                  \
        READ_MEMBER(fp, exponent, uint32_t);                                  \
        member = ldexp(((double) mantissa) / UINT32_MAX, exponent);           \
    } else

static int index_params_read(struct index *idx, 
  unsigned int *root_fileno, unsigned long int *root_offset, 
  unsigned int *terms, 
  unsigned int *vfileno, unsigned long int *voffset, unsigned int *vsize,
  int ignore_version) {
    FILE *fp;
    char *buf;
    unsigned int bufsize = FILENAME_MAX;
    uint32_t tmp_uint32_t,
             tmp_tmp_uint32_t;
    uint8_t tmp_uint8_t,
            tmp_tmp_uint8_t;
    uint32_t mantissa;
    int exponent;
    unsigned long int tmp,
                      tmplen;
    unsigned int size;
    uint32_t file_format_version;
    char filename[FILENAME_MAX + 1];

    if (bufsize < storagep_size()) {
        bufsize = storagep_size();
    }

    /* note that i had an alternative method of doing this using fdopen(), 
     * except that had funny behaviour on OS X (?) */
    if ((buf = malloc(bufsize))
      && (fdset_name(idx->fd, idx->param_type, 0, filename, FILENAME_MAX, 
          &size, &exponent) == FDSET_OK)
      && (((filename[size] = '\0'), fp = fopen(filename, "rb"))
        /* blech, hack around times when too many fd's are open in fdset */
        || (((errno == EMFILE) || (errno == ENFILE)) 
          && (fdset_close(idx->fd) == FDSET_OK)
          && (fp = fopen(filename, "rb"))))) {
        /* open succeeded */

        /* get signature bytes from the start */
        assert(str_len(INDEX_HEADER) < FILENAME_MAX);
        if (fread(buf, 1, str_len(INDEX_HEADER), fp) < str_len(INDEX_HEADER)) {
            FAIL();
        }
        if (str_ncmp(buf, INDEX_HEADER, str_len(INDEX_HEADER))) {
            ERROR1("index header not found at head of params buffer: "
              "looking for '%s'", INDEX_HEADER);
            FAIL();
        }

        READ_MEMBER(fp, file_format_version, uint32_t);
        if (file_format_version != INDEX_FILE_FORMAT_VERSION) {
            ERROR2("wrong index file format version: index file has version "
              "0x%08x, executable has version 0x%08x", file_format_version,
              INDEX_FILE_FORMAT_VERSION);
            if (!ignore_version) {
                FAIL();
            }
        }
        READ_MEMBER(fp, idx->flags, uint8_t);

        READ_MEMBER(fp, idx->repos, uint32_t);
        READ_MEMBER(fp, idx->vectors, uint32_t);
        READ_MEMBER(fp, idx->vocabs, uint32_t);
        READ_MEMBER(fp, idx->repos_pos, uint32_t);
        if (!idx->vectors || !idx->vocabs) {
            ERROR2("0 length vectors (%u) or vocabs (%u) in params load", 
              idx->vectors, idx->vocabs);
            FAIL();
        }

        READ_MEMBER(fp, idx->stats.terms_high, uint32_t);
        READ_MEMBER(fp, idx->stats.terms_low, uint32_t);
        READ_MEMBER(fp, idx->stats.updates, uint32_t);
        READ_DOUBLE(fp, idx->stats.avg_weight);
        READ_DOUBLE(fp, idx->stats.avg_length);
        READ_DOUBLE(fp, idx->impact_stats.avg_f_t);
        READ_DOUBLE(fp, idx->impact_stats.slope);
        READ_MEMBER(fp, idx->impact_stats.quant_bits, uint32_t);
        READ_DOUBLE(fp, idx->impact_stats.w_qt_min);
        READ_DOUBLE(fp, idx->impact_stats.w_qt_max);
        READ_MEMBER(fp, idx->doc_order_vectors, uint32_t);
        READ_MEMBER(fp, idx->doc_order_word_pos_vectors, uint32_t);
        READ_MEMBER(fp, idx->impact_vectors, uint32_t);

        READ_MEMBER(fp, *root_fileno, uint32_t);
        READ_MEMBER(fp, *root_offset, uint32_t);

        READ_MEMBER(fp, *terms, uint32_t);

        /* read structured params */
        if (fread(buf, storagep_size(), 1, fp)) {
            storagep_read(&idx->storage, buf);
        } else {
            FAIL();
        }

        /* retrieve index config name */
        idx->params.config = NULL;
        READ_MEMBER(fp, tmp, uint32_t);
        if (tmp && (idx->params.config = malloc(tmp + 1))) {
            if (fread(idx->params.config, tmp, 1, fp)) {
                idx->params.config[tmp] = '\0';
            } else {
                FAIL();
            }
        } else if (tmp) {
            ERROR("allocating config buffer");
            FAIL();
        }

        /* retrieve index repository names */
        while (fread(&tmp_uint32_t, sizeof(uint32_t), 1, fp)) {
            MEM_NTOH(&tmp, &tmp_uint32_t, sizeof(uint32_t));
            READ_MEMBER(fp, tmplen, uint32_t);
            if ((tmplen < bufsize) && fread(buf, tmplen, 1, fp)
              && fdset_set_fd_name(idx->fd, idx->repos_type, tmp, buf, tmplen, 
                0) == FDSET_OK) {

                /* succeeded, do nothing */
            } else {
                FAIL();
            }
        }

        if (feof(fp)) {
            free(buf);
            fclose(fp);
            return 1;
        } else {
            assert(!CRASH);
            FAIL();
        }
    } else {
        if (buf) {
            free(buf);
        }
        return 0;
    }
}
#undef FAIL

#define FAIL()                                                                \
    free(buf);                                                                \
    fclose(fp);                                                               \
    return 0

#define WRITE_MEMBER(fp, member, sizetype)                                    \
    /* note that we have to transfer to a tmp variable before we hton into a  \
     * second tmp variable to ensure that the hton is on two variables of the \
     * correct size.  This prevents bugs on bigendian architectures. */       \
    tmp_tmp_##sizetype = (sizetype) member;                                   \
    mem_hton(&tmp_##sizetype, &tmp_tmp_##sizetype, sizeof(sizetype));         \
    VALGRIND_CHECK_READABLE(&tmp_##sizetype, sizeof(sizetype));               \
    if (!fwrite(&tmp_##sizetype, sizeof(sizetype), 1, fp)) {                  \
        FAIL();                                                               \
    } else

#define WRITE_DOUBLE(ptr, val)                                                \
    mantissa = (unsigned long int) (UINT32_MAX * frexp(val, &exponent));      \
    WRITE_MEMBER(ptr, mantissa, uint32_t);                                    \
    WRITE_MEMBER(ptr, exponent, uint32_t)

static int index_params_write(struct index *idx, 
  unsigned int root_fileno, unsigned long int root_offset, 
  unsigned int terms) {
    FILE *fp = NULL;
    char *buf;
    uint32_t tmp_uint32_t,
             tmp_tmp_uint32_t;
    uint8_t tmp_uint8_t,
            tmp_tmp_uint8_t;
    uint32_t mantissa;
    int exponent,
        writeable;
    unsigned int i;
    char filename[FILENAME_MAX + 1];

    if ((buf = malloc(storagep_size()))
      && (fdset_name(idx->fd, idx->param_type, 0, filename, FILENAME_MAX, 
          &i, &exponent) == FDSET_OK)
      && (((filename[i] = '\0'), fp = fopen(filename, "wb"))
        /* blech, hack around times when too many fd's are open in fdset */
        || (((errno == EMFILE) || (errno == ENFILE)) 
          && (fdset_close(idx->fd) == FDSET_OK)
          && (fp = fopen(filename, "wb"))))
      /* write in signature bytes */
      && fwrite(INDEX_HEADER, str_len(INDEX_HEADER), 1, fp)) {
        /* open succeeded */

        i = INDEX_FILE_FORMAT_VERSION;
        WRITE_MEMBER(fp, i, uint32_t);
        WRITE_MEMBER(fp, idx->flags, uint8_t);

        WRITE_MEMBER(fp, idx->repos, uint32_t);
        WRITE_MEMBER(fp, idx->vectors, uint32_t);
        WRITE_MEMBER(fp, idx->vocabs, uint32_t);
        WRITE_MEMBER(fp, idx->repos_pos, uint32_t);

        WRITE_MEMBER(fp, idx->stats.terms_high, uint32_t);
        WRITE_MEMBER(fp, idx->stats.terms_low, uint32_t);
        WRITE_MEMBER(fp, idx->stats.updates, uint32_t);

        WRITE_DOUBLE(fp, idx->stats.avg_weight);
        WRITE_DOUBLE(fp, idx->stats.avg_length);

        WRITE_DOUBLE(fp, idx->impact_stats.avg_f_t);
        WRITE_DOUBLE(fp, idx->impact_stats.slope);
        WRITE_MEMBER(fp, idx->impact_stats.quant_bits, uint32_t);
        WRITE_DOUBLE(fp, idx->impact_stats.w_qt_min);
        WRITE_DOUBLE(fp, idx->impact_stats.w_qt_max);

        WRITE_MEMBER(fp, idx->doc_order_vectors, uint32_t);
        WRITE_MEMBER(fp, idx->doc_order_word_pos_vectors, uint32_t);
        WRITE_MEMBER(fp, idx->impact_vectors, uint32_t);

        WRITE_MEMBER(fp, root_fileno, uint32_t);
        WRITE_MEMBER(fp, root_offset, uint32_t);

        WRITE_MEMBER(fp, terms, uint32_t);

        storagep_write(&idx->storage, buf);
        if (!fwrite(buf, storagep_size(), 1, fp)) {
            FAIL();
        }

        /* write config filename */
        if (idx->params.config) {
            i = str_len(idx->params.config);
        } else {
            i = 0;
        }
        WRITE_MEMBER(fp, i, uint32_t);
        if (i && !fwrite(idx->params.config, i, 1, fp)) {
            FAIL();
        }

        /* write set repository names */
        for (i = 0; i < idx->repos; i++) {
            int set;
            unsigned int len;

            if ((fdset_isset(idx->fd, idx->repos_type, i, &set) != FDSET_OK) 
              || (fdset_name(idx->fd, idx->repos_type, i, filename, 
                FILENAME_MAX, &len, &writeable))) {

                assert(!CRASH);
                FAIL();
            }

            if (set) {
                WRITE_MEMBER(fp, i, uint32_t);
                WRITE_MEMBER(fp, len, uint32_t);
                if (!fwrite(filename, len, 1, fp)) {
                    FAIL();
                }
            }
        }

        free(buf);
        fclose(fp);
        return 1;
    } else {
        assert(!CRASH);
        if (buf) {
            free(buf);
            if (fp) {
                fclose(fp);
            }
        }
        return 0;
    }
}
#undef FAIL

int index_commit_superblock(struct index *idx) {
    void *buf = NULL;
    unsigned int root_fileno = 0,
                 btree_size = 0;
    unsigned long int root_offset = 0;
    int ret = 1;
    enum docmap_ret dm_ret;

    if (idx->vocab) {
        iobtree_root(idx->vocab, &root_fileno, &root_offset);
        btree_size = iobtree_size(idx->vocab);
    }

    if (!ret 
      /* XXX: note that we don't try to flush btree if its NULL, so that the
       * (somewhat external) build process can use this function */
      || (idx->vocab && !iobtree_flush(idx->vocab))) {
        assert(!CRASH);
        return 0;
    }

    /* write docmap to disk */
    dm_ret = docmap_save(idx->map);
    if (dm_ret != DOCMAP_OK) {
        ERROR1("saving docmap: return value is '%d'", dm_ret);
        if (buf) {
            free(buf);
            buf = NULL;
        }
        assert(!CRASH);
        return 0;
    }

    /* write parameters to disk */
    if (index_params_write(idx, root_fileno, root_offset, btree_size)) {
        /* write succeeded */
    } else {
        assert(!CRASH);
        return 0;
    }
    return 1;
}

static int index_set_fd_types(struct index *idx, const char * name) {
    if (fdset_create_new_type(idx->fd, name, PARAMSUF, 1 /* write */,
          &idx->param_type) != FDSET_OK
      || fdset_create_new_type(idx->fd, name, VECTORSUF, 1,
          &idx->index_type) != FDSET_OK
      || fdset_create_new_type(idx->fd, name, REPOSSUF, 1, 
          &idx->repos_type) != FDSET_OK
      || fdset_create_new_type(idx->fd, name, VOCABSUF, 1, 
          &idx->vocab_type) != FDSET_OK
      || fdset_create_new_type(idx->fd, name, DOCMAPSUF, 1, 
          &idx->docmap_type) != FDSET_OK
      || fdset_create_new_type(idx->fd, name, "vtmp", 1, 
          &idx->vtmp_type) != FDSET_OK
      || fdset_create_new_type(idx->fd, name, "tmp", 1, 
          &idx->tmp_type) != FDSET_OK) {
        return -1;
    }
    return 0;
}

struct index *index_new(const char *name, const char *config, 
  unsigned int memory, int opts, struct index_new_opt *opt) {
    struct index *idx;
    int fd = -1;
    int retval;
    int bigendian = 0;
    enum docmap_ret dm_ret;
    FILE *fp;

#ifdef WORDS_BIGENDIAN
    bigendian = 1;
#endif

    if (opts & INDEX_NEW_ENDIAN) {
        bigendian = opt->bigendian;
    }

    /* note that we don't initialise a vocab here, because there is nothing to
     * search.  It should be initialised at construction time */

    /* XXX: first section here shared with index_load: factorise
       into common init function. */

    if (!(idx = malloc(sizeof(*idx)))) {
        ERROR("allocating memory for index object");
        return NULL;
    }

    /* set all pointers to NULL so we know what to free on failure */
    idx->flags = 0;
    idx->fd = NULL;
    idx->vocab = NULL;
    idx->map = NULL;
    idx->post = NULL;
    idx->settings = NULL;
    idx->repos = 0;
    idx->vectors = 0;
    idx->vocabs = 0;
    idx->merger = NULL;
    idx->sum = NULL;
    idx->istop = NULL;
    idx->qstop = NULL;
    idx->params.config = NULL;
    idx->params.tblsize = TABLESIZE;
    idx->params.parsebuf = PARSE_BUFFER;
    idx->impact_stats.avg_f_t = 0;
    idx->impact_stats.slope = 0;
    idx->impact_stats.quant_bits = 0;
    idx->impact_stats.w_qt_min = 0;
    idx->impact_stats.w_qt_max = 0;
    idx->doc_order_vectors = 0;
    idx->doc_order_word_pos_vectors = 1;
    idx->impact_vectors = 0;
    
    /* initialise stemming algorithm if requested */
    if (opts & INDEX_NEW_STEM) {
        if ((opt->stemmer == INDEX_STEM_PORTERS) 
          && (idx->stem 
            = stem_cache_new(stem_porters, NULL, 200))) {
            idx->flags |= INDEX_STEMMED_PORTERS;
        } else if ((opt->stemmer == INDEX_STEM_EDS)) {
            idx->flags |= INDEX_STEMMED_EDS;
            idx->stem = NULL;
        } else if ((opt->stemmer == INDEX_STEM_LIGHT)) {
            idx->flags |= INDEX_STEMMED_LIGHT;
            idx->stem = NULL;
        } else {
            /* unknown stemming algorithm or couldn't initialise stemmer */
            ERROR("initialising stemming algorithm");
            index_rm(idx);
            index_delete(idx);
            return NULL;
        }
    } else {
        idx->stem = NULL;
    }

    if (opts & INDEX_NEW_STOP) {
        idx->istop 
          = stop_new_file(index_stemmer(idx), idx->stem, opt->stop_file);
        if (idx->istop == NULL) {
            ERROR1("loading stop file %s", opt->stop_file);
            index_rm(idx);
            index_delete(idx);
            return NULL;
        }
    } else {
        idx->istop = NULL;
    }

    if (opts & INDEX_NEW_QSTOP) {
        if (!(opt->qstop_file 
            && (idx->qstop 
              = stop_new_file(index_stemmer(idx), idx->stem, opt->qstop_file)))
          && !(!opt->qstop_file
            && (idx->qstop 
              = stop_new_default(index_stemmer(idx), idx->stem)))) {

            ERROR1("loading query stop file %s", 
              opt->qstop_file ? opt->qstop_file : "(default)");
            index_rm(idx);
            index_delete(idx);
            return NULL;
        }
    }

    if (opts & INDEX_NEW_TABLESIZE) {
        idx->params.tblsize = opt->tablesize;
    }
    if (opts & INDEX_NEW_PARSEBUF) {
        idx->params.parsebuf = opt->parsebuf;
    }

    if ((idx->fd = fdset_new(0644, 1)) == NULL) {
        ERROR("creating fdset for new index");
        index_delete(idx);
        return NULL;
    }

    if (index_set_fd_types(idx, name) < 0) {
        ERROR("setting fdset type names");
        index_delete(idx);
        return NULL;
    }

    /* ensure we assigned different types */
    assert(idx->index_type != idx->tmp_type);
    assert(idx->repos_type != idx->tmp_type);
    assert(idx->index_type != idx->repos_type);

    if ((fd = fdset_create(idx->fd, idx->index_type, 0)) < 0) { 
        ERROR("creating the new index file");
        index_delete(idx);
        return NULL;
    } 

    /* get/apply parameters */
    if (storagep_defaults(&idx->storage, fd) < 0) {
        ERROR("determining maximum file size");
        index_rm(idx);
        index_delete(idx);
        return NULL;
    }
    idx->storage.bigendian = bigendian; 
    retval = fdset_unpin(idx->fd, idx->index_type, 0, fd);
    assert(retval == FDSET_OK);
    fd = -1;

    /* process storage-related options */
    if (opts & INDEX_NEW_MAXFILESIZE) {
        idx->storage.max_filesize = opt->maxfilesize;
    }
    if (opts & INDEX_NEW_VOCAB) {
        idx->storage.vocab_lsize = opt->vocab_size;
    }

    idx->params.memory = memory;
    if (config) {
        if ((idx->params.config = str_dup(config)) == NULL) {
            ERROR1("duplicating config file name '%s'", config);
            index_rm(idx);
            index_delete(idx);
            return NULL;
        }
    }

    if ((idx->map = docmap_new(idx->fd, idx->docmap_type, 
        idx->storage.pagesize,
        0, idx->storage.max_filesize, 0, &dm_ret)) == NULL) {

        ERROR1("creating docmap: error code '%d'", dm_ret);
        index_rm(idx);
        index_delete(idx);
        return NULL;
    }
    if (!(idx->post = postings_new(idx->params.tblsize, index_stemmer(idx), 
        idx->stem, idx->istop))) {

        ERROR("creating postings");
        index_rm(idx);
        index_delete(idx);
        return NULL;
    }
    if (idx->params.config) {
        if ((fp = fopen((const char *) idx->params.config, "rb"))
          && (idx->settings = psettings_read(fp, PSETTINGS_ATTR_INDEX))) {
            fclose(fp);
        } else {
            if (fp) {
                fclose(fp);
            }
            ERROR1("loading parser settings from '%s'", idx->params.config);
            index_rm(idx);
            index_delete(idx);
            return NULL;
        }
    } else if (!(idx->settings 
      = psettings_new_default(PSETTINGS_ATTR_INDEX))) {
        ERROR("loading default parser settings");
        index_rm(idx);
        index_delete(idx);
        return NULL;
    }

    if ((idx->merger 
      = pyramid_new(idx->fd, idx->tmp_type, idx->index_type, idx->vocab_type,
          PYRAMID_WIDTH, &idx->storage, NULL, NULL)) 
        == NULL) {

        ERROR("creating merger pyramid");
        index_rm(idx);
        index_delete(idx);
        return NULL;
    }
    if ((idx->sum = summarise_new(idx)) == NULL) {
        ERROR("creating summarisation object");
        index_rm(idx);
        index_delete(idx);
        return NULL;
    }

    idx->flags |= INDEX_SOURCE;

    idx->vectors = 0;
    idx->repos = 0;
    idx->repos_pos = 0;
    idx->stats.updates = 0;
    idx->stats.terms_high = 0;
    idx->stats.terms_low = 0;
    idx->stats.avg_length = 0.0;
    idx->stats.avg_weight = 0.0;

    return idx;
}

struct index *index_load(const char *name, unsigned int memory, int opts, 
  struct index_load_opt *opt) {
    struct index *idx = NULL;
    void *buf = NULL;
    unsigned int fd_fileno = -1,
                 root_fileno,
                 terms,
                 vfileno,
                 vsize;
    unsigned long int voffset,
                      root_offset;
    enum docmap_ret dm_ret;
    enum docmap_cache dm_cache = DOCMAP_CACHE_WORDS | DOCMAP_CACHE_TRECNO;
    FILE *fp;

    if (!(idx = malloc(sizeof(*idx)))) {
        ERROR("allocating memory for index object");
        return NULL;
    }

    /* set all pointers to NULL so we know what to free on failure */
    idx->fd = NULL; 
    idx->vocab = NULL; 
    idx->map = NULL; 
    idx->post = NULL;
    idx->settings = NULL; 
    idx->istop = NULL; 
    idx->qstop = NULL; 
    idx->sum = NULL; 
    idx->repos = 0; 
    idx->vectors = 0; 
    idx->merger = NULL; 
    idx->params.config = NULL;
    idx->params.tblsize = TABLESIZE;
    idx->params.parsebuf = PARSE_BUFFER;
    idx->stem = NULL;

    if (!(idx->fd = fdset_new(0644, 1))) {
        ERROR("creating fdset for index");
        index_delete(idx);
        return NULL;
    }
    /* need to initialise a fdset before getting parameters */
    if (index_set_fd_types(idx, name) < 0) {
        ERROR("setting fdset type names");
        index_delete(idx);
        return NULL;
    }

    fd_fileno = 0;

    /* get parameters from disk */
    if (!(index_params_read(idx, &root_fileno, &root_offset, &terms, 
          &vfileno, &voffset, &vsize,
          opts & INDEX_LOAD_IGNORE_VERSION))) {
        /* function prints error */
        index_delete(idx);
        free(buf);
        return NULL;
    }
    assert(idx->vocabs);
    idx->params.memory = memory;

    /* process storage-related options */
    if (opts & INDEX_LOAD_VOCAB) {
        idx->storage.vocab_lsize = opt->vocab_size;
    }
    if (opts & INDEX_LOAD_MAXFLIST) {
        idx->storage.file_lsize = opt->maxflist_size;
    }
    if (opts & INDEX_LOAD_TABLESIZE) {
        idx->params.tblsize = opt->tablesize;
    }
    if (opts & INDEX_LOAD_PARSEBUF) {
        idx->params.parsebuf = opt->parsebuf;
    }
    if (opts & INDEX_LOAD_DOCMAP_CACHE) {
        dm_cache = opt->docmap_cache;
    }

    /* initialise stemming algorithm if required */
    if (idx->flags & INDEX_STEMMED) {
        /* XXX: note that we currently assume porters stemmer */
        if ((idx->flags & INDEX_STEMMED_LIGHT) == INDEX_STEMMED_LIGHT) {
            idx->stem = NULL;
        } else if ((idx->flags & INDEX_STEMMED_PORTERS) 
          && (idx->stem = stem_cache_new(stem_porters, NULL, 200))) {
            /* succeeded, do nothing */
        } else if (idx->flags & INDEX_STEMMED_EDS) {
            idx->stem = NULL;
        } else {
            /* couldn't initialise stemmer */
            index_delete(idx);
            return NULL;
        }
    } else {
        idx->stem = NULL;
    }

    if (opts & INDEX_LOAD_QSTOP) {
        if (!(opt->qstop_file 
            && (idx->qstop 
              = stop_new_file(index_stemmer(idx), idx->stem, opt->qstop_file)))
          && !(!opt->qstop_file
            && (idx->qstop 
              = stop_new_default(index_stemmer(idx), idx->stem)))) {

            ERROR1("loading query stop file %s", 
              opt->qstop_file ? opt->qstop_file : "(default)");
            index_rm(idx);
            index_delete(idx);
            return NULL;
        }
    }

    /* read the docmap off of disk */
    if ((idx->map = docmap_load(idx->fd, idx->docmap_type, 
          idx->storage.pagesize, 0, idx->storage.max_filesize, 
          dm_cache, &dm_ret)) 
      == NULL) {

        ERROR1("loading docmap: error is '%s'", docmap_strerror(dm_ret));
        index_delete(idx);
        return NULL;
    }

    /* now initialise the rest of the members */
    if (!(idx->post = postings_new(idx->params.tblsize, index_stemmer(idx), 
        idx->stem, NULL))) {

        ERROR("creating postings");
        index_delete(idx);
        return NULL;
    }

    if (idx->params.config) {
        if ((fp = fopen((const char *) idx->params.config, "rb"))
          && (idx->settings = psettings_read(fp, PSETTINGS_ATTR_INDEX))) {
            fclose(fp);
        } else {
            if (fp) {
                fclose(fp);
            }
            ERROR1("loading parser settings from %s", idx->params.config);
            index_delete(idx);
            return NULL;
        }
    } else if (!(idx->settings 
      = psettings_new_default(PSETTINGS_ATTR_INDEX))) {
        ERROR("loading default parser settings");
        index_delete(idx);
        return NULL;
    }

    if ((idx->vocab = iobtree_load_quick(idx->storage.pagesize, 
            idx->storage.btleaf_strategy, idx->storage.btnode_strategy, 
            NULL, idx->fd, idx->vocab_type, root_fileno, 
            root_offset, terms)) == NULL) {
        ERROR("quick-loading btree");
        index_delete(idx);
        return NULL;
    }
    if ((idx->sum = summarise_new(idx)) == NULL) {
        ERROR("creating summarisation object");
        index_rm(idx);
        index_delete(idx);
        return NULL;
    }

    return idx;
}

int index_rm(struct index *idx) {
    char buf[FILENAME_MAX + 1];
    unsigned int i,
                 len;
    int write;
    int error = 0;
    int last_errno = 0;

    fdset_unlink(idx->fd, idx->param_type, 0);

    for (i = 0; i < idx->repos; i++) {
        if ((fdset_name(idx->fd, idx->repos_type, i, buf, FILENAME_MAX, &len, 
            &write) == FDSET_OK) && (len <= FILENAME_MAX) && (write)) {
            if (unlink((char *) buf) < 0) {
                ERROR1("Error trying to unlink repository file %s", buf);
                error = 1;
                last_errno = errno;
            }
        }
    }

    for (i = 0; i < idx->vocabs; i++) {
        if (fdset_unlink(idx->fd, idx->vocab_type, i) != FDSET_OK) {
            ERROR1("Error trying to unlink vocab file %u", i);
            error = 1;
            last_errno = errno;
        }
    }

    for (i = 0; fdset_unlink(idx->fd, idx->docmap_type, i) == FDSET_OK; i++) ;

    /* index_new creates a single vector file to find out maximum file
       size, but this doesn't get added to idx->vectors until actually
       used in the final merge.  So even if idx->vectors is 0, we still
       attempt to delete the first index file, and don't stress if it
       doesn't work. */
    for (i = 0; i < idx->vectors || i == 0; i++) {
        if ((fdset_name(idx->fd, idx->index_type, i, buf, FILENAME_MAX, &len, 
            &write) == FDSET_OK) && (len <= FILENAME_MAX) && (write)) {
            if (unlink((char *) buf) < 0) {
                if (i == 0 && idx->vectors == 0 && errno == ENOENT) {
                    errno = 0;
                } else {
                    ERROR1("Error trying to unlink index file %s", buf);
                    error = 1;
                    last_errno = errno;
                }
            }
        }
    }
    
    /* NB: we rely on sections of code that use temporary files to
       remove them in case of failure.  Of course, this won't work
       for signal handlers... */

    if (error)
        return (errno ? -errno : -EINVAL);
    else
        return 0;
}

int index_expensive_stats(const struct index *idx, 
  struct index_expensive_stats *stats) {
    unsigned int state[3] = {0, 0, 0};
    void *data;
    unsigned int datalen,
                 tmp,
                 terms;
    struct vocab_vector vv;
    const char *term;

    stats->avg_weight = idx->stats.avg_weight;
    if (docmap_avg_words(idx->map, &stats->avg_words) != DOCMAP_OK) {
        return 0;
    }
    stats->avg_length = idx->stats.avg_length;

    stats->vocab_pages = iobtree_pages(idx->vocab, &stats->vocab_leaves, NULL);
    stats->pagesize = iobtree_pagesize(idx->vocab);

    /* iterate over all vocabulary items to get counts of list sizes */
    stats->vectors = stats->vectors_files 
      = stats->vectors_vocab = stats->allocated_files = 0;
    stats->vocab_info = 0;
    stats->vocab_structure = 0;
    terms = 0;
    while ((term 
      = iobtree_next_term(idx->vocab, state, &tmp, &data, &datalen))) {
        unsigned int bytes;
        unsigned int info;
        struct vec v;
        enum vocab_ret vret;

        terms++;
        v.pos = data;
        v.end = v.pos + datalen;

        while ((vret = vocab_decode(&vv, &v)) == VOCAB_OK) {
            bytes = vocab_len(&vv);
            assert(bytes);
            stats->vectors += vv.size;
            info = vec_vbyte_len(vv.header.doc.docs) 
              + vec_vbyte_len(vv.header.doc.occurs) 
              + vec_vbyte_len(vv.header.doc.last) + vec_vbyte_len(vv.size);
            stats->vocab_info += vec_vbyte_len(tmp) + tmp + info;
            assert(bytes > info);
            stats->vocab_structure += bytes - info;

            switch (vv.location) {
            case VOCAB_LOCATION_VOCAB: 
                stats->vectors_vocab += vv.size; 
                break;

            case VOCAB_LOCATION_FILE: 
                stats->vectors_files += vv.size; 
                stats->allocated_files += vv.loc.file.capacity; 
                break;

            default: assert(0);
            }
        }

        if (vret != VOCAB_END) {
            return 0;
        }
    }
    assert(terms == iobtree_size(idx->vocab));

    return 1;
}

int index_stats(const struct index *idx, struct index_stats *stats) {
    stats->terms_high = idx->stats.terms_high;
    stats->terms_low = idx->stats.terms_low;
    stats->dterms = iobtree_size(idx->vocab);
    stats->docs = docmap_entries(idx->map);
    stats->maxtermlen = idx->storage.max_termlen;

    stats->vocab_listsize = idx->storage.vocab_lsize;

    stats->updates = idx->stats.updates;
    stats->tablesize = idx->params.tblsize;
    stats->parsebuf = idx->params.parsebuf;
    
    stats->doc_order_vectors = idx->doc_order_vectors;
    stats->doc_order_word_pos_vectors = idx->doc_order_word_pos_vectors;
    stats->impact_vectors = idx->impact_vectors;
    stats->sorted = idx->flags & INDEX_SORTED;

    return 1;
}

enum stream_ret index_stream_read(struct stream *instream, int fd, 
  char *buf, unsigned int bufsize) {
    enum stream_ret sret;
    ssize_t readlen;

    do {
        sret = stream(instream);
        switch (sret) {
        case STREAM_OK:
        case STREAM_END:
            return sret;
            break;

        case STREAM_INPUT:
            instream->next_in = buf;
            if ((readlen = read(fd, buf, bufsize)) > 0) {
                instream->avail_in = readlen;
            } else if (!readlen) {
                stream_flush(instream, STREAM_FLUSH_FINISH);
            } else {
                return STREAM_EINVAL;
            }
            break;

        default:
            return sret;
        }
    } while (1);
}

unsigned int index_retrieve(const struct index *idx, unsigned long int docno,
  unsigned long int dst_offset, void *dst, unsigned int dstsize) {
    off_t offset,
          curroffset;
    unsigned int source,
                 bytes, 
                 rlen,
                 readb;
    enum docmap_flag flags;
    enum mime_types type;
    int fd;
    struct stream *instream = NULL;
    struct stream_filter *gunzipfilter = NULL;
    char *buf = NULL,
         *cdst = dst;

    if (!dstsize) {
        return 0;
    }

    if (docmap_get_location(idx->map, docno, &source, &offset, &bytes,
          &type, &flags) == DOCMAP_OK) {

        if (dst_offset >= bytes) {
            return 0;
        }

        if (dst_offset + dstsize >= bytes) {
            dstsize = bytes - dst_offset;
        }

        if (flags & DOCMAP_COMPRESSED) {
            /* read from compressed repository */
            if ((fd = fdset_pin(idx->fd, idx->repos_type, source, 0, SEEK_SET))
              && (instream = stream_new()) 
              && (gunzipfilter 
                = (struct stream_filter *) gunzipfilter_new(BUFSIZ))
              && (buf = malloc(BUFSIZ))) {
                /* read compressed repository until we get to where we're 
                 * after */
                stream_filter_push(instream, gunzipfilter);

                for (curroffset = 0; 
                  curroffset + instream->avail_out < offset + dst_offset; ) {

                    curroffset += instream->avail_out;
                    if (index_stream_read(instream, fd, buf, BUFSIZ) 
                      != STREAM_OK) {
                        /* error */
                        fdset_unpin(idx->fd, idx->repos_type, source, fd);
                        stream_delete(instream);
                        free(buf);
                        return 0;
                    }
                }

                /* ok, should now be at required position */
                if (curroffset + instream->avail_out 
                  >= offset + dst_offset + dstsize) {
                    /* whole of required portion is in buffer */
                    memcpy(dst, 
                      instream->curr_out + offset + dst_offset - curroffset, 
                      dstsize);
                    fdset_unpin(idx->fd, idx->repos_type, source, fd);
                    stream_delete(instream);
                    free(buf);
                    return dstsize;
                } else {
                    unsigned int lcloffset = offset + dst_offset - curroffset;

                    /* whole of required portion isn't available, copy first
                     * chunk */
                    readb = instream->avail_out - lcloffset;
                    memcpy(dst, instream->curr_out + lcloffset, readb);
                    curroffset += readb;
                }

                /* move to next chunk */
                if (index_stream_read(instream, fd, buf, BUFSIZ) 
                  != STREAM_OK) {
                    /* error */
                    fdset_unpin(idx->fd, idx->repos_type, source, fd);
                    stream_delete(instream);
                    free(buf);
                    return 0;
                }

                /* copy entire bufferloads */
                while (readb + instream->avail_out < dstsize) {
                    memcpy(cdst + readb, instream->curr_out, 
                      instream->avail_out);
                    readb += instream->avail_out;

                    /* move to next chunk */
                    if (index_stream_read(instream, fd, buf, BUFSIZ) 
                      != STREAM_OK) {
                        /* error */
                        fdset_unpin(idx->fd, idx->repos_type, source, fd);
                        stream_delete(instream);
                        free(buf);
                        return 0;
                    }
                }

                /* copy last chunk in */
                memcpy(cdst + readb, instream->curr_out, dstsize - readb);
                fdset_unpin(idx->fd, idx->repos_type, source, fd);
                stream_delete(instream);
                free(buf);
                return dstsize;
            } else {
                if (fd >= 0) {
                    fdset_unpin(idx->fd, idx->repos_type, source, fd);
                }
                if (instream) {
                    stream_delete(instream);
                }
                if (gunzipfilter) {
                    gunzipfilter->deletefn(gunzipfilter);
                }
                if (buf) {
                    free(buf);
                }
            }
        } else {
            /* read from regular repository */
            if ((fd = fdset_pin(idx->fd, idx->repos_type, source, 
              offset + dst_offset, SEEK_SET)) >= 0) {
                rlen = read(fd, dst, dstsize);
                fdset_unpin(idx->fd, idx->repos_type, source, fd);
                return rlen;
            }
        }
    }

    return -1;
}

int index_load_stoplist(struct index *idx, void *src, 
  unsigned int (*fillbuf)(void *src, void *buf, unsigned int len)) {
    /* FIXME */
    fprintf(stderr, "load_stoplist called, but unimplemented\n");
    return 0;
}

void index_delete(struct index *idx) {
    /* first, save the docmap */
    if (idx->map) {
        docmap_save(idx->map);  /* XXX: should be in index_commit()? */
        docmap_delete(idx->map);
        idx->map = NULL;
    }

    if (idx->sum) {
        summarise_delete(idx->sum);
        idx->sum = NULL;
    }

    if (idx->merger) {
        pyramid_delete(idx->merger);
        idx->merger = NULL;
    }

    if (idx->vocab) {
        iobtree_delete(idx->vocab);
        idx->vocab = NULL;
    }

    if (idx->fd) {
        if (fdset_pinned(idx->fd)) {
            fprintf(stderr, "warning: exiting with %u pinned fds\n", 
              fdset_pinned(idx->fd));
        }
        fdset_delete(idx->fd);
        idx->fd = NULL;
    }

    if (idx->settings) {
        psettings_delete(idx->settings);
        idx->settings = NULL;
    }

    if (idx->post) {
        postings_delete(idx->post);
        idx->post = NULL;
    }

    if (idx->params.config) {
        free(idx->params.config);
        idx->params.config = NULL;
    }

    if (idx->stem) {
        stem_cache_delete(idx->stem);
        idx->stem = NULL;
    }
    if (idx->istop) {
        stop_delete(idx->istop);
        idx->istop = NULL;
    }
    if (idx->qstop) {
        stop_delete(idx->qstop);
        idx->qstop = NULL;
    }

    free(idx);
}

int index_retrieve_doc_aux(const struct index *idx, 
  unsigned long int docno, char * aux_buf, unsigned int aux_buf_len,
  unsigned int *aux_len) {
    enum docmap_ret dm_ret;

    dm_ret = docmap_get_trecno(idx->map, docno, aux_buf, aux_buf_len, aux_len);
    return dm_ret == DOCMAP_OK;
}

unsigned int index_retrieve_doc_bytes(const struct index *idx,
  unsigned long int docno) {
    enum docmap_ret dm_ret;
    unsigned int bytes;

    dm_ret = docmap_get_bytes(idx->map, docno, &bytes);
    if (dm_ret != DOCMAP_OK)
        return UINT_MAX;
    else
        return bytes;
}

int index_add(struct index *idx, const char *file, const char *mimetype,
  unsigned long int *docno, unsigned int *docs, 
  unsigned int opts, struct index_add_opt *opt, 
  unsigned int commitopts, struct index_commit_opt *commitopt) {
    void *parsebuf = NULL, 
         *dumpbuf = NULL;
    struct makeindex mispace,
                     *mi = NULL;
    int infd = -1,
        outfd;
    off_t bytes_read = 0,
          last_pos = 0,
          curr_pos = 0;
    unsigned int i,
                 multiple_compression = 0;
    ssize_t readlen;
    enum makeindex_ret miret;
    unsigned int accbuf = idx->params.memory,
                 accdoc = 0,
                 dumpbufsz = DUMP_BUFFER;
    struct stream *instream = NULL;
    enum stream_ret sret;
    struct stream_filter *filter = NULL;
    enum docmap_ret dm_ret;
    unsigned long int docno_out;
    const char *aux_docno;
    enum mime_types mtype,
                    comptype;
    enum docmap_flag flags = DOCMAP_NO_FLAGS;

    TIMINGS_DECL();

    TIMINGS_START();

    if (opts & INDEX_ADD_ACCBUF) {
        accbuf = opt->accbuf;
    }
    if (opts & INDEX_ADD_ACCDOC) {
        accdoc = opt->accdoc;
    }
    if (commitopts & INDEX_COMMIT_DUMPBUF) {
        dumpbufsz = commitopt->dumpbuf;
    }

#define FAIL()                                                                \
    if (1) {                                                                  \
        assert(!CRASH);                                                       \
        if (instream) {                                                       \
            stream_delete(instream);                                          \
        }                                                                     \
        if (filter) {                                                         \
            filter->deletefn(filter);                                         \
        }                                                                     \
        if (mi) {                                                             \
            makeindex_delete(mi);                                             \
        }                                                                     \
        if (dumpbuf) {                                                        \
            free(dumpbuf);                                                    \
        }                                                                     \
        if (parsebuf) {                                                       \
            free(parsebuf);                                                   \
        }                                                                     \
        if (infd >= 0) {                                                      \
            fdset_unpin(idx->fd, idx->repos_type, idx->repos, infd);          \
        }                                                                     \
        return 0;                                                             \
    } else

    if ((parsebuf = malloc(idx->params.parsebuf)) 
      && (fdset_set_fd_name(idx->fd, idx->repos_type, idx->repos, file,
          str_len(file), 0) == FDSET_OK)
      && ((infd = fdset_pin(idx->fd, idx->repos_type, idx->repos, 0, 
          SEEK_SET)) >= 0)
      && (instream = stream_new())
      && (filter = (struct stream_filter *) detectfilter_new(BUFSIZ, 0))) {
        int ret;

        /* push detection filter onto stream to auto-detect gzip encoding */
        stream_filter_push(instream, filter);
        filter = NULL;
 
        /* read first chunk of document via stream */
        do {
            sret = stream(instream);

            switch (sret) {
            case STREAM_OK:
                bytes_read += instream->avail_out;
                break;

            case STREAM_END:
                /* reached eof */
                sret = STREAM_OK;
                break;

            case STREAM_INPUT:
                assert(instream->avail_in == 0);
                instream->next_in = parsebuf;
                if ((readlen = read(infd, parsebuf, idx->params.parsebuf)) 
                  > 0) {
                    instream->avail_in = (unsigned int) readlen;
                } else if (!readlen) {
                    /* flush stream */
                    stream_flush(instream, STREAM_FLUSH_FINISH);
                } else {
                    FAIL();
                }
                break;

            default:
                /* error of some type */
                FAIL();
            }
        } while (sret != STREAM_OK);

        /* figure out what compression was detected */
        comptype = MIME_TYPE_UNKNOWN_UNKNOWN;
        multiple_compression = 0;
        for (i = stream_filters(instream) - 1; i != -1; i--) {
            const char *id;

            if (stream_filter(instream, i, &id) == STREAM_OK) {
                if (!str_cmp(id, "gunzip")) {
                    if (comptype == MIME_TYPE_UNKNOWN_UNKNOWN) {
                        multiple_compression = 1;
                    }
                    flags = DOCMAP_COMPRESSED;
                    comptype = MIME_TYPE_APPLICATION_X_GZIP;
                }
            } else {
                assert("can't get here" && 0);
                FAIL();
            }
        }

        /* now detect the type of file if necessary */
        if (mimetype) {
            if ((mtype = mime_type(mimetype)) == MIME_TYPE_UNKNOWN_UNKNOWN) {
                ERROR1("failed to extract MIME type from '%s'", mimetype);
                FAIL();
            }
        } else {
            /* detect from the start of the file */
            mtype = mime_content_guess(instream->curr_out, instream->avail_out);
        }
        opt->detected_type = mime_string(mtype);

        /* can now initialise makeindex module */
        if (((miret = makeindex_new(&mispace, idx->settings, 
            idx->storage.max_termlen, mtype)) == MAKEINDEX_OK)) {
            mi = &mispace;
            *docno = mi->docs = docmap_entries(idx->map);
            mi->post = idx->post;
            *docs = 0;
            mi->next_in = instream->curr_out;
            mi->avail_in = instream->avail_out;
        } else {
            FAIL();
        }

        if (!psettings_self_id(idx->settings, mtype)) {
            /* set docno within makeindex object */
            makeindex_append_docno(mi, "file://");
            makeindex_append_docno(mi, file);
        }

        do {
            switch ((miret = makeindex(mi))) {
            case MAKEINDEX_ENDDOC:
                assert(!postings_needs_update(idx->post));

                /* add document to the docmap */

                /* XXX: + (mi->avail_in == 0) is needed to push pos 
                 * over '>' in end tag.  This is dodgy because we 
                 * may not have actually received an end tag (can 
                 * get EOF instead) and the end tag may be formatted
                 * other than just </doc>.  Proper solution is to
                 * make the parser explicitly return end tags, so
                 * you know when the end tag has finished */
                curr_pos = bytes_read - mi->avail_in 
                  - makeindex_buffered(mi) + (mi->avail_in != 0);

                (*docs)++;
                aux_docno = makeindex_docno(mi);
                mtype = makeindex_type(mi);
                dm_ret = docmap_add(idx->map, idx->repos, last_pos, 
                  curr_pos - last_pos, flags, 
                  mi->stats.terms, mi->stats.distinct, 
                  mi->stats.weight, aux_docno, strlen(aux_docno), mtype,
                  &docno_out);
                if (dm_ret != DOCMAP_OK) {
                    ERROR1("error on docmap_add: %s", 
                      docmap_strerror(dm_ret));
                    FAIL();
                }
                assert(docno_out == mi->docs - 1);
                last_pos = curr_pos;

                /* check if we need to dump the postings */
                if ((postings_memsize(idx->post) >= accbuf) 
                  && (postings_documents(idx->post) >= accdoc)) {

                    TIMINGS_END("parsing");
                    TIMINGS_START();

                    if (!(idx->flags & INDEX_BUILT)
                        /* index is in construction, add to merger pyramid */
                      && (dumpbuf || (dumpbuf = malloc(dumpbufsz)))
                      && ((outfd = pyramid_pin_next(idx->merger)) >= 0)
                      && (postings_dump(idx->post, dumpbuf, dumpbufsz, outfd))
                      && (pyramid_unpin_next(idx->merger, outfd) == PYRAMID_OK)
                      && (pyramid_add_file(idx->merger, 1, dumpbuf, dumpbufsz)
                        == PYRAMID_OK)) {
                        /* do nothing, dumping succeeded */

                    } else if ((idx->flags & INDEX_BUILT) 
                      /* need to do an update */
                      && index_commit_internal(idx, commitopts, commitopt, 
                          opts & ~INDEX_ADD_FLUSH, opt)) {

                        /* update succeeded */
                    } else {
                        /* error */
                        FAIL();
                    }
                    TIMINGS_END("dumping");
                    TIMINGS_START();
                }
                break;

            case MAKEINDEX_EOF:
                /* check if we need to commit changes */
                if (opts & INDEX_ADD_FLUSH) {
                    TIMINGS_END("parsing");
                    TIMINGS_START();

                    if (!(idx->flags & INDEX_BUILT)
                        /* index is in construction, add to merger pyramid */
                      && (dumpbuf || (dumpbuf = malloc(dumpbufsz)))
                      && ((outfd = pyramid_pin_next(idx->merger)) >= 0)
                      && (postings_dump(idx->post, dumpbuf, dumpbufsz, outfd))
                      && (pyramid_unpin_next(idx->merger, outfd) == PYRAMID_OK)
                      && (pyramid_add_file(idx->merger, 1, dumpbuf, dumpbufsz)
                        == PYRAMID_OK)) {
                        /* do nothing, dumping succeeded */
                    } else if ((idx->flags & INDEX_BUILT) 
                      /* need to do an update */
                      && index_commit_internal(idx, commitopts, commitopt, 
                          opts & ~INDEX_ADD_FLUSH, opt)) {

                        /* update succeeded */
                    } else {
                        /* error */
                        FAIL();
                    }
                    TIMINGS_END("dumping");
                    TIMINGS_START();
                }
                break;
 
            case MAKEINDEX_INPUT:
                /* read next chunk via stream */
                assert(mi->avail_in == 0);
                do {
                    sret = stream(instream);

                    switch (sret) {
                    case STREAM_OK:
                        mi->next_in = instream->curr_out;
                        mi->avail_in = instream->avail_out;
                        bytes_read += instream->avail_out;
                        break;

                    case STREAM_END:
                        /* reached eof */
                        makeindex_eof(mi);
                        sret = STREAM_OK;
                        break;

                    case STREAM_INPUT:
                        assert(instream->avail_in == 0);
                        instream->next_in = parsebuf;
                        if ((readlen 
                          = read(infd, parsebuf, idx->params.parsebuf)) > 0) {
                            instream->avail_in = (unsigned int) readlen;
                        } else if (!readlen) {
                            /* flush stream */
                            stream_flush(instream, STREAM_FLUSH_FINISH);
                        } else {
                            FAIL();
                        }
                        break;

                    default:
                        /* error of some type */
                        FAIL();
                    }
                } while (sret != STREAM_OK);
                break;

            default:
                /* error */
                FAIL();
            }
        } while (miret != MAKEINDEX_EOF);

        ret = fdset_unpin(idx->fd, idx->repos_type, idx->repos, infd);
        assert(ret == FDSET_OK);
        free(parsebuf);
        if (dumpbuf) {
            free(dumpbuf);
        }
        assert(mi->docs == docmap_entries(idx->map));
        makeindex_delete(mi);
        stream_delete(instream);
        idx->repos++;

        return 1;
    } else {
        FAIL();
    }
#undef FAIL
}

int index_construct(struct index *idx, int opts, struct index_commit_opt *opt) {
    void *dumpbuf;
    unsigned int dumpbufsz = idx->params.memory;
    int outfd;
    unsigned int root_fileno;
    unsigned long int root_offset,
                      dterms;

    if (opts & INDEX_COMMIT_DUMPBUF) {
        dumpbufsz = opt->dumpbuf;
    }

    /* can allocate all of our memory to construction */
    if (!(dumpbuf = malloc(dumpbufsz)) && !(errno = 0) 
      && (dumpbufsz = DUMP_BUFFER) && !(dumpbuf = malloc(dumpbufsz))) {
        assert(!CRASH);
        return 0;
    }

    /* perform last dump if necessary */
    assert(!postings_needs_update(idx->post));
    if (postings_size(idx->post)) {
        if (((outfd = pyramid_pin_next(idx->merger)) >= 0) 
          && postings_dump(idx->post, dumpbuf, dumpbufsz, outfd)
          && (pyramid_unpin_next(idx->merger, outfd) == PYRAMID_OK)
          && (pyramid_add_file(idx->merger, 0, dumpbuf, dumpbufsz) 
            == PYRAMID_OK)) {
            /* dumping succeeded */
        } else {
            /* fail */
            assert(!CRASH);
            free(dumpbuf);
            return 0;
        }
    }

    if (docmap_entries(idx->map) == 0) {
        ERROR("no documents found!  Perhaps the input was empty, or "
          "not in the expected format?");
        free(dumpbuf);
        return 0;
    }

    /* perform final merge */

    if ((pyramid_merge(idx->merger, &idx->vectors, &idx->vocabs, &dterms, 
        &idx->stats.terms_high, &idx->stats.terms_low, &root_fileno, 
        &root_offset, dumpbuf, dumpbufsz) == PYRAMID_OK)
      && (idx->vocab = iobtree_load_quick(idx->storage.pagesize, 
          idx->storage.btleaf_strategy, idx->storage.btnode_strategy, 
          NULL, idx->fd, idx->vocab_type, root_fileno, root_offset, dterms))) {

        /* succeeded, cleanup */
        pyramid_delete(idx->merger);
        idx->merger = NULL;
        idx->flags |= INDEX_BUILT;
        idx->flags |= INDEX_SORTED;
        free(dumpbuf);
        return 1;
    } else {
        /* fail */
        assert(!CRASH);
        free(dumpbuf);
        return 0;
    }
}

/* small internal function to update stats during commit */
static int stat_update(struct index *idx) {
    unsigned int terms = postings_total_terms(idx->post);

    if (idx->stats.terms_low + terms < idx->stats.terms_low) {
        idx->stats.terms_high++;
    }
    idx->stats.terms_low += terms;

    if (docmap_avg_weight(idx->map, &idx->stats.avg_weight) != DOCMAP_OK 
      || docmap_avg_bytes(idx->map, &idx->stats.avg_length) != DOCMAP_OK)
        return 0;
    return 1;
}

int index_commit_internal(struct index *idx, 
  unsigned int opts, struct index_commit_opt *opt, 
  unsigned int addopts, struct index_add_opt *addopt) {

    if (docmap_avg_weight(idx->map, &idx->stats.avg_weight) != DOCMAP_OK 
      || docmap_avg_bytes(idx->map, &idx->stats.avg_length) != DOCMAP_OK)
        return 0;

    if (!(idx->flags & INDEX_BUILT)
        /* index is in construction, add to merger pyramid */
      && (idx->vocab == NULL)
      && index_construct(idx, opts, opt)
      && index_commit_superblock(idx)) {

        /* construction succeeded */
        return 1;

    } else if ((idx->flags & INDEX_BUILT)
      /* need to remerge index */
      && index_remerge(idx, opts, opt)
      && stat_update(idx)
      && index_commit_superblock(idx)) {
        assert(idx->flags & INDEX_SORTED);

        /* succeeded, clear accumulated postings */
        postings_clear(idx->post);
        idx->stats.updates++;
        return 1;

    } else {
        /* some sort of failure */
        assert(!CRASH);
        return 0;
    }
}

int index_commit(struct index *idx, 
  unsigned int opts, struct index_commit_opt *opt, 
  unsigned int addopts, struct index_add_opt *addopt) {
    int ret,
        altered = 0;
    enum impact_ret impact_ret;

    ret = index_commit_internal(idx, opts, opt, addopts, addopt);

    /* check to make sure that we haven't swapped tmp type for index type 
     * during updates (this is a bit of a hack :o( ), and swap them around 
     * if we have */
    if (ret && (idx->index_type > idx->tmp_type)) {
        char srcfile[FILENAME_MAX + 1];
        char dstfile[FILENAME_MAX + 1];
        unsigned int i,
                     len;
        int write;

        /* unfortunately, with the current state of filesystems, no matter
         * what we do we can't make moving a set of files atomic (if there is 
         * more than one), so if we fail somewhere in this code we'd probably 
         * have to rebuild the index from scratch */
        for (i = 0; i < idx->vectors; i++) {
            /* this is also not portable to win32, need to use different move
             * semantics there */
            if ((fdset_name(idx->fd, idx->tmp_type, i, dstfile, FILENAME_MAX, 
                &len, &write) == FDSET_OK)
              && (fdset_name(idx->fd, idx->index_type, i, srcfile, FILENAME_MAX,
                &len, &write) == FDSET_OK)

              /* perform move */
              && (unlink(dstfile), 1)        /* don't care if this fails */
              && (rename(srcfile, dstfile) == 0)) {
                /* move succeeded */
            } else {
                /* everything is potentially very screwed up */
                assert(!CRASH);
                return 0;
            }
        }

        /* swap types */
        len = idx->tmp_type;
        idx->tmp_type = idx->index_type;
        idx->index_type = len;
        altered = 1;
    }

    assert(idx->vocabs);
    assert(idx->vectors);
 
    /* do the same for vocab types */
    if (ret && (idx->vocab_type > idx->vtmp_type)) {
        char srcfile[FILENAME_MAX + 1];
        char dstfile[FILENAME_MAX + 1];
        unsigned int i,
                     len;
        int write;

        /* unfortunately, with the current state of filesystems, no matter
         * what we do we can't make moving a set of files atomic (if there is 
         * more than one), so if we fail somewhere in this code we'd probably 
         * have to rebuild the index from scratch */
        for (i = 0; i < idx->vocabs; i++) {
            /* this is also not portable to win32, need to use different move
             * semantics there */
            if ((fdset_name(idx->fd, idx->vtmp_type, i, dstfile, FILENAME_MAX, 
                &len, &write) == FDSET_OK)
              && (fdset_name(idx->fd, idx->vocab_type, i, srcfile, FILENAME_MAX,
                &len, &write) == FDSET_OK)

              /* perform move */
              && (unlink(dstfile), 1)        /* don't care if this fails */
              && (rename(srcfile, dstfile) == 0)) {
                /* move succeeded */
            } else {
                /* everything is potentially very screwed up */
                assert(!CRASH);
                return 0;
            }
        }

        /* swap types */
        len = idx->vtmp_type;
        idx->vtmp_type = idx->vocab_type;
        idx->vocab_type = len;
        altered = 1;
    }
   
    /* add impact ordered vectors to index if requested */
    if (opts & INDEX_COMMIT_ANH_IMPACTS) {
        impact_ret = impact_order_index(idx);
        altered = 1;
        if (impact_ret != IMPACT_OK) {
            ERROR("creating impact vectors");
            return 0;
        }
    }    

    /* recommit superblock */
    if (altered && index_commit_superblock(idx)) {
        /* succeeded */
    } else if (altered) {
        assert(!CRASH);
        return 0;
    }

    return ret;
}

