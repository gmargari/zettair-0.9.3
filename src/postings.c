/* postings.c implements a postings list as specified in postings.h
 *
 * There are two primary problems in generating postings.  This postings module
 * uses a (large) internal hashtable that seems to work pretty well for 
 * locating postings entries, so that's not hard.  However, once you've found
 * the correct postings entry, there's memory management involved in managing
 * the compressed accumulated postings, which is difficult.  There's also the
 * problem that our postings format is <d, f_dt [off1, off2 ... off_f_dt]>.
 * Note that the f_dt value appears in the vector *before* the offsets it
 * describes.  So when we recieve the first offset, we don't know how many are
 * going to follow it in this document, and hence don't know the correct f_dt
 * value.  But we have to store the offsets *somewhere* in the mean time.
 *
 * The solution to the f_dt/offsets problem is to assume that each term is 
 * going to occur once.  When we recieve the first offset, we write the docno
 * dgap and an f_dt of 1 prior to writing it.  When we update the changed
 * postings entries at the end of the document (which i'd like to get away from,
 * but am finding difficult thanks to the need to calculate the document weight
 * for the cosine measure) we check the count of offsets received for this
 * document.  If it turned out to be 1 (which it will in a large number of
 * cases) then everything is fine.  If not, then we go back to the location
 * that we wrote the f_dt value and change it.  Since it's stored in vbyte
 * format, the size of the f_dt value may change, which means we have to 
 * shuffle the offsets for this document up a couple of bytes to make room.
 * However, since vbytes are byte-coded, most f_dt's occupy exactly one byte.
 * That means that actually needing to move the offsets is rare.  Previous
 * solutions to this problem have been to maintain a linked list of offsets
 * before encoding them all, or encoding them onto a different memory area and
 * then copying them.  This should be by far the best.
 *
 * originally written Hugh Williams
 *
 * updated nml 2003-03-09
 *
 */

#include "firstinclude.h"

#include "postings.h"
#include "_postings.h"

#include "bit.h"
#include "def.h"
#include "stop.h"
#include "str.h"
#include "poolalloc.h"
#include "objalloc.h"
#include "timings.h"
#include "vec.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* initial size of memory pools created */
#define MEMPOOL_SIZE 1000000

struct postings* postings_new(unsigned int tablesize, 
  void (*stem)(void *opaque, char *term), void *opaque, struct stop *list) {
    struct postings *p = malloc(sizeof(*p));
    unsigned int bits = bit_log2(tablesize);

    tablesize = bit_pow2(bits);  /* tablesize is now guaranteed to be a power 
                                  * of two */

    if (p && (p->hash = malloc(sizeof(struct postings_node *) * tablesize))
      && (p->string_mem = poolalloc_new(!!DEAR_DEBUG, MEMPOOL_SIZE, NULL))
      && (p->node_mem = objalloc_new(sizeof(struct postings_node), 0, 
          !!DEAR_DEBUG, MEMPOOL_SIZE, NULL))) {
        p->stop = list;
        p->stem = stem; 
        p->stem_opaque = opaque;
        p->tblbits = bits;
        p->tblsize = tablesize;
        assert(p->tblbits && p->tblsize);
        p->size = p->dterms = p->terms = 0;
        p->err = 0;
        p->docno = 0;
        p->docs = 0;
        p->update = NULL;
        p->update_required = 0;
        BIT_ARRAY_NULL(p->hash, tablesize);
    } else if (p) {
        if (p->hash) {
            free(p->hash);
            if (p->string_mem) {
                poolalloc_delete(p->string_mem);
                if (p->node_mem) {
                    objalloc_delete(p->node_mem); 
                }
            }
        }
        p = NULL;
    }

    return p;
}

void postings_delete(struct postings *post) {
    unsigned int i;
    struct postings_node* node;

    /* iterate over hashtable, freeing vectors */
    for (i = 0; i < post->tblsize; i++) {
        node = post->hash[i];
        while (node) {
            free(node->vecmem);
            node = node->next;
        }

        post->hash[i] = NULL;
    }

    poolalloc_delete(post->string_mem);
    objalloc_delete(post->node_mem);
    free(post->hash);
    free(post);
}

void postings_abort(struct postings* post) {
    poolalloc_delete(post->string_mem);
    objalloc_delete(post->node_mem);
    free(post->hash);
    free(post);
    return;
}

int postings_needs_update(struct postings *post) {
    return post->update_required;
}

/* internal function to double the size of the vector allocated to a node */
static int postings_node_expand(struct postings_node *node) {
    unsigned int len = node->vec.pos - node->vecmem;
    unsigned int size = node->vec.end - node->vecmem;
    unsigned int count = node->last_count - node->vecmem;
    char *ptr;

    assert(node->last_count >= node->vecmem);

    if ((ptr = realloc(node->vecmem, size * 2))) {
        node->vecmem = ptr;
        node->vec.pos = ptr + len;
        node->vec.end = ptr + size * 2;
        node->last_count = node->vecmem + count;
        assert(node->last_count >= node->vecmem);
        return 1;
    } else {
        return 0;
    }
}

int postings_update(struct postings* post, struct postings_docstats* stats) {
    struct postings_node* node = post->update;
    unsigned int terms = 0,
                 dterms = 0;
    float weight = 0,
           fdt_log;

    while (node) {
        /* calculate document weight */
        fdt_log = (float) logf((float) node->offsets);
        weight += (1 + fdt_log) * (1 + fdt_log);

        assert(node->offsets);
        assert(node->last_count != node->vecmem);
        assert(node->last_count > node->vecmem);
        assert((post->docno > node->last_docno) || (node->last_docno == -1));

        /* check if we have to correct the count that we wrote before (1) */
        if (node->offsets > 1) {
            unsigned int len = vec_vbyte_len(node->offsets);
            struct vec offsetvec;
 
            /* check if the count will expand in size */
            if (len > 1) {
                /* check if we need to resize the vector */
                if ((VEC_LEN(&node->vec) < len - 1) 
                  /* resize it */
                  && !postings_node_expand(node)) {
                    return 0;
                }

                /* move offsets up to make room for expanded count */
                assert(node->vec.pos > node->last_count);
                assert(node->vecmem < node->last_count);
                memmove(node->last_count + len, node->last_count + 1, 
                  node->vec.pos - (node->last_count + 1));
                node->vec.pos += len - 1;
                post->size += len - 1;
            }

            /* point offsetvec to the correct location to write the correct
             * count */
            offsetvec.pos = node->last_count;
            offsetvec.end = node->vec.end;

            len = vec_vbyte_write(&offsetvec, node->offsets);
            assert(len);
        }

        /* watch for overflow */
        assert(node->occurs + node->offsets > node->occurs);  
        node->occurs += node->offsets;
        terms += node->offsets;
        dterms++;
        node->offsets = 0;

        node->last_docno = post->docno;
        node->last_offset = -1;
        node->last_count = node->vecmem;
        node->docs++;

        node = node->update;
    }

    post->update = NULL;                /* reset update list */
    post->update_required = 0;
    stats->weight = (float) sqrtf(weight);
    stats->terms = terms;
    stats->distinct = dterms;
    return 1;
}

void postings_adddoc(struct postings* post, unsigned long int docno) {
    /* do compile time check of correct call sequence for efficiency */
    assert(post->update == NULL);
    assert((post->docno < docno) || ((post->docno == 0) && (docno == 0)));
    post->docno = docno;
    post->docs++;
    /* this ensures that updates happen even for empty documents! */
    post->update_required = 1;
}

int postings_addword(struct postings *post, char *term, 
  unsigned long int wordno) {
    unsigned int hash,
                 len,
                 bytes;
    struct postings_node** prev,
                        * node;

    if (post->stem) {
        post->stem(post->stem_opaque, term);
        /* do nothing for 0 length terms (occur sometimes because stemming 
         * removes entire word :o( ) */
        if (!term[0]) {
            return 1;
        }
    }

    assert(!iscntrl(term[0]));
    hash = str_hash(term) & bit_lmask(post->tblbits);
    prev = &post->hash[hash]; 
    node = *prev;

    while (node) {
        if (!str_cmp(term, node->term)) {
            /* found previous entry, perform move to front on hash chain */
            *prev = node->next;
            node->next = post->hash[hash];
            post->hash[hash] = node;
            break;
        }
        prev = &node->next;
        node = *prev;
    }

    if (!node) {
        /* couldn't find a previous entry, need to add a new one */
        len = str_len(term);
        assert(len);
        if ((node = objalloc_malloc(post->node_mem, sizeof(*node)))
          && (node->term = poolalloc_memalign(post->string_mem, len + 1, 1))
          && (node->vec.pos = node->vecmem = malloc(INITVECLEN))) {

            node->vec.end = node->vec.pos + INITVECLEN;

            str_cpy(node->term, term);

            node->last_docno = -1;
            node->last_offset = -1;
            node->last_count = node->vecmem;
            node->next = post->hash[hash];
            node->docs = node->offsets = node->occurs = 0;
            /* insert into table */
            post->hash[hash] = node;
            post->dterms++;
            post->size += len + 1;
        } else if (node->vecmem) {
            free(node->vecmem);
            post->err = ENOMEM;
            return 0;
        /* don't need to check others because they come from mem pools */
        } else {
            post->err = ENOMEM;
            return 0;
        }
    }

    assert(node);

    /* mark document as changed */
    if (!node->offsets) {
        node->update = post->update;
        post->update = node;

        /* write d-gap */
        while (!(bytes 
          = vec_vbyte_write(&node->vec, 
            post->docno - (node->last_docno + 1)))) {

            /* need to expand the node */
            if (!postings_node_expand(node)) {
                return 0;
            }
        }
        post->size += bytes + 1;

        /* save current position in case we need to overwrite it later, and
         * write in a count of 1 */
        node->last_count = node->vec.pos;
        assert(node->last_count > node->vecmem);
        while (!(bytes = vec_vbyte_write(&node->vec, 1))) {
            /* need to expand the node */
            if (!postings_node_expand(node)) {
                return 0;
            }
        }
    }

    /* encode new offset */
    assert((wordno > node->last_offset) || (node->last_offset == -1));
    while (!(bytes 
      = vec_vbyte_write(&node->vec, wordno - (node->last_offset + 1)))) {
         /* need to expand the node */
         if (!postings_node_expand(node)) {
             return 0;
         }
    }
    post->size += bytes;
    node->last_offset = wordno;
    node->offsets++;
    post->terms++;
    return 1;
}

int post_cmp(const void *vone, const void *vtwo) {
    const struct postings_node * const *one = vone,
                               * const *two = vtwo;

    return str_cmp((*one)->term, (*two)->term);
}

int postings_remove_offsets(struct postings *post) {
    unsigned int i,
                 dgap_bytes,
                 f_dt_bytes,
                 bytes,
                 scanned,
                 scanned_bytes;
    struct postings_node *node;
    struct vec src,
               dst;
    unsigned long int dgap,
                      f_dt;

    /* FIXME: note, this should assert !post->update_required, but due to the
     * way that TREC documents are parsed (basically under the assumption that
     * another one is always coming) we end up with an empty document at the 
     * end */
    assert(!post->update);

    for (i = 0; i < post->tblsize; i++) {
        node = post->hash[i];
        while (node) {
            src.pos = dst.pos = node->vecmem;
            src.end = dst.end = node->vec.pos;

            /* remove offsets by decoding and re-encoding the vector.  Note 
             * that we can read and write from the same vector because the
             * writing always lags the reading. */
            while ((dgap_bytes = vec_vbyte_read(&src, &dgap))
              && (f_dt_bytes = vec_vbyte_read(&src, &f_dt))
              && ((scanned = vec_vbyte_scan(&src, f_dt, &scanned_bytes)) 
                == f_dt)) {

                bytes = vec_vbyte_write(&dst, dgap);
                assert(bytes);
                bytes = vec_vbyte_write(&dst, f_dt);
                assert(bytes);
            }

            assert(dgap_bytes == 0 && VEC_LEN(&src) == 0);
            node->vec.pos = src.pos;
            node = node->next;
        }
    }

    /* note that after this operation the postings are so completely broken 
     * that they're only good for dumping or clearing, although this is not
     * currently enforced. */

    return 1;
}

int postings_dump(struct postings* post, void *buf, unsigned int bufsize, 
  int fd) {
    unsigned int i,
                 j,
                 stopped = 0,
                 pos,                     /* position in current vector */
                 len,                     /* length of current term */
                 wlen,                    /* length of last write */
                 dbufsz;                  /* size of dbuf */
    struct postings_node* node,           /* current node */
                        ** arr;           /* array of postings nodes */
    char *dbuf,                           /* dumping buffer */
         *dbufpos;                        /* position in dumping buffer */
    struct vec v;

    /* FIXME: note, this should assert !post->update_required, but due to the
     * way that TREC documents are parsed (basically under the assumption that
     * another one is always coming) we end up with an empty document at the 
     * end */
    assert(!post->update);

    /* XXX: hack, allocate a big array of postings and then sort them by term.
     * This is so that postings go out sorted by term instead of hash value. */
    if (!(arr = malloc(sizeof(*arr) * post->dterms))) {
        return 0;
    }

    /* the provided buffer is used to dump the postings */
    dbuf = buf;
    dbufsz = bufsize;

    /* copy nodes into array */
    j = 0;
    for (i = 0; i < post->tblsize; i++) {
        node = post->hash[i];
        while (node) {
            /* perform stopping.  Ideally we'd like to stop terms before
             * stemming them, and before they make their way into the postings.
             * However, this means that we have to call the stoplist
             * once-per-term, which makes it a big bottleneck.  We stop here to
             * minimise the performance impact on the most common case, no
             * stopping.  Note that if we really wanted to make stopping 
             * (when actually used) go faster, it would be better to have a
             * sorted stoplist as well, and merge against that rather than 
             * doing one hash lookup per term. */
            if (!post->stop || stop_stop(post->stop, node->term) == STOP_OK) {
                arr[j++] = node;
            } else {
                assert(++stopped);  /* count stopped terms while debugging */
            }
            node = node->next;
        }

        /* reset hash node (memory free'd below) */
        post->hash[i] = NULL;
    }

    assert(j + stopped == post->dterms);
    stopped = 0;

    qsort(arr, post->dterms, sizeof(*arr), post_cmp);

    v.pos = dbuf;
    v.end = dbuf + dbufsz;
    for (i = 0; i < j;) {
        while ((i < post->dterms) 
          && ((len = str_len(arr[i]->term)), 1)
          && (((unsigned int) VEC_LEN(&v)) >= vec_vbyte_len(len) + len 
            + vec_vbyte_len(arr[i]->docs) + vec_vbyte_len(arr[i]->occurs) 
            + vec_vbyte_len(arr[i]->last_docno) 
            + vec_vbyte_len(arr[i]->vec.pos - arr[i]->vecmem))) {

            unsigned int bytes;

            assert(len);
            assert(dbufsz > vec_vbyte_len(len) + len 
              + vec_vbyte_len(arr[i]->docs) + vec_vbyte_len(arr[i]->occurs) 
              + vec_vbyte_len(arr[i]->last_docno)
              + vec_vbyte_len(arr[i]->vec.pos - arr[i]->vecmem));

            /* have enough space, copy stuff into buffer */
            bytes = vec_vbyte_write(&v, len);
            assert(bytes);
            bytes = vec_byte_write(&v, arr[i]->term, len);
            assert(bytes == len);
            bytes = vec_vbyte_write(&v, arr[i]->docs);
            assert(bytes);
            bytes = vec_vbyte_write(&v, arr[i]->occurs);
            assert(bytes);
            bytes = vec_vbyte_write(&v, arr[i]->last_docno);
            assert(bytes);
            bytes = vec_vbyte_write(&v, arr[i]->vec.pos - arr[i]->vecmem);
            assert(bytes);

            /* copy the inverted list in */
            pos = 0;
            while (((unsigned int) VEC_LEN(&v)) 
              < (arr[i]->vec.pos - arr[i]->vecmem) - pos) {

                /* copy last bit we can in */
                pos += vec_byte_write(&v, arr[i]->vecmem + pos, VEC_LEN(&v));

                /* write the buffer out */
                len = v.pos - dbuf;
                dbufpos = dbuf;

                while (len && ((wlen = write(fd, dbufpos, len)) >= 0)) {
                    len -= wlen;
                    dbufpos += wlen;
                }

                if (len) {
                    free(arr);
                    return 0;
                }

                v.pos = dbuf;
                v.end = dbuf + dbufsz;
            }

            /* copy last bit of inverted list in */
            pos += vec_byte_write(&v, arr[i]->vecmem + pos, 
              (arr[i]->vec.pos - arr[i]->vecmem) - pos);
            assert(arr[i]->vecmem + pos == arr[i]->vec.pos);

            free(arr[i]->vecmem);

            i++;
        }

        /* write the buffer out */
        len = v.pos - dbuf;
        dbufpos = dbuf;

        while (len && ((wlen = write(fd, dbufpos, len)) >= 0)) {
            len -= wlen;
            dbufpos += wlen;
        }

        if (len) {
            free(arr);
            return 0;
        }

        v.pos = dbuf;
        v.end = dbuf + dbufsz;
    }

    /* reinitialise hash table */
    post->size = 0;
    post->dterms = 0;
    post->terms = 0;
    post->docs = 0;
    poolalloc_clear(post->string_mem);
    objalloc_clear(post->node_mem);
 
    free(arr);

    return 1;
}

void postings_clear(struct postings* post) {
    unsigned int i;
    struct postings_node* node;

    /* free memory from postings */
    for (i = 0; i < post->tblsize; i++) {
        if ((node = post->hash[i])) {
            /* write it out */
            do {
                /* free memory (don't use vec_free thanks to our initialisation
                 * hack */
                free(node->vecmem);
            } while ((node = node->next));

            /* reset hash node (memory free'd below) */
            post->hash[i] = NULL;
        }
    }

    /* reinitialise hash table */
    post->size = 0;
    post->dterms = 0;
    post->terms = 0;
    post->docs = 0;
    poolalloc_clear(post->string_mem);
    objalloc_clear(post->node_mem);
    
    return;
}

unsigned int postings_size(struct postings* post) {
    return post->size;
}

unsigned int postings_memsize(struct postings* post) {
    return sizeof(struct postings_node) * post->dterms 
      + (sizeof(*post->hash) * post->tblsize) 
      + sizeof(struct postings) + post->size;
}

int postings_err(struct postings* post) {
    return post->err;
}

unsigned int postings_total_terms(struct postings *post) {
    return post->terms;
}

unsigned int postings_distinct_terms(struct postings *post) {
    return post->dterms;
}

unsigned int postings_documents(struct postings *post) {
    return post->docs;
}

struct vec *postings_vector(struct postings *post, char *term) {
    unsigned int hash;
    struct postings_node *node;

    if (!post->stop || (stop_stop(post->stop, term) == STOP_OK)) {
        hash = str_hash(term) & bit_lmask(post->tblbits);
        node = post->hash[hash];
        while (node) {
            if (!str_cmp(term, node->term)) {
                return &node->vec;
            }
        }
    }

    return NULL;
}

