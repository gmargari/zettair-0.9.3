/* _index.h defines the index structure and declares internal functions 
 *
 * written nml 2003-06-02
 *
 */

/* can't use _INDEX_H because names with underscore are reserved for 
 * compiler */
#ifndef PRIVATE_INDEX_H
#define PRIVATE_INDEX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "firstinclude.h"

#include "index.h"
#include "storagep.h"
#include "stream.h"

enum index_flags {
    INDEX_BUILT = (1 << 0),             /* the index has been constructed */
    INDEX_SORTED = (1 << 1),            /* the index is sorted by vocabulary 
                                         * order (more or less) */
    INDEX_SOURCE = (1 << 2),            /* source text for the documents is 
                                         * available */
    INDEX_STEMMED = (3 << 3),           /* mask to tell whether index is 
                                         * stemmed */
    INDEX_STEMMED_PORTERS = (1 << 3),   /* whether its stemmed with porter's 
                                         * stemmer */
    INDEX_STEMMED_EDS = (1 << 4),       /* whether its stemmed with eds 
                                         * stemmer */
    INDEX_STEMMED_LIGHT = (3 << 3)      /* whether its stemmed with the light 
                                         * stemmer */
};

struct index {
    enum index_flags flags;             /* flags to indicate various global 
                                         * states of the index */

    unsigned int repos;                 /* number of repositories */
    unsigned int vectors;               /* number of vector files */
    unsigned int vocabs;                /* number of vocab files */
    unsigned long int repos_pos;        /* byte position of the last repos_fd */

    struct fdset *fd;                   /* dynamic file descriptor set for all 
                                         * index/repository files */
    struct iobtree *vocab;              /* vocabulary */
    struct docmap *map;                 /* document map */
    struct psettings *settings;         /* parser settings */
    struct summarise *sum;              /* summary producing object */

    struct stem_cache *stem;            /* stemmer cache (or NULL) */
    struct stop *istop;                 /* construction stoplist (or NULL) */
    struct stop *qstop;                 /* construction stoplist (or NULL) */

    /* construction stuff */
    struct postings *post;              /* accumulated in-memory postings */
    struct pyramid *merger;             /* pointers to dumped postings */
    struct storagep storage;            /* storage parameters */

    /* 'types' for accessing files through the fdset */
    unsigned int param_type;            /* index parameter file type */
    unsigned int index_type;            /* index fileset type */
    unsigned int repos_type;            /* repos fileset type */
    unsigned int tmp_type;              /* temporary fileset type */
    unsigned int vtmp_type;             /* temporary vocabulary fileset type */
    unsigned int vocab_type;            /* vocab btree fileset type */
    unsigned int docmap_type;           /* docmap fileset type */

    struct {
        unsigned int parsebuf;          /* size of parse buffer */
        unsigned int tblsize;           /* size of postings hashtable */
        unsigned int memory;            /* how much memory we can use */
        char *config;                   /* configuration file location (or 
                                         * NULL for built-in configuration) */
    } params;

    struct {
        unsigned int updates;           /* number of updates index has 
                                         * undergone */
        double avg_weight;              /* average document weight */
        double avg_length;              /* average document length in bytes */

        /* total number of term occurrances */
        unsigned int terms_high;        /* high word */
        unsigned int terms_low;         /* low word */
    } stats;

    struct imp_stats {
        double avg_f_t;          /* average term frequency */
        double slope;            /* used in normalising */
        unsigned int quant_bits; /* used in quantising */
        double w_qt_min;
        double w_qt_max;
    } impact_stats;              /* statistics required at query time for 
                                    impact ordered vectors */

    unsigned int doc_order_vectors; /* indicates if standard document order 
                                       vectors are in index */
    unsigned int doc_order_word_pos_vectors; /* indicates if document 
                                       ordered with word 
                                       positions vectors are in index */
    unsigned int impact_vectors;    /* indicates if impact ordered vectors 
                                       are in index */
};

/* internal function to merge the current postings into the index */
int index_remerge(struct index *idx, unsigned int commitopts, 
  struct index_commit_opt *commitopt);

/* commit the superblock of an index to disk.  Only here for usage
 * of programs bypassing the normal building mechanism, such as 
 * partitioning programs. */
int index_commit_superblock(struct index *idx);

/* internal function to atomically perform a read */
ssize_t index_atomic_read(int fd, void *buf, unsigned int size);

/* internal function to atomically perform a write */
ssize_t index_atomic_write(int fd, void *buf, unsigned int size);

/* write the header block of the index to disk. */
int index_commit_superblock(struct index *idx);

/* exactly the same as index_commit, except doesn't bugger around with restoring
 * indexes to exact on-disk representation */
int index_commit_internal(struct index *idx, unsigned int opts, 
  struct index_commit_opt *opt,
  unsigned int addopts, struct index_add_opt *addopt);

/* function to return the stemming function used by an index */
void (*index_stemmer(struct index *idx))(void *, char *);

/* utility function to read into a stream from a file/buffer */
enum stream_ret index_stream_read(struct stream *instream, int fd, 
  char *buf, unsigned int bufsize);

#ifdef __cplusplus
}
#endif

#endif

