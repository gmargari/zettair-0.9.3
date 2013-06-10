/* index.h declares high level operations for a simple, non-distributed search 
 * engine index
 *
 * written nml 2003-04-14
 *
 */

#ifndef INDEX_H
#define INDEX_H

#ifdef __cplusplus
extern "C" {
#endif

/* FIXME instead of these fixed-length buffers, there should be an
   index_results structure that holds the individual results and a pool
   of memory used for these string fields. */
#define INDEX_SUMMARYLEN 350        /* Maximum size of document summary */
#define INDEX_TITLELEN 50           /* Maximum size of document title */
#define INDEX_AUXILIARYLEN 150      /* Maximum size of auxiliary fields */

enum index_summary_type {
    INDEX_SUMMARISE_NONE = 0, /* no summary */
    INDEX_SUMMARISE_PLAIN = 1, /* summarise without highlighting */
    INDEX_SUMMARISE_TAG = 2, /* summarise using bold tags to highlight */
    INDEX_SUMMARISE_CAPITALISE = 3 /* summarise using capitalisation */
};

struct index;
extern const char *index_doctype_names[];

/* FIXME: this should really go elsewhere, but where? */
enum index_doctype {
    INDEX_DOCTYPE_ERR = 0,
    INDEX_DOCTYPE_HTML = 1,
    INDEX_DOCTYPE_TREC = 2,
    INDEX_DOCTYPE_INEX = 3,
    INDEX_DOCTYPE_LAST = 4    /* placeholder entry to indicate end of list */
};

enum index_stem {
    INDEX_STEM_NONE = 0,            /* no stemming algorithm */
    INDEX_STEM_PORTERS = 1,         /* use Porter's stemming algorithm */
    INDEX_STEM_EDS = 2,             /* use 'eds' stemming algorithm */
    INDEX_STEM_LIGHT = 3            /* use light stemming algorithm */
};

/* options to be passed to index_new */
enum index_new_opts {
    INDEX_NEW_NOOPT = 0,            /* pass this to indicate no options */
    INDEX_NEW_VOCAB = (1 << 0),     /* option to create an index with postings 
                                     * of size less than the size you specify 
                                     * (as an unsigned int) in the 
                                     * vocabulary */
    INDEX_NEW_STEM = (1 << 4),      /* use stemming algorithm (passed as 
                                     * stemmer) to stem the index */
    INDEX_NEW_MAXFILESIZE           /* set maximum filesize created */
      = (1 << 5),
    INDEX_NEW_ENDIAN = (1 << 6),    /* set the endian-ness of the generated 
                                     * index (supply bigendian, which is 0 for
                                     * little-endian and non-zero for 
                                     * bigendian) */
    INDEX_NEW_STOP = (1 << 8),      /* stop at indexing time, using the file 
                                     * given as stop_file */
    INDEX_NEW_QSTOP = (1 << 11),    /* stop at query time, using the file 
                                     * given as qstop_file (or NULL for default
                                     * stoplist) */
    INDEX_NEW_TABLESIZE = (1 << 9), /* dictate how large the postings 
                                     * hashtable is */
    INDEX_NEW_PARSEBUF = (1 << 10)  /* dictate how large the postings 
                                     * hashtable is */
};

/* XXX: comment me */
struct index_new_opt {
    unsigned int vocab_size;
    enum index_stem stemmer;        /* stemming algorithm used */
    const char *stop_file;
    unsigned long int maxfilesize;
    int bigendian;
    unsigned int tablesize;
    unsigned int parsebuf;
    const char *qstop_file;
};

/* create a new, empty index.  config can now be NULL if default compile-time 
 * configuration is to be used */
struct index *index_new(const char *name, const char *config, 
  unsigned int memory, int opts, struct index_new_opt *opt);

/* options to be passed to index_load */
enum index_load_opts {
    INDEX_LOAD_NOOPT = 0,           /* pass this to indicate no options */
    INDEX_LOAD_VOCAB = (1 << 0),    /* option to load index with updates of 
                                     * postings of size less than the size you 
                                     * specify (as an unsigned int) in the 
                                     * vocabulary */
    INDEX_LOAD_MAXFLIST = (1 << 2), /* option to load index, with updates to 
                                     * lists greater than the unsigned int you 
                                     * specify causing the creation of a new 
                                     * inverted list */
    INDEX_LOAD_IGNORE_VERSION = (1 << 5), /* if the index file format
                                      * version doesn't match, warn, but
                                      * try loading the index anyway. */
    INDEX_LOAD_TABLESIZE = (1 << 7), /* dictate how large the postings 
                                      * hashtable is */
    INDEX_LOAD_PARSEBUF = (1 << 8),  /* dictate how large the parsing buffer 
                                      * is */
    INDEX_LOAD_QSTOP = (1 << 11),    /* stop at query time, using the file 
                                      * given as qstop_list (or NULL for 
                                      * default stoplist) */
    INDEX_LOAD_DOCMAP_CACHE = (1 << 12)  /* specify which values to cache 
                                      * in-memory when the docmap loads */
};

/* XXX: comment me */
struct index_load_opt {
    unsigned int vocab_size;
    unsigned int maxflist_size;
    unsigned int tablesize;
    unsigned int parsebuf;
    const char *qstop_file;
    int docmap_cache;
};

#define INDEX_MEMORY_UNLIMITED 0    /* Don't limit memory usage */

/* read an index off of disk. memory is an indication of how much
   memory the user wants the system to use; INDEX_MEMORY_UNLIMITED 
   leaves it up to the system. */
struct index *index_load(const char *name, unsigned int memory, int opts, 
  struct index_load_opt *opt);

/* remove the index from disk.  Returns 0 on success, 
   negative failure.  Does _not_ perform index_delete.*/
int index_rm(struct index *idx);

/* destruct the index object */
void index_delete(struct index *idx);

/* remove index files then delete the index */
void index_cleanup(struct index *idx);

/* struct to record statistics about the index */
struct index_stats {
    unsigned long int dterms;       /* distinct terms in the index */
    unsigned int terms_high;        /* high word of total terms in index */
    unsigned int terms_low;         /* low word of total terms in index */
    unsigned long int docs;         /* number of documents in the index */
    unsigned int maxtermlen;        /* maximum length of a term in the index */
    unsigned int vocab_listsize;    /* size of lists that go in vocab */
    unsigned int updates;           /* number of times the index has actually 
                                     * been updated (not necessarily number of 
                                     * times index_add was called) */
    unsigned int tablesize;         /* hashtable size constant used */
    unsigned int parsebuf;          /* memory used for parser */
    int sorted;                     /* whether vectors are sorted */
    unsigned int doc_order_vectors; /* whether has doc-order vectors */
    unsigned int doc_order_word_pos_vectors; /* whether has doc-order with word 
                                      positions vectors are in index */
    unsigned int impact_vectors; /* indicates if impact ordered vectors are in
                                    index */
};

/* struct to record statistics about the index that take a while to 
 * calculate */
struct index_expensive_stats {
    double avg_weight;              /* average document weight */
    double avg_words;               /* average number of words in a document */
    double avg_length;              /* average document length in bytes */
    unsigned int vocab_leaves;      /* number of leaves in vocab btree */
    unsigned int vocab_pages;       /* total number of pages (incl leaves) in 
                                     * vocab btree */
    unsigned int pagesize;          /* pagesize used by btree */
    double vectors;                 /* total size of inverted lists */
    double vectors_files;           /* total size of lists in files */
    double vectors_vocab;           /* total size of lists in the vocab */
    double allocated_files;         /* total size of space allocated to lists
                                     * in files */
    double vocab_info;              /* size of non-structural information 
                                     * stored in the vocabulary */
    double vocab_structure;         /* size of structural information stored 
                                     * in the vocabulary */
    /* XXX: size of query processing vectors? */
    /* XXX: docmap aux string size? */
};

/* get statistics about the index, (statistics are written into stats 
 * structure) returning true on success.  index_expensive_stats returns stats
 * that take time to calculate. */
int index_stats(const struct index *idx, struct index_stats *stats);
int index_expensive_stats(const struct index *idx, 
  struct index_expensive_stats *stats);

/* struct that is returned for each document found by search */
struct index_result {
    unsigned long int docno;        /* the distinct number given to this doc */
    float score;                    /* strength of the match to query */
    char summary                    /* a summary of the document */
      [INDEX_SUMMARYLEN + 1];
    char title                      /* the title of the document */
      [INDEX_TITLELEN + 1];
    char auxilliary                 /* auxilliary information about the */
      [INDEX_AUXILIARYLEN + 1];     /* document, such its TREC number or URL */
};

/* options that can be passed to index_search.  Note that OKAPI and COSINE are
 * mutually exclusive. */
enum index_search_opts {
    INDEX_SEARCH_NOOPT = 0,              /* pass this to indicate no flags */
    INDEX_SEARCH_OKAPI_RANK = (1 << 0),  /* use Okapi ranking scheme with 
                                          * supplied parameters (must supply 
                                          * k1, k3, b as doubles) */
    INDEX_SEARCH_PCOSINE_RANK = (1 << 1),/* use pivoted cosine ranking scheme 
                                          * with supplied pivot point (must 
                                          * supply pivot as a double).  Note
                                          * that a pivot of 0 effectively turns
                                          * off pivoting, and 0.2 is a
                                          * reasonable starting value */
    INDEX_SEARCH_COSINE_RANK = (1 << 7), /* use basic cosine measurement with 
                                          * query length normalisation */
    INDEX_SEARCH_WORD_LIMIT = (1 << 2),  /* supply (as unsigned int) the maximum
                                          * number of words used from the query.
                                          * A default limit applies if not
                                          * supplied */

    /* use language model ranking with dirichlet smoothing as found in 
     * Zhai & Lafferty, A study of smoothing methods, 
     * ACM TOIS 2004 vol 22 num 2, 179-214.  Must provide mu parameter as an
     * option */
    INDEX_SEARCH_DIRICHLET_RANK = (1 << 5),

    /* use Dave Hawking's anchor-text oriented Okapi variant, as described in
     * 'Toward Better Weighting of Anchors', by Hawking, Upstill and Craswell.
     * you'll need to provide the two parameters alpha and k3. */
    INDEX_SEARCH_HAWKAPI_RANK = (1 << 8),

    /* use the impact-ordered ranking hueristic as proposed by Anh and Moffat 
     * in 'Impact Transformation: Effective and Efficient Web Retrieval'.
     * Requires impact ordered vectors to have been built previously. */
    INDEX_SEARCH_ANH_IMPACT_RANK = (1 << 10),

    /* query biased document summary type*/
    INDEX_SEARCH_SUMMARY_TYPE = (1 << 6),

    /* explicitly set the accumulator limit, rather than using the
     * default ACCUMULATOR_LIMIT adjusted to a minimum of 1%.  Note that
     * if you explicitly set the accumulator limit, then _no_ adjustment
     * is made for either collection size or number of results requested. */
    INDEX_SEARCH_ACCUMULATOR_LIMIT = (1 << 9)
};

/* FIXME: comment me */
struct index_search_opt {
    union {
        struct {
            float k1;
            float k3;
            float b;
        } okapi_k3;

        struct {
            float pivot;
        } pcosine;

        struct {
            float mu;
        } dirichlet;

        struct {
            float alpha;
            float k3;
        } hawkapi;
    } u;

    unsigned int word_limit;
    unsigned int accumulator_limit;
    enum index_summary_type summary_type;
};

/* search the index.  idx is a loaded or created index, query is a
 * NUL-terminated query string (see queryparse.h for format), startdoc is the
 * number of results to skip, len is the number of results to return, result is
 * an array of length len to hold the results.  Options is a bitfield in which
 * you can pass options in enum above (with corresponding parameters afterward).
 * Returns true on success and 0 on failure.  On successful return, the number
 * of results returned is written into *results, and the total number of
 * matching documents is written into *total_results.  value written into est
 * indicates whether *total_results is estimated (non-zero) or exact (zero). */
int index_search(struct index *idx, const char *query, 
  unsigned long int startdoc, unsigned long int len, 
  struct index_result *result, unsigned int *results, 
  double *total_results, int *est, int opts, struct index_search_opt *opt);

/* retrieve a portion of a document from the index cache.  idx is the index,
 * docno is the document number to retrieve, offset is the offset in the
 * document from which to start retrieval.  dst is a buffer of size dstsize that
 * will be filled with document data on successful return.  The number of bytes
 * read will be returned on success, -1 on failure */
unsigned int index_retrieve(const struct index *idx, unsigned long int docno,
  unsigned long int offset, void *dst, unsigned int dstsize); 

/* Retrieve the auxiliary data for a document.  AUX_BUF is the buffer
   the auxiliary data will be written into; AUX_BUF_LEN is the length
   of this buffer. AUX_LEN is the length of the auxiliary data.
   Returns true on success, false on failure.  If the buffer is too
   small, this counts as a failure; the required size will be written
   into AUX_LEN in this case. */
int index_retrieve_doc_aux(const struct index *idx, unsigned long int docno,
  char *aux_buf, unsigned int aux_buf_len, unsigned int *aux_len);

/* Retrieve the length of a document in bytes.  Returns UINT_MAX
   on error; we assume there is no document this length. */
unsigned int index_retrieve_doc_bytes(const struct index *idx,
  unsigned long int docno);
 
/* retrieve document statistics from the index.  the length (written into
 * bytes), number of words in (written into words), number of distinct words in
 * (written into distinct_words) cosine weight of (written into weight) and 
 * auxilliary data (written into aux) of docno are retrieved.  Returns true on 
 * success. */
int index_retrieve_stats(const struct index *idx, unsigned long int docno, 
  unsigned int *bytes, unsigned int *words, unsigned int *distinct_words,
  double *weight, const char **aux);

/* options that can be passed to index_add */
enum index_commit_opts {
    INDEX_COMMIT_NOOPT = 0,              /* pass this to indicate no flags */
    INDEX_COMMIT_DUMPBUF = (1 << 1),     /* buffer update writing (supply size 
                                          * of buffer as unsigned int) */
    INDEX_COMMIT_ANH_IMPACTS = (1 << 10) /* create Anh impact-ordered vectors */
};

struct index_commit_opt {
    unsigned int dumpbuf;
};

/* options that can be passed to index_add */
enum index_add_opts {
    INDEX_ADD_NOOPT = 0,                 /* pass this to indicate no flags */
    INDEX_ADD_ACCBUF = (1 << 0),         /* buffer updates (supply size of 
                                          * buffer as unsigned int) */
    INDEX_ADD_ACCDOC = (1 << 1),         /* buffer this number of documents 
                                          * (supply number as unsigned int) */
    INDEX_ADD_FLUSH = (1 << 2)           /* commit changes after this addition*/
};

struct index_add_opt {
    unsigned int accbuf;
    unsigned int accdoc;
    const char *detected_type;           /* contains type that file was 
                                          * indexed with after method call */
};

/* add a new document to the index.  file specifies what file to add, with type
 * giving the type of the file.  on successful return (non-zero return value),
 * *docno is written with the first assigned docno, and docs with the number of
 * *docs with the number of documents extracted from the file.  opts specify
 * options to use when updating the index.  You can supply options for the
 * adding operation using opt(s), and options for commits that occur during the
 * process using commitopt(s) */
int index_add(struct index *idx, const char *file, const char *mimetype,
  unsigned long int *docno, unsigned int *docs, 
  unsigned int opts, struct index_add_opt *opt, 
  unsigned int commitopts, struct index_commit_opt *commitopt);
 
/* write all changes to the index to disk (returns true on success) (XXX: talk
 * about options) */
int index_commit(struct index *idx, unsigned int opts, 
  struct index_commit_opt *opt,
  unsigned int addopts, struct index_add_opt *addopt);

#ifdef __cplusplus
}
#endif

#endif

