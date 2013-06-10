#include "firstinclude.h"

#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <float.h>

#include "vocab.h"
#include "impact_build.h"
#include "index.h"
#include "_index.h"
#include "getmaxfsize.h"
#include "btbulk.h"
#include "bucket.h"
#include "error.h"
#include "stdio.h"
#include "iobtree.h"
#include "vocab.h"
#include "docmap.h"
#include "_docmap.h"
#include "error.h"
#include "vec.h"
#include "fdset.h"
#include "str.h"

#define IMPACT_UNSET -1.0F
#define W_QT_UNSET -1.0F
#define E_VALUE 0.0001 

/* In-memory, decompressed representation of a postings list, without wpos. */
struct list_posting {
    unsigned long int docno; /* posting document number */
    unsigned long int f_dt;  /* number of times gram appears in thisdoc */
    unsigned int impact;     /* impact score of this posting */
};

struct list_decomp {
    struct list_posting *postings;   /* array of postings */
    unsigned int postings_size;      /* size of above array */
    unsigned long int f_t;           /* term frequency */
    unsigned long int docno_max;     /* highest doc no this gram appears in */
};

static enum impact_ret decompress_list(struct vocab_vector *vocab, 
  char *vec_buf, struct list_decomp *decomp_list);

static enum impact_ret compress_impact_ordered_list(struct list_decomp * list,
  char ** vec_mem, unsigned int * vec_mem_len, unsigned int * vec_size);

static enum impact_ret load_vector(struct index * idx, const char * term,
  struct vocab_vector * vocab, char ** vec_mem,
  unsigned int * vec_mem_len, unsigned int * vec_len); 

static enum impact_ret get_doc_vec(struct index * idx, const char * term,
  void * term_data, unsigned int term_data_len, 
  char ** vec_mem, unsigned int * vec_mem_len, 
  struct vocab_vector * vocab_in);

static enum impact_ret calculate_impact_limits(struct index * idx,
  double pivot, double * max_impact, double * min_impact, double *ft_avg);

static double calc_impact_pivoted_cosine(unsigned long int f_dt, 
  unsigned long int f_t, double W_d, double aW_d, double pivot);

static enum impact_ret 
calculate_list_impact_limits(struct vocab_vector * vocab_entry,
  char * vec_buf, struct docmap * docmap,
  double avg_weight, double pivot, double * list_max_impact, 
  double * list_min_impact);

static void impact_transform_list(struct list_decomp * decomp_list,
  struct docmap * docmap, double avg_weight, double pivot,
  double max_impact, double min_impact, double slope, 
  unsigned int quant_bits, double norm_B, double *w_qt_min, double *w_qt_max, 
  double f_t_avg);

static void impact_order_sort(struct list_decomp *list);

static int impact_order_compare(const void *p1, const void *p2);

struct addfile_data {
    struct fdset * fd;
    unsigned int fd_type;
};

static enum impact_ret fdset_write(unsigned int fileno,
  unsigned int filetype, unsigned long int offset,
  struct fdset *fdset, char *data, unsigned int data_len);

/**
 *  Routines for creating an impact-ordered index.
 */
enum impact_ret impact_order_index(struct index *idx) {
    unsigned int new_vocab_fileno;
    unsigned long int new_vocab_file_offset;
    struct btbulk new_vocab_bulk_inserter;
    int new_vocab_bulk_inserter_inited = 0;
    unsigned int tmp_vocab_fd_type;

    unsigned int new_vocab_root_fileno = 0;
    unsigned long int new_vocab_root_file_offset = 0;

    unsigned int new_vector_fileno;
    unsigned long int vector_file_offset;
    int new_vector_fd_out;
    unsigned int new_vector_fd_type;

    char * vec_mem = NULL;
    unsigned vec_mem_len = 0;

    ssize_t nwritten;
    unsigned int term_iterator_state[3] = { 0U, 0U, 0U };
    const char * term;
    unsigned int termlen;
    void * data;
    unsigned int datalen;
    enum btbulk_ret bulk_inserter_ret;
    struct list_decomp decomp_list;
    double max_impact;
    double min_impact;
    enum impact_ret our_ret = IMPACT_OK;
    double norm_B; /* used for logarithmic normalisation */
    struct addfile_data addfile_data;
    unsigned int fileno;
    unsigned long int terms = 0;
    unsigned long int dummy_offset = 0;
    unsigned int dummy_size = 0;
    int vector_file_is_new = 1;
    double f_t_avg;
    double w_qt_max = W_QT_UNSET;
    double w_qt_min = W_QT_UNSET;
    double pivot = IMPACT_DEFAULT_PIVOT;
    double slope = IMPACT_DEFAULT_SLOPE;
    unsigned int quant_bits = IMPACT_DEFAULT_QUANT_BITS;
    
    if ( (our_ret = calculate_impact_limits(idx, pivot,
              &max_impact, &min_impact, &f_t_avg)) != IMPACT_OK) {
        ERROR("calculating impact limits");
        goto ERROR;
    }
    assert(min_impact <= max_impact);

    norm_B = pow(max_impact / min_impact, 
      min_impact / (max_impact - min_impact));

    decomp_list.postings = NULL;
    decomp_list.postings_size = 0;

    tmp_vocab_fd_type = idx->tmp_type;
    addfile_data.fd = idx->fd;
    addfile_data.fd_type = tmp_vocab_fd_type;

    if (!(btbulk_new(idx->storage.pagesize, idx->storage.max_filesize,
              idx->storage.btleaf_strategy, idx->storage.btnode_strategy,
              1.0 /* fill factor */, 0, &new_vocab_bulk_inserter))) {
        ERROR("creating new btbulk inserter for impact ordering");
        goto ERROR;
    }
    new_vocab_bulk_inserter_inited = 1;

    /* add the new vectors to existing vector file set (though starting
       with a new file. */
    new_vector_fd_type = idx->index_type;
    new_vector_fileno = idx->vectors;
    /* not first_file_header, as these vector files follow 
       the existings ones. */
    vector_file_offset = 0;

    new_vocab_fileno = 0;
    new_vocab_file_offset = 0;
    /* not first_file_header, as that is only for vectors files */
    new_vocab_file_offset = 0;

    while ( (term = iobtree_next_term(idx->vocab, term_iterator_state,
              &termlen, &data, &datalen)) != NULL) {
        struct vocab_vector vocab_in;
        unsigned int vec_size;
        struct vocab_vector vocab_entry_out;
        unsigned int vocab_entry_out_len;
        unsigned int vocab_vector_out_len;
        enum vocab_ret vocab_ret;
        struct vec vec;

        if ( (our_ret = get_doc_vec(idx, term, data, datalen, 
                  &vec_mem, &vec_mem_len, &vocab_in)) != IMPACT_OK) {
            ERROR1("loading document vector for term '%s'", term);
            goto ERROR;
        }
        if ( (our_ret = decompress_list(&vocab_in, vec_mem, &decomp_list))
          != IMPACT_OK) {
            goto ERROR;
        }
        impact_transform_list(&decomp_list, idx->map, idx->stats.avg_weight, 
          pivot, max_impact, min_impact, slope, quant_bits, norm_B, 
          &w_qt_min, &w_qt_max, f_t_avg);
        if ( (our_ret = compress_impact_ordered_list(&decomp_list,
                  &vec_mem, &vec_mem_len, &vec_size)) != IMPACT_OK) {
            goto ERROR;
        }

        /* Write vector to disk. */
        /* XXX currently, we write all impact ordered vectors to location
           type 'file'.  This should be changed in future. */
        /* NOTE expressed this way to avoid integer overflow, which
           is a real issue, as the max filesize is quite likely to be 
           UINT_MAX.  Be VERY CAREFUL about modifying this expression! */
        if (idx->storage.max_filesize - vec_size < vector_file_offset) {
            dummy_offset = 0;
            dummy_size = vector_file_offset;
            new_vector_fileno++;
            vector_file_offset = 0;
            vector_file_is_new = 1;
        }

        if (vector_file_is_new) {
            if ( (new_vector_fd_out = fdset_create_seek(idx->fd, 
                      new_vector_fd_type, new_vector_fileno, 
                      vector_file_offset)) < 0) {
                ERROR2("unable to create output temporary vector file number "
                  "%lu and seek to offset %lu", new_vector_fileno, 
                  vector_file_offset);
                goto ERROR;
            }
            vector_file_is_new = 0;
        } else {
            if ( (new_vector_fd_out = fdset_pin(idx->fd, new_vector_fd_type,
                      new_vector_fileno, vector_file_offset, SEEK_SET)) < 0) {
                ERROR2("unable to open output temporary vector file number "
                       "%lu to offset %lu", new_vector_fileno, 
                       vector_file_offset);
                goto ERROR;
            }
        }

        nwritten = index_atomic_write(new_vector_fd_out, vec_mem, vec_size);
        fdset_unpin(idx->fd, new_vector_fd_type, new_vector_fileno,
          new_vector_fd_out);
        if (nwritten != (ssize_t) vec_size) {
            ERROR3("writing vector of size %lu to temporary vector file "
              "number %lu, offset %lu", vec_size, new_vector_fileno,
              vector_file_offset);
            goto ERROR;
        }

        /* XXX we should really remove any old impact-ordered vector
           entries; but the policy for this is still unclear. */

        /* Add vocab entry for impact vector to existing vocab entries. */
        vocab_entry_out.attr = VOCAB_ATTRIBUTES_NONE;
        vocab_entry_out.attribute = 0;
        vocab_entry_out.type = VOCAB_VTYPE_IMPACT;
        vocab_entry_out.size = vec_size;
        switch (vocab_in.type) {
        case VOCAB_VTYPE_DOC:
            vocab_entry_out.header.impact.docs = vocab_in.header.doc.docs;
            vocab_entry_out.header.impact.occurs = vocab_in.header.doc.occurs;
            vocab_entry_out.header.impact.last = vocab_in.header.doc.last;
            break;
        case VOCAB_VTYPE_DOCWP:
            vocab_entry_out.header.impact.docs = vocab_in.header.docwp.docs;
            vocab_entry_out.header.impact.occurs = vocab_in.header.docwp.occurs;
            vocab_entry_out.header.impact.last = vocab_in.header.docwp.last;
            break;
        default:
            assert("shouldn't happen" && 0);
        }
        vocab_entry_out.location = VOCAB_LOCATION_FILE;
        vocab_entry_out.loc.file.capacity = vec_size;
        vocab_entry_out.loc.file.fileno = new_vector_fileno;
        vocab_entry_out.loc.file.offset = vector_file_offset;

        vocab_entry_out_len = vocab_len(&vocab_entry_out);
        vocab_vector_out_len = datalen + vocab_entry_out_len;

        if (vocab_vector_out_len > vec_mem_len) {
            char * new_vec_mem;
            new_vec_mem = realloc(vec_mem, vocab_vector_out_len); 
            if (new_vec_mem == NULL) {
                our_ret = IMPACT_MEM_ERROR;
                goto ERROR;
            }
            vec_mem = new_vec_mem;
            vec_mem_len = vocab_vector_out_len;
        }
        memcpy(vec_mem, data, datalen);

        vec.pos = vec_mem + datalen;
        vec.end = vec_mem + vec_mem_len;
        vocab_ret = vocab_encode(&vocab_entry_out, &vec);
        assert(vocab_ret == VOCAB_OK); /* bug if not */

        /* vec_mem now contains new vocab vector to add to new index. */
        new_vocab_bulk_inserter.term = term;
        new_vocab_bulk_inserter.termlen = termlen;
        new_vocab_bulk_inserter.datasize = vocab_vector_out_len;
        do {
            new_vocab_bulk_inserter.fileno = new_vocab_fileno;
            new_vocab_bulk_inserter.offset = new_vocab_file_offset;

            bulk_inserter_ret = btbulk_insert(&new_vocab_bulk_inserter);
            switch (bulk_inserter_ret) {
            case BTBULK_OK:
                memcpy(new_vocab_bulk_inserter.output.ok.data, 
                  vec_mem, vocab_vector_out_len);
                break;
            case BTBULK_WRITE:
                our_ret = fdset_write(new_vocab_fileno,
                  tmp_vocab_fd_type, new_vocab_file_offset, idx->fd, 
                  new_vocab_bulk_inserter.output.write.next_out,
                  new_vocab_bulk_inserter.output.write.avail_out);
                if (our_ret != IMPACT_OK)
                    goto ERROR;
                new_vocab_file_offset += 
                      new_vocab_bulk_inserter.output.write.avail_out;
                break;
            case BTBULK_FLUSH:
                new_vocab_fileno++;
                idx->vectors++;
                assert(new_vocab_fileno == idx->vectors - 1);
                new_vocab_file_offset = 0;
                new_vocab_file_offset = 0;
                break;
            case BTBULK_ERR:
                ERROR2("error on btbulk_insert call for term '%s', "
                  "data size %u", term, vocab_vector_out_len);
                goto ERROR;
                break;
            default:
                assert(0);
            }
        } while (bulk_inserter_ret != BTBULK_OK);
        terms++;
        vector_file_offset += vec_size;
    } /* end while term = iobtree_next_term */
    assert(terms == iobtree_size(idx->vocab));
    assert(w_qt_max > w_qt_min);
    
    /* save parameters required at query time for impact ordering in index */
    /* these are saved in index_params_write() which is called in 
       index_commit_superblock() below */
    idx->impact_stats.avg_f_t = f_t_avg;
    idx->impact_stats.slope = slope;
    idx->impact_stats.quant_bits = quant_bits;
    idx->impact_stats.w_qt_min = w_qt_min;
    idx->impact_stats.w_qt_max = w_qt_max;
    idx->impact_vectors = 1;
    
    dummy_offset = 0;
    dummy_size = vector_file_offset;

    do {
        new_vocab_bulk_inserter.fileno = new_vocab_fileno;
        new_vocab_bulk_inserter.offset = new_vocab_file_offset;
        bulk_inserter_ret 
          = btbulk_finalise(&new_vocab_bulk_inserter, &new_vocab_root_fileno, 
            &new_vocab_root_file_offset);
        switch (bulk_inserter_ret) {
        case BTBULK_FINISH:
        case BTBULK_OK:
            bulk_inserter_ret = BTBULK_OK;
            break;
        case BTBULK_WRITE:
            our_ret = fdset_write(new_vocab_fileno,
              tmp_vocab_fd_type, new_vocab_file_offset, idx->fd,
              new_vocab_bulk_inserter.output.write.next_out,
              new_vocab_bulk_inserter.output.write.avail_out);
            if (our_ret != IMPACT_OK)
                goto ERROR;
            new_vocab_file_offset += 
                new_vocab_bulk_inserter.output.write.avail_out;
            break;
        case BTBULK_FLUSH:
            /* XXX copied from main loop. */
            new_vocab_fileno++;
            idx->vectors++;
            assert(new_vocab_fileno == idx->vectors - 1);
            new_vocab_file_offset = 0;
            new_vocab_file_offset = 0;
            break;
        case BTBULK_ERR:
            ERROR("error on btbulk_finalise call");
            goto ERROR;
            break;
        default:
            assert(0);
        }
    } while (bulk_inserter_ret != BTBULK_OK);

    /* delete the old vocab */
    iobtree_delete(idx->vocab);
    idx->vocab = NULL;
    for (fileno = 0; fileno < idx->vocabs; fileno++) {
        if (fdset_unlink(idx->fd, idx->vocab_type, fileno) < 0) {
            /* ouch, now the index is probably stuffed. */
            ERROR1("unlinking old vocab file number %u", fileno);
            our_ret = IMPACT_IO_ERROR;
            goto ERROR;
        }
    }
    idx->vocabs = 0;
    for (fileno = 0; fileno <= new_vocab_fileno; fileno++) {
        char final_fname[FILENAME_MAX + 1];
        char tmp_fname[FILENAME_MAX + 1];
        unsigned int final_fname_len;
        unsigned int tmp_fname_len;
        int writeable;

        fdset_name(idx->fd, idx->vocab_type, fileno, final_fname, FILENAME_MAX,
          &final_fname_len, &writeable);
        fdset_name(idx->fd, tmp_vocab_fd_type, fileno, tmp_fname, FILENAME_MAX,
          &tmp_fname_len, &writeable);
        if (fdset_close_file(idx->fd, tmp_vocab_fd_type, fileno) < 0) {
            /* in fact probably indicates a still-pinned fd, i.e. a 
               programming error */
            ERROR1("closing new vocab file %u", fileno);
            our_ret = IMPACT_IO_ERROR;
            goto ERROR;
        }
        if (rename(tmp_fname, final_fname) < 0) {
            ERROR3("renaming vocab file number %u from %s to %s\n",
              fileno, tmp_fname, final_fname);
            our_ret = IMPACT_IO_ERROR;
            goto ERROR;
        }
    }
    idx->vocabs = new_vocab_fileno + 1;

    /* do quick load of vocab */
    idx->vocab = iobtree_load_quick(idx->storage.pagesize, 
      idx->storage.btleaf_strategy, idx->storage.btnode_strategy,
      NULL, idx->fd, idx->vocab_type, new_vocab_root_fileno,
      new_vocab_root_file_offset, terms);
    if (idx->vocab == NULL) {
        ERROR("quick-loading new vocab");
        our_ret = IMPACT_OTHER_ERROR; /* could be mem, io, prob. bug */
        goto ERROR;
    }
    /* commit index superblock */
    /* XXX check that this is correct. */
    if (!(index_commit_superblock(idx))) {
        ERROR("committing superblock for new index");
        our_ret = IMPACT_IO_ERROR; /* or else a bug */
        goto ERROR;
    }

    goto END;

ERROR:
    if (our_ret == IMPACT_OK)
        our_ret = IMPACT_OTHER_ERROR;

END:
    if (new_vocab_bulk_inserter_inited) {
        btbulk_delete(&new_vocab_bulk_inserter);
    }

    free(vec_mem);
    free(decomp_list.postings);
    return our_ret;
}

static enum impact_ret decompress_list(struct vocab_vector *vocab, 
  char *vec_buf, struct list_decomp *decomp_list) {
    unsigned long int docs;
    unsigned long int occurs;
    unsigned long int last;
    unsigned long int docno;
    struct vec vec;
    unsigned long int d;

    assert(vocab->type == VOCAB_VTYPE_DOC || vocab->type == VOCAB_VTYPE_DOCWP);
    switch (vocab->type) {
    case VOCAB_VTYPE_DOC:
        docs = vocab->header.doc.docs;
        occurs = vocab->header.doc.occurs;
        last = vocab->header.doc.last;
        break;
    case VOCAB_VTYPE_DOCWP:
        docs = vocab->header.docwp.docs;
        occurs = vocab->header.docwp.occurs;
        last = vocab->header.docwp.last;
        break;
    default:
        assert("shouldn't be here" && 0);
        return IMPACT_OTHER_ERROR;
    }
    if (decomp_list->postings_size < docs) {
        struct list_posting *tmp_postings;
        if ( (tmp_postings = realloc(decomp_list->postings,
                  sizeof(*decomp_list->postings) * docs)) == NULL)
            return IMPACT_MEM_ERROR;
        decomp_list->postings = tmp_postings;
        decomp_list->postings_size = docs;
    }
    decomp_list->f_t = docs;
    decomp_list->docno_max = last;

    docno = 0;
    vec.pos = vec_buf;
    vec.end = vec_buf + vocab->size;
    for (d = 0; d < docs; d++) {
        unsigned long int docno_d;
        unsigned long int f_dt;
        vec_vbyte_read(&vec, &docno_d);
        vec_vbyte_read(&vec, &f_dt);
        docno += docno_d;
        decomp_list->postings[d].docno = docno + d;
        decomp_list->postings[d].f_dt = f_dt;
        /* N.B. leaving impact unset at this time. */
        if (vocab->type == VOCAB_VTYPE_DOCWP) {
            unsigned int bytes;
            vec_vbyte_scan(&vec, (unsigned int) f_dt, &bytes);
        }
        assert(d == 0 || decomp_list->postings[d].docno >
          decomp_list->postings[d - 1].docno);
    }
    assert(decomp_list->docno_max == docno + (d - 1));
    return IMPACT_OK;
}

static enum impact_ret load_vector(struct index * idx, const char * term,
  struct vocab_vector * vocab, char ** vec_mem,
  unsigned int * vec_mem_len, unsigned int * vec_len) {
    int fd_in;
    ssize_t read;

    if (vocab->size > *vec_mem_len) {
        if (!(*vec_mem = realloc(*vec_mem, vocab->size)))
            return IMPACT_MEM_ERROR;
        *vec_mem_len = vocab->size;
    }
    *vec_len = vocab->size;

    switch (vocab->location) {
    case VOCAB_LOCATION_VOCAB:
        memcpy(*vec_mem, vocab->loc.vocab.vec, vocab->size);
        /* XXX does loc.vocab.vec need to be freed? */
        break;

    case VOCAB_LOCATION_FILE:
        if ( (fd_in = fdset_pin(idx->fd, idx->index_type,
                  vocab->loc.file.fileno, vocab->loc.file.offset,
                  SEEK_SET)) < 0) 
            return IMPACT_IO_ERROR;
        read = index_atomic_read(fd_in, *vec_mem, vocab->size);
        fdset_unpin(idx->fd, idx->index_type, vocab->loc.file.fileno, fd_in);
        if (read < 0)
            return IMPACT_IO_ERROR;
        break;
    default:
        assert("shouldn't get here" && 0);
    }
    return IMPACT_OK;
}

static enum impact_ret calculate_impact_limits(struct index * idx,
  double pivot, double * max_impact, double * min_impact, double *ft_avg) {
    unsigned int term_iterator_state[3] = { 0U, 0U, 0U };
    const char * term;
    unsigned int termlen;
    void * data;
    unsigned int datalen;
    char * vec_mem = NULL;
    unsigned vec_mem_len = 0;
    double avg_weight;
    enum impact_ret our_ret = IMPACT_OK;
    int first_term = 1;
    enum docmap_ret docmap_ret;
    unsigned long int ft_sum = 0; /* cumulitive sum of f_t for each term */
    unsigned long int ft_count = 0; /* number of terms */

    docmap_ret = docmap_cache(idx->map, docmap_get_cache(idx->map) 
      | DOCMAP_CACHE_WEIGHT);
    if (docmap_ret != DOCMAP_OK) {
        ERROR("loading document weights");
        switch (docmap_ret) {
        case DOCMAP_MEM_ERROR:
            our_ret = IMPACT_MEM_ERROR;
            break;
        case DOCMAP_IO_ERROR:
            our_ret = IMPACT_IO_ERROR;
            break;
        case DOCMAP_FMT_ERROR:
            our_ret = IMPACT_FMT_ERROR;
            break;
        default:
            our_ret = IMPACT_OTHER_ERROR;
        }
        goto ERROR;
    }

    avg_weight = idx->stats.avg_weight;

    *max_impact = IMPACT_UNSET;
    *min_impact = IMPACT_UNSET;
    while ( (term = iobtree_next_term(idx->vocab, term_iterator_state,
              &termlen, &data, &datalen)) != NULL) {
        struct vocab_vector vocab_in;
        double list_max_impact;
        double list_min_impact;

        if ( (our_ret = get_doc_vec(idx, term, data, datalen, 
                  &vec_mem, &vec_mem_len, &vocab_in)) != IMPACT_OK) {
            ERROR1("loading document vector for term '%s'", term);
            goto ERROR;
        }
        
        ft_sum += vocab_in.header.docwp.docs;
        ft_count++;
        
        if ( (our_ret = calculate_list_impact_limits(&vocab_in, 
                  vec_mem, idx->map, avg_weight, pivot,
                  &list_max_impact, &list_min_impact)) != IMPACT_OK) {
            ERROR1("calculating max and min impact for term '%s'", term);
            goto ERROR;
        }
        if (first_term) {
            *max_impact = list_max_impact;
            *min_impact = list_min_impact;
            first_term = 0;
        } else {
            if (*max_impact < list_max_impact)
                *max_impact = list_max_impact;
            if (*min_impact > list_min_impact)
                *min_impact = list_min_impact;
        }
    }
    *ft_avg = ft_sum/ft_count;
    goto END;

ERROR:
    if (our_ret == IMPACT_OK)
        our_ret = IMPACT_OTHER_ERROR;

END:
    free(vec_mem);
    return our_ret;
}

static enum impact_ret calculate_list_impact_limits(
  struct vocab_vector * vocab_entry, char * vec_buf, struct docmap * docmap,
  double avg_weight, double pivot, double * list_max_impact, 
  double * list_min_impact) {
    unsigned long int docs;
    unsigned long int d;
    unsigned long int docno;
    struct vec vec;

    *list_max_impact = IMPACT_UNSET;
    *list_min_impact = IMPACT_UNSET;

    switch (vocab_entry->type) {
    case VOCAB_VTYPE_DOC:
        docs = vocab_entry->header.doc.docs;
        break;
    case VOCAB_VTYPE_DOCWP:
        docs = vocab_entry->header.docwp.docs;
        break;
    default:
        assert("shouldn't be here" && 0);
        return IMPACT_OTHER_ERROR;
    }

    docno = 0;
    vec.pos = vec_buf;
    vec.end = vec_buf + vocab_entry->size;
    for (d = 0; d < docs; d++) {
        unsigned long int docno_d;
        unsigned long int f_dt;
        double w_dt;
        
        vec_vbyte_read(&vec, &docno_d);
        vec_vbyte_read(&vec, &f_dt);
        if (vocab_entry->type == VOCAB_VTYPE_DOCWP) {
            unsigned int bytes;
            vec_vbyte_scan(&vec, (unsigned int) f_dt, &bytes);
        }
        docno += docno_d;
        w_dt = calc_impact_pivoted_cosine(f_dt, docs, 
          DOCMAP_GET_WEIGHT(docmap, docno + d), avg_weight, pivot);
        if (d == 0) 
            *list_min_impact = *list_max_impact = w_dt;
        else if (w_dt < *list_min_impact)
            *list_min_impact = w_dt;
        else if (w_dt > *list_max_impact)
            *list_max_impact = w_dt;
    }
    return IMPACT_OK;
}

static double calc_impact_pivoted_cosine(unsigned long int f_dt, 
  unsigned long int f_t, double W_d, double aW_d, double pivot) {
    return (1 + log(f_dt)) / ((1.0 - pivot) + (pivot * W_d / aW_d));
}

/* normalises using "loga" two fixed-point promotion technique. */
double impact_normalise(double impact, double norm_B, double slope, 
  double max_impact, double min_impact) {
    double norm_impact;

    norm_impact = min_impact + (min_impact * (log10(impact / min_impact)
          / log10(norm_B)));
    norm_impact = (1.0 - slope) * norm_impact + (slope * impact);
    if (norm_impact < min_impact)
        norm_impact = min_impact;
    else if (norm_impact > max_impact)
        norm_impact = max_impact;
    return norm_impact;
}

unsigned int impact_quantise(double impact, unsigned int quant_bits, 
  double max_impact, double min_impact) {
    return (unsigned int) floor(pow(2, quant_bits) 
        * ((impact - min_impact) / (max_impact - min_impact + E_VALUE))) 
      + 1;
}

static enum impact_ret get_doc_vec(struct index * idx, const char * term,
  void * term_data, unsigned int term_data_len, 
  char ** vec_mem, unsigned int * vec_mem_len, struct vocab_vector * vocab_in) {
    struct vec vocab_vec;
    enum vocab_ret vocab_ret;
    enum impact_ret our_ret;
    unsigned int vec_len;

    vocab_vec.pos = (char *)term_data;
    vocab_vec.end = (char *)term_data + term_data_len;
    while ( (vocab_ret = vocab_decode(vocab_in, &vocab_vec)) == VOCAB_OK
      && vocab_in->type != VOCAB_VTYPE_DOC 
      && vocab_in->type != VOCAB_VTYPE_DOCWP) 
        ;
    if (vocab_ret != VOCAB_OK)
        return IMPACT_FMT_ERROR;
    if ( (our_ret = load_vector(idx, term, vocab_in, vec_mem, 
              vec_mem_len, &vec_len)) != IMPACT_OK)
        return our_ret;
    return IMPACT_OK;
}

static void impact_transform_list(struct list_decomp * list,
  struct docmap * docmap, double avg_weight, double pivot,
  double max_impact, double min_impact, double slope, 
  unsigned int quant_bits, double norm_B, double *w_qt_min, 
  double *w_qt_max, double f_t_avg) {
    unsigned int d;
    double impact;
    double w_qt;
    
    for (d = 0; d < list->f_t; d++) {
        impact = calc_impact_pivoted_cosine(list->postings[d].f_dt, list->f_t,
          DOCMAP_GET_WEIGHT(docmap, list->postings[d].docno), avg_weight,
          pivot);
        impact = impact_normalise(impact, norm_B, slope, max_impact,
          min_impact);
        list->postings[d].impact = impact_quantise(impact, quant_bits,
          max_impact, min_impact);
          
        w_qt = (1 + log(list->postings[d].f_dt)) * 
               (log (1 + (f_t_avg/list->f_t)));
        if (*w_qt_min == W_QT_UNSET) {
            *w_qt_min = w_qt;
            *w_qt_max = w_qt;
        } else {
            if (*w_qt_max < w_qt)
                *w_qt_max = w_qt;
            if (*w_qt_min > w_qt)
                *w_qt_min = w_qt;
        }
    }
    impact_order_sort(list);
#ifndef NDEBUG
    for (d = 0; d < list->f_t; d++) {
        if (d != 0) {
            assert(list->postings[d].docno > list->postings[d - 1].docno
              || list->postings[d].impact < list->postings[d - 1].impact);
        }
    }
#endif
}

static void impact_order_sort(struct list_decomp *list) {
     qsort(list->postings, list->f_t, sizeof(struct list_posting), 
       impact_order_compare);
}

static int impact_order_compare(const void *p1, const void *p2) {
    struct list_posting *d1 = (struct list_posting *)p1;
    struct list_posting *d2 = (struct list_posting *)p2;

    if (d1->impact < d2->impact)
        return 1;
    else if (d1->impact > d2->impact)
        return -1;
    else {
        if (d1->docno > d2->docno)
            return 1;
        else if (d1->docno < d2->docno)
            return -1;
        else
            return 0;
    }
}

static enum impact_ret compress_impact_ordered_list(struct list_decomp * list,
  char ** vec_mem, unsigned int * vec_mem_len, unsigned int * vec_size) {
    unsigned long int prev_docno;
    unsigned long int block_size;
    struct vec vec;
    unsigned long int d;
    unsigned long int docno_d;
    unsigned int space_for_block;
    unsigned int new_block;
        
    vec.pos = *vec_mem;
    vec.end = *vec_mem + *vec_mem_len;

    prev_docno = 0;
    block_size = 0;
    new_block = 1;

    for (d = 0; d < list->f_t; d++) {
        if (block_size == 0) {
            prev_docno = 0;
            new_block = 1;
            for (block_size = 1; block_size + d < list->f_t 
              && list->postings[block_size + d].impact
              == list->postings[d].impact; block_size++) 
                /* no-op */ ;

            /* Check for space only at the start of each block.  This
               frees us from having to check for any of the vbyte_writes. */
            space_for_block = (2 + block_size) * VEC_VBYTE_MAX;
            if (vec_len(&vec) < space_for_block) {
                char * new_vec_mem;
                unsigned int new_vec_mem_len;
                unsigned long int offset;
                /* realloc enough for this block, and guess that remaining
                   blocks will be the same size.  We don't try to over-allocate
                   too much here; we're re-using the vector memory space from
                   the doc-ordered list, which should generally be larger than
                   the impact-ordered one anyway.  So we're only dealing with
                   marginal cases here. */
                new_vec_mem_len = *vec_mem_len + space_for_block 
                    * (list->f_t - d);
                if ( (new_vec_mem = realloc(*vec_mem, 
                          new_vec_mem_len)) == NULL) {
                    return IMPACT_MEM_ERROR;
                }
                *vec_mem_len = new_vec_mem_len;
                offset = vec.pos - *vec_mem;
                *vec_mem = new_vec_mem; 
                vec.pos = new_vec_mem + offset;
                vec.end = new_vec_mem + new_vec_mem_len;
                assert(vec.end > vec.pos);
                assert(vec_len(&vec) >= space_for_block);
            }
            vec_vbyte_write(&vec, block_size);

            /* Note: as long as impact quantisation bits B < 8, impact will 
               only be one byte, so there is no gain in storing delta values.  
               Since this will generally be the case, and this simplifies 
               the code, we don't calculate the deltas.  This code will 
               work even for B >= 8, just with a (very slight) loss 
               in compression. */
            vec_vbyte_write(&vec, list->postings[d].impact);
        } 
        --block_size;
        
        if (new_block == 1) { 
            new_block = 0;
            /* first document in block is an absolute value, not a difference */
            docno_d = list->postings[d].docno;
        } else {
            assert (list->postings[d].docno > prev_docno);
            docno_d = list->postings[d].docno - (prev_docno + 1);
            /* we store the docno delta - 1, as it will always be at least
               1 (doc numbers always increase) */
        }
        prev_docno = list->postings[d].docno;
        vec_vbyte_write(&vec, docno_d);
        /* vec_vbyte_write(&vec, list->postings[d].f_dt); */ 
        /* dont need offsets */
    }
    *vec_size = vec.pos - *vec_mem;
    return IMPACT_OK;
}

static enum impact_ret fdset_write(unsigned int fileno,
  unsigned int filetype, unsigned long int offset,
  struct fdset *fdset, char *data, unsigned int data_len) {
    int fd;
    ssize_t nwritten;
    fd = fdset_pin(fdset, filetype, fileno, offset, SEEK_SET);
    if (fd < 0 
      && ((fd = fdset_create_seek(fdset, filetype, fileno, offset)) < 0)) {
        ERROR2("opening output file number %u to offset %lu", fileno, offset);
        return IMPACT_IO_ERROR;
    }
    nwritten = index_atomic_write(fd, data, data_len);
    fdset_unpin(fdset, filetype, fileno, fd);
    if (nwritten != (ssize_t) data_len) {
        ERROR3("writing %u bytes to file number %u at offset %lu", 
          data_len, fileno, offset);
        return IMPACT_IO_ERROR;
    }
    return IMPACT_OK;
}

