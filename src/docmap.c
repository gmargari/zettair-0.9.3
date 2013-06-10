/* docmap.c implements the interface declared in docmap.h.  The docmap holds
 * all per-document information in the search engine.  As of the below date, 
 * the design has been changed and a new implementation written (also renamed
 * ndocmap back to docmap).
 *
 * The new design is essentially a paged array.  The docmap is split into pages
 * of a given size, with each page consisting of a small header, and then some
 * data.  Pages may be either data or cache pages, where data pages hold the
 * 'true' docmap record, and cache pages are used to load data into memory
 * quickly between invocations.
 * Each entry is serially encoded into a data page
 * utilising simple inter- and intra-entry compression (so that 
 * they must be read as a stream).  Specifically, all integer entries are
 * vbyte-encoded, and all text entries associated with each entry are
 * front-coded with respect to the previous entry.
 * Unlike the previous design, this docmap holds all elements of an entry
 * contiguously on disk.
 *
 * The docmap then has an append buffer, which holds new documents in main
 * memory prior to them migrating to disk, and a read buffer.  
 * Both are an integral number of pages.  
 * In addition to these buffers, the user can request that certain quantities 
 * be cached in main memory, where that quantity is individually held as 
 * an array.  Cache pages are read/written at the load/save of the docmap to 
 * move cached items in/out of memory quickly.
 *
 * The data pages are indexed by an in-memory sorted array, mapping the first
 * docno contained within each page to its location.  This information and 
 * other aggregate information is written to the end of the docmap on shutdown,
 * in cache pages.
 *
 * The format of each data page is: 
 *   - the constant byte '0xda', or '0xdf' for the final data page
 *   - number of entries in this page (4 byte big-endian integer)
 *   - for each entry:
 *     - fileno gap + 1 or 0 (vbyte).  0 indicates that this document 
 *       immediately follows the previous, and no offset field is present
 *     - (optional, see above) offset field (vbyte)
 *     - docno gap (since docnos strictly ascend - vbyte)
 *     - flags (shifted onto docno gap vbyte)
 *     - distinct words in document (vbyte)
 *     - words - distinct_words (since words >= distinct_words, vbyte)
 *     - bytes - (2 * words - 1) (since each word must occupy at least two 
 *       bytes, except the first. vbyte)
 *     - mime type of document
 *     - TREC docno (front-coded):
 *       - number of prefix bytes from previous TREC docno (vbyte)
 *       - length of suffix bytes (vbyte)
 *       - suffix bytes
 *     - weight (float)
 *
 * this implementation doesn't currently handle deletion, but has been designed
 * so that adding it shouldn't be too much hassle.  The in-memory arrays can be
 * limited by knowing the smallest active entry.  The map can be made a true
 * search structure (such as rbtree) instead of just a sorted array.  Adjacent
 * pages can be combined when they become sufficiently sparse, and freed pages
 * reused for new content.
 *
 * The format of each cache page is:
 *   - the constant byte '0xca', or constant byte '0xcf' for the final cache 
 *     page
 *   - the last document number in the docmap (acts as a timestamp - vbyte)
 *   - the page number of the first cache page (vbyte)
 *   - a series of self-delimiting entries containing cache information:
 *     - byte 0x00 meaning end-of-page
 *     - byte 0x02, followed by the aggregate quantities (sum_bytes, sum_words,
 *       sum_dwords and sum_weight) written as a vbyte normalised fraction
 *       (multiplied by UINT32_MAX) and a vbyte exponent
 *     - byte 0x03, followed by a map cache entry:
 *       - vbyte number of entries in the map 
 *       - vbyte offset of following entries in the map
 *       - uint32_t number of following entries from the map
 *       - a series of vbyte map entries
 *       - note that no entry shall exceed a page, and that the offsets
 *         encountered should form a sequence across pages
 *     - byte 0x4, followed by word array entry, same format as map array
 *     - byte 0x5, followed by dword array entry, same format as word array
 *     - byte 0x6, followed by bytes array entry, same format as word array
 *     - byte 0x7, followed by weight array entry, same format as word array,
 *       except that weights are stored like aggregate doubles
 *     - byte 0x8, followed by trecno offset array entry, same format as word
 *       array
 *     - byte 0x9, followed by trecno font-coded entry:
 *       - vbyte number of characters in array
 *       - vbyte offset of following entry in array 
 *       - vbyte number of characters in following entry
 *       - a character array of front-coded trecno entries
 *
 * There are a few in-memory tricks which i've used to reduce the amount of
 * space required to cache elements.  The first is to use 3-in-4
 * front-coding of TREC docno entries.  This means that only every fourth entry
 * is wholly represented, with three subsequent entries front-coded against it.
 * All entries are stored in a character array, with an array of pointers into
 * the char array for reasonably fast random access.  Last time i checked, this
 * saved 50% of the space required for sensibly ordered TREC docnos.
 *
 * A process similar to 3-in-4 front-coding is used for the location array.  
 * The array records vbyte'd byte counts for each document.  Every 8 entries, 
 * an offset into the array is recorded for faster random access.  In addition,
 * if that record has a non-zero offset (which we can tell through the reposset
 * structure) then the offset is recorded before the vbyte bytes entry.
 * This provides access to the byte counts, but additionally allows the offset
 * to be reconstructed for any record by decoding at most 8 bytes records.
 * 
 * To complete the in-memory caching of location information, document types 
 * are recorded in a sorted in-memory array if they are not of type
 * MIME_TYPE_APPLICATION_X_TREC (yes, this is a bit hacky).
 *
 * There are still some flaws in the current implementation (XXX).  Firstly, 
 * the empty docmap has no valid representation.  
 * Also, the number of entries on a page can change during reading, which won't
 * be picked up.  Also, there's some confusion about whether buffers point to a
 * series of pages or to a single page.  
 * 
 * written nml 2006-04-06 
 *
 */

#include "firstinclude.h"

#include "_docmap.h"
#include "docmap.h"

#include "def.h"
#include "binsearch.h"
#include "fdset.h"
#include "mem.h"
#include "reposset.h"
#include "_reposset.h"
#include "str.h"
#include "timings.h"
#include "vec.h"
#include "zstdint.h"
#include "zvalgrind.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* frequency of front-coding of trecno's.  For example, a value of 4 sets a
 * policy of 3-in-4 front coding, where only every fourth entry is not
 * front-encoded */
#define TRECNO_FRONT_FREQ 4

/* frequency of relative coding of locations. */
#define LOC_REL_FREQ 8

/* initial memory allocated to various arrays */
#define INIT_LEN 8

/* character constants for identifying pages */
#define FINAL_DATA_BYTE ((char) 0xdf)
#define DATA_BYTE ((char) 0xda)
#define CACHE_BYTE ((char) 0xca)
#define FINAL_CACHE_BYTE ((char) 0xcf)

#define DOCMAP_WEIGHT_PRECISION 7

enum cache_id {
    CACHE_ID_END = 0,
    CACHE_ID_AGG = 0x01,
    CACHE_ID_MAP = 0x02,
    CACHE_ID_WORDS = 0x03,
    CACHE_ID_DWORDS = 0x04,
    CACHE_ID_WEIGHT = 0x07,
    CACHE_ID_TRECNO = 0x08,
    CACHE_ID_TRECNO_CODE = 0x09,
    CACHE_ID_REPOS_REC = 0x0a,
    CACHE_ID_REPOS_CHECK = 0x0b,
    CACHE_ID_LOC = 0x0c,
    CACHE_ID_LOC_CODE = 0x0d,
    CACHE_ID_TYPEEX = 0x0e
};

/* internal function to make a cursor point to nothing */
static void invalidate_cursor(struct docmap_cursor *cur) {
    cur->buf = NULL;
    cur->page = -1;
    cur->entry.docno = -1;
    cur->entry.fileno = -1;
    cur->entry.offset = -1;
    cur->entry.bytes = 0;
    cur->first_docno = cur->last_docno = -1;
    cur->pos.pos = cur->pos.end = NULL;
}

/* internal function to invalidate a buffer entry and the cursors that point to
 * it */
static void invalidate_buffer(struct docmap *dm, struct docmap_buffer *buf) {
    if (dm->read.buf == buf) {
        invalidate_cursor(&dm->read);
    }
    if (dm->write.buf == buf) {
        invalidate_cursor(&dm->write);
    }
    buf->page = -1;
    buf->buflen = 0;
    buf->dirty = 0;
}

/* internal function to initialise a docmap structure, used for both docmap_new
 * and docmap_load */
static struct docmap *docmap_init(struct fdset *fdset, 
  int fd_type, unsigned int pagesize, unsigned int pages, 
  unsigned long int max_filesize, enum docmap_cache cache) {
    struct docmap *dm = malloc(sizeof(*dm));

    if (pages < 2) {
        pages = 2;
    }

    if (dm && (dm->buf = malloc(pagesize * pages)) 
      && (dm->map = malloc(sizeof(*dm->map) * INIT_LEN))
      && (dm->rset = reposset_new())) {
        dm->map_size = INIT_LEN;
        dm->map_len = 0;
        dm->map[dm->map_len] = ULONG_MAX;

        dm->file_pages = max_filesize / pagesize;
        dm->fdset = fdset;
        dm->fd_type = fd_type;
        dm->pagesize = pagesize;
        dm->entries = 0;

        /* assign all pages to reading, append buffering will steal them later
         * if necessary */
        invalidate_cursor(&dm->read);
        invalidate_cursor(&dm->write);
        invalidate_buffer(dm, &dm->readbuf);
        invalidate_buffer(dm, &dm->appendbuf);
        dm->read.entry.trecno = dm->write.entry.trecno = NULL;
        dm->read.entry.trecno_len = dm->read.entry.trecno_size = 0;
        dm->write.entry.trecno_len = dm->write.entry.trecno_size = 0;
        dm->readbuf.buf = dm->buf;
        dm->readbuf.bufsize = pages;

        dm->appendbuf.page = 0;      /* append gets page 0 because it has no 
                                      * buflen */
        dm->appendbuf.buf = dm->buf;
        dm->appendbuf.bufsize = 0;

        dm->dirty = 0;           

        dm->cache.cache = cache;
        dm->cache.len = dm->cache.size = 0;
        dm->cache.words = NULL;
        dm->cache.dwords = NULL;
        dm->cache.weight = NULL;
        dm->cache.loc_off = NULL;
        dm->cache.loc.buf = NULL;
        dm->cache.loc.len = dm->cache.loc.size = 0;
        dm->cache.trecno_off = NULL;
        dm->cache.trecno.buf = NULL;
        dm->cache.trecno.len = dm->cache.trecno.size = 0;
        dm->cache.typeex = NULL;
        dm->cache.typeex_len = dm->cache.typeex_size = 0;

        dm->agg.avg_bytes = dm->agg.sum_bytes 
          = dm->agg.avg_weight = dm->agg.sum_weight 
          = dm->agg.avg_words = dm->agg.sum_words 
          = dm->agg.avg_dwords = dm->agg.sum_dwords
          = dm->agg.sum_trecno = 0;
    } else {
        if (dm) {
            if (dm->buf) {
                free(dm->buf);
                dm->buf = NULL;
            }
            free(dm);
            dm = NULL;
        }
    }

    return dm;
}

/* internal function to encode an entry relative to a previous entry into 
 * some space. */
static enum docmap_ret encode(struct vec *v, struct docmap_entry *prev, 
  struct docmap_entry *curr) {
    uintmax_t tmp[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned int arrlen = sizeof(tmp) / sizeof(*tmp),
                 prefix,
                 len,
                 bytes,
                 i = 0;
    char *pos = v->pos;

    /* figure out the length of the front-coding prefix between trecnos */
    if (curr->trecno_len < prev->trecno_len) {
        len = curr->trecno_len;
    } else {
        len = prev->trecno_len;
    }
    for (prefix = 0; 
      prefix < len 
        && curr->trecno[prefix] == prev->trecno[prefix]; prefix++) ;
    assert(prefix <= len && prefix <= curr->trecno_len);

    if (curr->fileno == prev->fileno 
      && curr->offset == prev->offset + (off_t) prev->bytes) {
        /* document immediately follows the previous */
        tmp[i++] = 0;
        arrlen--;
    } else {
        assert(curr->fileno >= prev->fileno || prev->fileno == -1);
        tmp[i++] = 1 + curr->fileno - prev->fileno;
        assert(tmp[i - 1] > 0);  /* CANNOT be zero */
        tmp[i++] = curr->offset;
    }

    /* encode docno gap and flags into one integer */
    tmp[i] = curr->docno - prev->docno;
    tmp[i] <<= 1;
    tmp[i++] |= curr->flags;

    tmp[i++] = curr->dwords;
    tmp[i++] = curr->words - curr->dwords;
    tmp[i++] = curr->bytes + 1 - 2 * curr->words;
    tmp[i++] = curr->mtype;
    tmp[i++] = prefix;
    tmp[i++] = curr->trecno_len - prefix;

    if ((vec_maxint_arr_write(v, tmp, arrlen, &bytes) == arrlen)
      && vec_byte_write(v, curr->trecno + prefix, 
        (unsigned int) tmp[arrlen - 1]) == tmp[arrlen - 1]
      && vec_flt_write(v, curr->weight, DOCMAP_WEIGHT_PRECISION)) {

        /* Relations that we use to compress entries.  These assertions go 
         * after encoding so that we don't assert things if encoding fails) */
        assert(curr->words >= curr->dwords);
        assert(curr->bytes + 1 >= 2 * curr->words);
        return DOCMAP_OK;
    } else {
        v->pos = pos;
        return DOCMAP_BUFSIZE_ERROR;
    }
}

/* internal function to decode an entry from some space.  Note that target must
 * contain the previous entry for the next entry to be correctly decoded. */
static enum docmap_ret decode(struct docmap_cursor *cur) {
    struct vec *v = &cur->pos; 
    struct docmap_entry *target = &cur->entry;
    uintmax_t tmp[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned int i,
                 bytes,
                 readlen,
                 arrlen = sizeof(tmp) / sizeof(*tmp);
    char *pos = v->pos;

    if (cur->past < cur->entries 
      && vec_maxint_arr_read(v, tmp, 1, &readlen)) {
        /* have to examine fileno before we can tell how many more numbers to
         * read */
        if (!tmp[0]) {
            /* fileno indicates that this doc starts immediately after prev */
            i = 2;
            tmp[0] = target->fileno;
            tmp[1] = target->offset + target->bytes;
        } else {
            tmp[0] += target->fileno - 1;
            i = 1;
            /* offset will be read below */
        }

        if (vec_maxint_arr_read(v, &tmp[i], arrlen - i, &bytes)) {
            /* ensure that we've got enough space for the trecno */
            if (target->trecno_size < tmp[arrlen - 2] + tmp[arrlen - 1] + 1) {
                void *ptr = realloc(target->trecno, (unsigned int)
                    (tmp[arrlen - 2] + tmp[arrlen - 1] + 1));

                if (ptr) {
                    target->trecno = ptr;
                    target->trecno_size = (unsigned int) 
                      (tmp[arrlen - 2] + tmp[arrlen - 1] + 1);
                } else {
                    assert(!CRASH);
                    v->pos = pos;
                    return DOCMAP_MEM_ERROR;
                }
            }

            i = 0;
            assert(tmp[0] != target->fileno || tmp[1] >= target->offset);
            target->fileno = (unsigned int) tmp[i++];
            target->offset = (unsigned long int) tmp[i++];
            target->flags = (int) (tmp[i] & 1);
            target->docno += (((unsigned int) tmp[i++]) >> 1);
            target->dwords = (unsigned int) tmp[i++];
            target->words = target->dwords + (unsigned int) tmp[i++];
            target->bytes = (unsigned int) tmp[i++] + 2 * target->words - 1;
            target->mtype = (unsigned int) tmp[i++];

            if (vec_byte_read(v, target->trecno + tmp[i], 
                (unsigned int) tmp[i + 1]) == tmp[i + 1]
              && vec_flt_read(v, &target->weight, DOCMAP_WEIGHT_PRECISION)) {
                target->trecno_len = (unsigned int) tmp[i] 
                  + (unsigned int) tmp[i + 1];
                target->trecno[target->trecno_len] = '\0';
                cur->past++;
                return DOCMAP_OK;
            } else {
                /* bugger, we've destroyed the previous entry */
                assert("can't get here" && 0);
                return DOCMAP_BUFSIZE_ERROR;
            }
        }
        return DOCMAP_OK;
    }

    v->pos = pos;
    return DOCMAP_BUFSIZE_ERROR;
}

/* internal method to transfer a buffer from the read buffer to the append
 * buffer */
static enum docmap_ret take_read_buffer(struct docmap *dm) {
    assert(!dm->readbuf.dirty);
    assert(dm->readbuf.bufsize > 1);
    if (!dm->readbuf.bufsize) {
        return DOCMAP_ARG_ERROR;
    }

    dm->readbuf.buf += dm->pagesize;
    dm->readbuf.page++;
    dm->readbuf.bufsize--;
    if (dm->readbuf.buflen >= dm->readbuf.bufsize) {
        dm->readbuf.buflen = dm->readbuf.bufsize;
    }
    if (dm->read.pos.pos >= dm->readbuf.buf && dm->read.pos.pos 
      < dm->readbuf.buf + dm->pagesize * dm->readbuf.buflen) {
        /* do nothing */
    } else {
        invalidate_cursor(&dm->read);
    }

    dm->appendbuf.bufsize++;

    return DOCMAP_OK;
}

static void zero_entry(struct docmap_entry *e) {
    e->docno = 0;
    e->fileno = 0;
    e->offset = 0;
    e->bytes = 0;
    e->trecno_len = 0;
}

/* internal function to initialise the next page in the append buffer */
static enum docmap_ret init_append_buffer(struct docmap *dm, 
  unsigned long int docno) {

    while (dm->map_len + 1 >= dm->map_size) {
        void *ptr = realloc(dm->map, 
            sizeof(*dm->map) * (dm->map_size * 2 + 1));

        if (ptr) {
            dm->map = ptr;
            dm->map_size *= 2;
            dm->map_size++;
        } else {
            assert(!CRASH);
            return DOCMAP_MEM_ERROR;
        }
    }

    assert(dm->appendbuf.buflen < dm->appendbuf.bufsize);
    dm->appendbuf.dirty = 1;
    dm->write.pos.pos 
      = dm->appendbuf.buf + dm->appendbuf.buflen * dm->pagesize;
    dm->write.pos.end = dm->write.pos.pos + dm->pagesize;
    dm->write.entries = 0;
    zero_entry(&dm->write.entry);
    dm->appendbuf.buflen++;

    dm->map[dm->map_len++] = docno;
    dm->map[dm->map_len] = ULONG_MAX;

    *dm->write.pos.pos++ = DATA_BYTE;
    dm->write.pos.pos += sizeof(uint32_t);       /* skip space for entries */
    assert(vec_len(&dm->write.pos));
    if (DEAR_DEBUG) {
        memset(dm->write.pos.pos, 0, vec_len(&dm->write.pos));
    }
    return DOCMAP_OK;
}

/* internal function to commit a dirty buffer to disk */
static enum docmap_ret commit(struct docmap *dm, struct docmap_buffer *buf) {
    unsigned int page = buf->page,
                 pages = buf->buflen,
                 pagesize = dm->pagesize,
                 fileno,
                 target,
                 bytes;
    char *pos = buf->buf;
    unsigned long int offset;
    int fd;

    assert(buf->dirty);

    /* note that buffers may span multiple files */
    do {
        fileno = page / dm->file_pages;
        offset = page % dm->file_pages;
        target = pages;
        if (offset + target > dm->file_pages) {
            target = dm->file_pages - offset;
            assert(target);
        }

        VALGRIND_CHECK_READABLE(pos, target * pagesize);
        if (((fd = fdset_pin(dm->fdset, dm->fd_type, fileno, offset * pagesize,
            SEEK_SET)) >= 0) 
          && (bytes = write(fd, pos, target * pagesize)) == target * pagesize
          && fdset_unpin(dm->fdset, dm->fd_type, fileno, fd) == FDSET_OK) {
            pages -= target;
            pos += bytes;
        } else {
            if (fd >= 0) {
                fdset_unpin(dm->fdset, dm->fd_type, fileno, fd);
            }
            return DOCMAP_IO_ERROR;
        }
    } while (pages);

    buf->dirty = 0;
    return DOCMAP_OK;
}

/* internal function to recalculate aggregate values */
static void aggregate(struct docmap *dm) {
    if (dm->entries) {
        dm->agg.avg_words = dm->agg.sum_words / dm->entries;
        dm->agg.avg_dwords = dm->agg.sum_dwords / dm->entries;
        dm->agg.avg_bytes = dm->agg.sum_bytes / dm->entries;
        dm->agg.avg_weight = dm->agg.sum_weight / dm->entries;
    } else {
        dm->agg.avg_words = dm->agg.avg_dwords = dm->agg.avg_bytes 
          = dm->agg.avg_weight = 0;
    }
}

static enum docmap_ret cbuf_realloc(struct docmap_cbuf *buf) {
    void *ptr = realloc(buf->buf, buf->size * 2 + 1);

    if (ptr) {
        buf->buf = ptr;
        buf->size *= 2;
        buf->size++;
        return DOCMAP_OK;
    } else {
        assert(!CRASH);
        return DOCMAP_MEM_ERROR;
    }
}

static enum docmap_ret cache(struct docmap *dm, struct docmap_entry *entry) {
    enum docmap_ret dmret;

    assert(dm->cache.len == entry->docno);
    if (dm->cache.cache & DOCMAP_CACHE_WORDS) {
        dm->cache.words[entry->docno] = entry->words;
    }
    if (dm->cache.cache & DOCMAP_CACHE_DISTINCT_WORDS) {
        dm->cache.dwords[entry->docno] = entry->dwords;
    }
    if (dm->cache.cache & DOCMAP_CACHE_WEIGHT) {
        dm->cache.weight[entry->docno] = entry->weight;
    }

    if (dm->cache.cache & DOCMAP_CACHE_LOCATION) {
        /* look up fileno of this entry */
        struct vec v;
        struct reposset_record *rec = reposset_record(dm->rset, entry->docno);
        enum docmap_ret dmret;
        unsigned int bytes;
        assert(rec);

        v.pos = dm->cache.loc.buf + dm->cache.loc.len;
        v.end = dm->cache.loc.buf + dm->cache.loc.size;

        if (!(entry->docno % LOC_REL_FREQ)) {
            /* need to add random access for this entry */
            dm->cache.loc_off[entry->docno / LOC_REL_FREQ] = dm->cache.loc.len;

            if (rec->rectype == REPOSSET_SINGLE_FILE 
              && rec->docno != entry->docno) {
                /* need to record offset for this entry, since it's in a
                 * many-docs file and it is not the first entry. */
                uintmax_t arr = entry->offset;

                while (!(vec_maxint_arr_write(&v, &arr, 1, &bytes))) {
                    if ((dmret = cbuf_realloc(&dm->cache.loc)) == DOCMAP_OK) {
                        v.pos = dm->cache.loc.buf + dm->cache.loc.len;
                        v.end = dm->cache.loc.buf + dm->cache.loc.size;
                    } else {
                        return dmret;
                    }
                }
                dm->cache.loc.len += bytes;
            }
        }

        /* write the number of bytes for this doc into the loc buffer */
        while (!(bytes = vec_vbyte_write(&v, entry->bytes))) {
            if ((dmret = cbuf_realloc(&dm->cache.loc)) == DOCMAP_OK) {
                v.pos = dm->cache.loc.buf + dm->cache.loc.len;
                v.end = dm->cache.loc.buf + dm->cache.loc.size;
            } else {
                return dmret;
            }
        }
        dm->cache.loc.len += bytes;

        /* record type if it's not X_TREC */
        if (entry->mtype != MIME_TYPE_APPLICATION_X_TREC) {
            /* ensure that we have enough memory */
            if (dm->cache.typeex_len >= dm->cache.typeex_size) {
                void *ptr = realloc(dm->cache.typeex, 
                  sizeof(*dm->cache.typeex) * (dm->cache.typeex_size * 2 + 1));
                
                if (ptr) {
                    dm->cache.typeex = ptr;
                    dm->cache.typeex_size *= 2;
                    dm->cache.typeex_size++;
                } else {
                    assert(!CRASH);
                    return DOCMAP_MEM_ERROR;
                }
            }

            dm->cache.typeex[dm->cache.typeex_len].docno = entry->docno;
            dm->cache.typeex[dm->cache.typeex_len++].mtype = entry->mtype;
        }
    }

    if (dm->cache.cache & DOCMAP_CACHE_TRECNO) {
        struct vec v;
        struct vec readv;
        unsigned int index = entry->docno / TRECNO_FRONT_FREQ;
        unsigned int offset = entry->docno % TRECNO_FRONT_FREQ;
        int encoded;
        unsigned int front = 0;

        v.pos = dm->cache.trecno.buf + dm->cache.trecno.len;
        v.end = dm->cache.trecno.buf + dm->cache.trecno.size;

        if (offset) {
            unsigned int i,
                         ret;
            unsigned long int len,
                              prefix;

            encoded = 1;

            /* front-code against unencoded entry */
            readv.pos = dm->cache.trecno.buf + dm->cache.trecno_off[index];
            readv.end = dm->cache.trecno.buf + dm->cache.trecno.len;
            ret = vec_vbyte_read(&readv, &len);
            assert(ret);
            for (front = 0; 
              front < len && readv.pos[front] == entry->trecno[front]; 
                front++) ;
            readv.pos += len;

            /* propagate front-code through similarly coded entries */
            for (i = 0; i + 1 < offset; i++) {
                ret = vec_vbyte_read(&readv, &prefix);
                assert(ret);
                ret = vec_vbyte_read(&readv, &len);
                assert(ret);

                assert(vec_len(&readv) >= len);
                if (front >= prefix) {
                    /* front may only match the length of the prefix, but can 
                     * be extended to match the rest of the string */
                    for (front = prefix; 
                      front < len + prefix && readv.pos[front - prefix] 
                          == entry->trecno[front]; front++) ;
                }
                readv.pos += len;
            }
        } else {
            /* don't front-encode this entry, just write it in */
            encoded = 0;
            dm->cache.trecno_off[index] = dm->cache.trecno.len;
        }

        /* write either unencoded or front-coded entry */
        while ((encoded && !vec_vbyte_write(&v, front)) 
          || !vec_vbyte_write(&v, entry->trecno_len - front)
          || vec_byte_write(&v, entry->trecno + front, 
              entry->trecno_len - front) != entry->trecno_len - front) {

            if ((dmret = cbuf_realloc(&dm->cache.trecno)) == DOCMAP_OK) {
                v.pos = dm->cache.trecno.buf + dm->cache.trecno.len;
                v.end = dm->cache.trecno.buf + dm->cache.trecno.size;
            } else {
                return dmret;
            }
        }
        dm->cache.trecno.len = v.pos - dm->cache.trecno.buf;
    }

    dm->cache.len++;
    return DOCMAP_OK;
}

static enum docmap_ret cache_realloc(struct docmap *dm) {
    void *ptr;

    if (dm->cache.cache & DOCMAP_CACHE_WORDS) {
        if ((ptr = realloc(dm->cache.words, 
            sizeof(*dm->cache.words) * dm->cache.size))) {

            dm->cache.words = ptr;
        } else {
            assert(!CRASH);
            return DOCMAP_MEM_ERROR;
        }
    }
    if (dm->cache.cache & DOCMAP_CACHE_DISTINCT_WORDS) {
        if ((ptr = realloc(dm->cache.dwords, 
            sizeof(*dm->cache.dwords) * dm->cache.size))) {

            dm->cache.dwords = ptr;
        } else {
            assert(!CRASH);
            return DOCMAP_MEM_ERROR;
        }
    }
    if (dm->cache.cache & DOCMAP_CACHE_WEIGHT) {
        if ((ptr = realloc(dm->cache.weight, 
            sizeof(*dm->cache.weight) * dm->cache.size))) {

            dm->cache.weight = ptr;
        } else {
            assert(!CRASH);
            return DOCMAP_MEM_ERROR;
        }
    }
    if (dm->cache.cache & DOCMAP_CACHE_TRECNO) {
        if ((ptr = realloc(dm->cache.trecno_off, sizeof(*dm->cache.trecno_off) 
          * (dm->cache.size / TRECNO_FRONT_FREQ + 1)))) {

            dm->cache.trecno_off = ptr;
        } else {
            assert(!CRASH);
            return DOCMAP_MEM_ERROR;
        }
    }
    if (dm->cache.cache & DOCMAP_CACHE_LOCATION) {
        if ((ptr = realloc(dm->cache.loc_off, sizeof(*dm->cache.loc_off) 
          * (dm->cache.size / LOC_REL_FREQ + 1)))) {

            dm->cache.loc_off = ptr;
        } else {
            assert(!CRASH);
            return DOCMAP_MEM_ERROR;
        }
    }

    return DOCMAP_OK;
}

void update_append_entries(struct docmap *dm) {
    /* first update entries in current append page */
    mem_hton(dm->appendbuf.buf 
      + (dm->appendbuf.buflen - 1) * dm->pagesize + 1, 
      &dm->write.entries, sizeof(dm->write.entries));
}

enum docmap_ret docmap_add(struct docmap *dm, 
  unsigned int fileno, off_t offset, 
  unsigned int bytes, enum docmap_flag flags, 
  unsigned int words, unsigned int distinct_words,
  float weight, const char *trecno, unsigned trecno_len, enum mime_types mtype,
  unsigned long int *docno) {
    enum docmap_ret dmret;
    struct docmap_entry entry;
    char *tmp;
    unsigned int tmplen,
                 reposno;
    enum reposset_ret rret;

    assert(offset >= 0);

    /* copy parameters into entry */
    entry.docno = dm->entries;
    entry.fileno = fileno;
    entry.offset = offset;
    entry.bytes = bytes;
    entry.words = words;
    entry.dwords = distinct_words;
    entry.flags = flags;
    entry.mtype = mtype;
    entry.weight = weight;
    entry.trecno = (char *) trecno;
    entry.trecno_size = entry.trecno_len = trecno_len;

    /* fiddle reposset in response to new docno */
    assert(!entry.docno 
      || reposset_reposno(dm->rset, entry.docno - 1, &reposno) == REPOSSET_OK);
    if (!offset) {
        unsigned int reposno;

        if ((rret = reposset_append(dm->rset, entry.docno, &reposno)) 
          == REPOSSET_OK) {
            assert(reposno == entry.fileno);
        } else {
            assert(!CRASH);
            return DOCMAP_MEM_ERROR;
        }

        /* add a checkpoint at the start of compressed repositories */
        if (entry.flags & DOCMAP_COMPRESSED) {
            rret = reposset_add_checkpoint(dm->rset, entry.fileno, 
                MIME_TYPE_APPLICATION_X_GZIP, 0);
            if (rret != REPOSSET_OK) {
                assert(!CRASH);
                return DOCMAP_MEM_ERROR;
            }
        }
    } else if ((rret = reposset_append_docno(dm->rset, entry.docno, 1)) 
      != REPOSSET_OK) {
        assert(!CRASH);
        return DOCMAP_MEM_ERROR;
    }
    /* check that previous reposno hasn't changed */
    assert(!entry.docno 
      || (reposset_reposno(dm->rset, entry.docno - 1, &tmplen) == REPOSSET_OK
        && tmplen == reposno));
    /* check that this reposno is as expected */
    assert(reposset_reposno(dm->rset, entry.docno, &tmplen) == REPOSSET_OK
      && tmplen == entry.fileno);

    /* ensure we've got enough space to store docno *before* we encode */
    while (dm->write.entry.trecno_size <= entry.trecno_len) {
        void *ptr = realloc(dm->write.entry.trecno, 
            dm->write.entry.trecno_size * 2 + 1);

        if (ptr) {
            dm->write.entry.trecno = ptr;
            dm->write.entry.trecno_size *= 2;
            dm->write.entry.trecno_size++;
        } else {
            assert(!CRASH);
            return DOCMAP_MEM_ERROR;
        }
    }

    /* ensure we've got enough space for cache */
    if (dm->cache.cache && dm->cache.len >= dm->cache.size) {
        dm->cache.size = dm->cache.size * 2 + 1;
        if ((dmret = cache_realloc(dm)) != DOCMAP_OK) {
            dm->cache.size = (dm->cache.size - 1) / 2;
            return dmret;
        }
    }

    while ((dmret = encode(&dm->write.pos, &dm->write.entry, &entry)) 
      != DOCMAP_OK) {
        switch (dmret) {
        case DOCMAP_OK: assert("can't get here" && 0); break;
        default: return dmret;

        case DOCMAP_BUFSIZE_ERROR:
            /* need to move to the next page */

            if (dm->appendbuf.buflen) {
                update_append_entries(dm);
            }

            assert(dm->readbuf.bufsize >= 1);
            if (dm->readbuf.bufsize == 1) {
                /* no more buffer space.  commit current contents and then 
                 * give all buffer back to read */
                if ((!dm->readbuf.dirty 
                    || (dmret = commit(dm, &dm->readbuf)) == DOCMAP_OK)
                  && (!dm->appendbuf.dirty 
                    || ((dmret = commit(dm, &dm->appendbuf)) == DOCMAP_OK))) {

                    /* remove the append buffer, giving it all back to the read
                     * buffer */
                    assert(dm->appendbuf.buflen == dm->appendbuf.bufsize);
                    invalidate_buffer(dm, &dm->readbuf);
                    dm->appendbuf.page += dm->appendbuf.buflen;
                    dm->appendbuf.buflen = 0;
                    dm->readbuf.bufsize += dm->appendbuf.bufsize;
                    dm->readbuf.buf = dm->buf;
                    dm->appendbuf.bufsize = 0;

                    /* invalidate write cursor */
                    invalidate_cursor(&dm->write);
                } else {
                    return dmret;
                }
            }

            /* see if we need to create a new file for the append page we're
             * about to create */
            if (!((dm->appendbuf.page + dm->appendbuf.buflen) 
              % dm->file_pages) && dm->appendbuf.page) {
                /* create new file (not for first file though - docmap_new 
                 * handles that) */
                unsigned int dmfileno = (dm->appendbuf.page 
                  + dm->appendbuf.buflen) / dm->file_pages;
                /* need to initialise a new file for this page */
                int fd = fdset_create(dm->fdset, dm->fd_type, dmfileno);
                if (fd >= 0) {
                    fdset_unpin(dm->fdset, dm->fd_type, dmfileno, fd);
                } else {
                    return DOCMAP_IO_ERROR;
                }
            }

            /* steal a page from the read buffer */
            if ((dmret = take_read_buffer(dm)) != DOCMAP_OK) {
                return dmret;
            }

            /* initialise buffer */
            if ((dmret = init_append_buffer(dm, dm->entries)) != DOCMAP_OK) {

                return dmret;
            }

            aggregate(dm); /* recalculate aggregates */
            break;
        }
    }

    dm->dirty = 1;
    dm->appendbuf.dirty = 1;
    dm->write.entries++;

    /* update stored entry in write cursor */
    tmp = dm->write.entry.trecno;
    tmplen = dm->write.entry.trecno_size;
    dm->write.entry = entry;
    dm->write.entry.trecno = tmp;
    dm->write.entry.trecno_size = tmplen;
    assert(dm->write.entry.trecno_size > entry.trecno_len);
    memcpy(dm->write.entry.trecno, entry.trecno, entry.trecno_len);
    dm->write.entry.trecno[entry.trecno_len] = '\0';

    if (dm->cache.cache) {
        assert(dm->cache.len < dm->cache.size);
        if ((dmret = cache(dm, &dm->write.entry)) != DOCMAP_OK) {
            return dmret;
        }
    }
    /* update aggregate entries */
    dm->agg.sum_bytes += bytes;
    dm->agg.sum_words += words;
    dm->agg.sum_dwords += distinct_words;
    dm->agg.sum_weight += weight;
    dm->agg.sum_trecno += trecno_len;

    *docno = dm->entries++;
    return DOCMAP_OK;
}

static int map_cmp(const void *vone, const void *vtwo) {
    const unsigned long int *one = vone,
                            *two = vtwo;

    if (*one < *two) {
        return -1;
    } else if (*one > *two) {
        return 1;
    } else {
        return 0;
    }
}

/* internal function to reset a cursor to the first entry of a new buffer */
static void reset_cursor(struct docmap *dm, struct docmap_cursor *cur, 
  struct docmap_buffer *buf, unsigned int page, unsigned long int last_docno) {
    int ret;

    assert(page >= buf->page && page < buf->page + buf->buflen);
    cur->pos.pos = buf->buf + dm->pagesize * (page - buf->page);
    cur->pos.end = cur->pos.pos + dm->pagesize;
    cur->page = page;
    cur->buf = buf;
    cur->last_docno = last_docno;
    cur->past = 0;

    /* read header */
    ret = *cur->pos.pos++;
    assert(ret == DATA_BYTE || ret == FINAL_DATA_BYTE);
    assert(vec_len(&cur->pos) > sizeof(cur->entries));
    mem_ntoh(&cur->entries, cur->pos.pos, sizeof(cur->entries));
    cur->pos.pos += sizeof(cur->entries);
    zero_entry(&cur->entry);
    ret = decode(cur);
    assert(ret == DOCMAP_OK);
    cur->first_docno = cur->entry.docno;
    assert(cur->first_docno <= cur->last_docno);
    return;
}

/* internal function to turn a docno into a page number */
static unsigned int find_page(struct docmap *dm, unsigned long int docno) {
    unsigned long int *find;
    unsigned int page;

    assert(docno < dm->entries);

    /* find the correct page in the map */
    find = binsearch(&docno, dm->map, dm->map_len, sizeof(docno), map_cmp);

    if (find < dm->map + dm->map_len && *find == docno) {
        page = find - dm->map;
    } else {
        page = find - dm->map - 1;
    }

    return page;
}

/* arrange for the read buffer to find the page that contains docno */
static enum docmap_ret page_in(struct docmap *dm, unsigned int page) {
    enum docmap_ret dmret;
    int fd;

    /* check that the page we want isn't already in the read buffer */
    if (page >= dm->readbuf.page 
      && page < dm->readbuf.page + dm->readbuf.buflen) {
        return DOCMAP_OK;
    } else if (page >= dm->appendbuf.page 
      && page < dm->appendbuf.page + dm->appendbuf.buflen) {
        /* page we're after is in the append buffer */
        return DOCMAP_OK;
    } else {
        /* we'll have to read the page we're after from disk */

        if (!dm->readbuf.dirty 
          || (dmret = commit(dm, &dm->readbuf)) == DOCMAP_OK) {
            dm->readbuf.dirty = 0;
        } else {
            /* commit failed */
            assert(!CRASH);
            return dmret;
        }

        if (((fd = fdset_pin(dm->fdset, dm->fd_type, page / dm->file_pages, 
            (page % dm->file_pages) * dm->pagesize, SEEK_SET)) >= 0)
          && (dm->readbuf.buflen 
            = read(fd, dm->readbuf.buf, dm->readbuf.bufsize * dm->pagesize)) 
              != -1
          && (fdset_unpin(dm->fdset, dm->fd_type, page / dm->file_pages, fd) 
              == FDSET_OK)
          && (dm->readbuf.buflen > 0)) {
            assert((dm->readbuf.buflen / dm->pagesize) * dm->pagesize 
              == dm->readbuf.buflen);
            dm->readbuf.buflen /= dm->pagesize;
            dm->readbuf.page = page;
            return DOCMAP_OK;
        } else {
            assert(!CRASH);
            invalidate_cursor(&dm->read);
            dm->readbuf.buflen = 0;
            dm->readbuf.page = -1;
            return DOCMAP_IO_ERROR;
        }
    }
}

/* internal function to iterate a cursor to the correct location in the 
 * docmap */
static enum docmap_ret traverse(struct docmap *dm, 
  struct docmap_cursor *cur, unsigned long int docno) {
    unsigned int page;
    enum docmap_ret dmret;

    assert(dm->map[dm->map_len] == ULONG_MAX);
    if (docno < cur->first_docno || docno >= cur->last_docno) {
        /* need to find the correct page */
        page = find_page(dm, docno);
        if ((dmret = page_in(dm, page)) == DOCMAP_OK) {
            assert(page < dm->map_len);
            if (page >= dm->readbuf.page 
              && page < dm->readbuf.page + dm->readbuf.buflen) {
                reset_cursor(dm, cur, &dm->readbuf, page, dm->map[page + 1]);
            } else if (page >= dm->appendbuf.page 
              && page < dm->appendbuf.page + dm->appendbuf.buflen) {
                update_append_entries(dm);
                reset_cursor(dm, cur, &dm->appendbuf, page, dm->map[page + 1]);
            } else {
                assert("can't get here" && 0);
                return DOCMAP_FMT_ERROR;
            }
        } else {
            return dmret;
        }
    } else if (docno < cur->entry.docno) {
        /* need to re-traverse current page */
        reset_cursor(dm, cur, cur->buf, cur->page, cur->last_docno);
    }
    assert(docno >= cur->first_docno && docno < cur->last_docno 
      && docno >= cur->entry.docno);

    /* iterate to the correct position */
    while ((docno > cur->entry.docno) 
      && (dmret = decode(cur)) == DOCMAP_OK) ;

    if (docno == cur->entry.docno) {
        return DOCMAP_OK;
    } else {
        return DOCMAP_ARG_ERROR;
    }
}

enum docmap_ret docmap_get_trecno(struct docmap *dm, unsigned long int
  docno, char *aux_buf, unsigned aux_buf_len, unsigned *aux_len) {
    enum docmap_ret dmret;
    unsigned int len;

    if (docno >= dm->entries) {
        return DOCMAP_ARG_ERROR;
    }

    if (dm->cache.cache & DOCMAP_CACHE_TRECNO) {
        /* retrieve front-coded aux from buffer */
        struct vec v;
        unsigned long int len,
                          prefix = 0;
        unsigned int i,
                     index = docno / TRECNO_FRONT_FREQ,
                     offset = docno % TRECNO_FRONT_FREQ;

        /* read first entry into buffer */
        v.pos = dm->cache.trecno.buf + dm->cache.trecno_off[index];
        v.end = dm->cache.trecno.buf + dm->cache.trecno.len;
        vec_vbyte_read(&v, &len);
        if (len <= aux_buf_len) {
            vec_byte_read(&v, aux_buf, len);
        } else {
            vec_byte_read(&v, aux_buf, aux_buf_len);
        }

        for (i = 0; i < offset; i++) {
            vec_vbyte_read(&v, &prefix);
            vec_vbyte_read(&v, &len);

            if (prefix + len < aux_buf_len) {
                vec_byte_read(&v, aux_buf + prefix, len);
            } else if (prefix < aux_buf_len) {
                char *pos = v.pos + len;
                vec_byte_read(&v, aux_buf + prefix, aux_buf_len - prefix);
                v.pos = pos;  /* skip the remaining entry */
            } else {
                /* just skip this entry, it can't affect the given buffer */
                v.pos += len;
            }
        }

        *aux_len = prefix + len;
        return DOCMAP_OK;
    } else if ((dmret = traverse(dm, &dm->read, docno)) == DOCMAP_OK) {
        len = (aux_buf_len < dm->read.entry.trecno_len) 
          ? aux_buf_len
          : dm->read.entry.trecno_len;
        memcpy(aux_buf, dm->read.entry.trecno, len);
        *aux_len = dm->read.entry.trecno_len;
        return DOCMAP_OK;
    } else {
        return dmret;
    }
}

static int typeex_cmp(const void *vone, const void *vtwo) {
    const struct docmap_type_ex *one = vone,
                                *two = vtwo;

    if (one->docno < two->docno) {
        return -1;
    } else if (one->docno > two->docno) {
        return 1;
    } else {
        return 0;
    }
}

enum docmap_ret docmap_get_location(struct docmap *dm,
  unsigned long int docno, unsigned int *fileno, 
  off_t *disk_offset_ptr, unsigned int *bytes, 
  enum mime_types *mtype, enum docmap_flag *flags) {
    enum docmap_ret dmret;
    uintmax_t disk_offset = 0;
    unsigned long int arr[LOC_REL_FREQ];
    unsigned int index,
                 offset,
                 readbytes;
    struct reposset_record *rec;
    struct vec v;
    struct docmap_type_ex *find,
                          target;

    if (docno >= dm->entries) {
        return DOCMAP_ARG_ERROR;
    }

    if (dm->cache.cache & DOCMAP_CACHE_LOCATION) {
        index = docno / LOC_REL_FREQ;
        offset = docno % LOC_REL_FREQ;

        rec = reposset_record(dm->rset, index * LOC_REL_FREQ);
        assert(rec);

        v.pos = dm->cache.loc.buf + dm->cache.loc_off[index];
        v.end = dm->cache.loc.buf + dm->cache.loc.len;

        if (rec->rectype == REPOSSET_SINGLE_FILE 
          && rec->docno != index * LOC_REL_FREQ) {
            /* need to retrieve offset as well */
            readbytes = vec_maxint_arr_read(&v, &disk_offset, 1, bytes);
            assert(readbytes);
        }

        /* retrieve bytes entries up until entry we're after */
        assert(offset < LOC_REL_FREQ);
        readbytes = vec_vbyte_arr_read(&v, arr, offset + 1, bytes);
        assert(readbytes == offset + 1);

        /* retrieve reposset record for docno we're after */
        rec = reposset_record(dm->rset, docno);
        *fileno = reposset_reposno_rec(rec, docno);
        *bytes = arr[offset];

        if (rec->rectype != REPOSSET_SINGLE_FILE
          || rec->docno > index * LOC_REL_FREQ) {
            /* this record doesn't extend back to the encoded offset */
            disk_offset = 0;
        }

        /* calculate offset by summing the bytes entries for relevant previous
         * entries */
        while (rec->rectype == REPOSSET_SINGLE_FILE
          && offset
          && rec->docno < index * LOC_REL_FREQ + offset) {
            disk_offset += arr[--offset];
        }
        *disk_offset_ptr = (off_t) disk_offset;

        /* read checkpoints to figure out whether that repository is 
         * compressed */
        assert(!reposset_check(dm->rset, *fileno)
          || reposset_check(dm->rset, *fileno)->reposno == *fileno);
        if (reposset_check(dm->rset, *fileno)) {
            *flags = DOCMAP_COMPRESSED;
        } else {
            *flags = 0;
        }

        /* figure out what the mtype is */
        target.docno = docno;

        /* find the correct page in the map */
        find = binsearch(&docno, dm->cache.typeex, dm->cache.typeex_len, 
          sizeof(*dm->cache.typeex), typeex_cmp);

        if (find < dm->cache.typeex + dm->cache.typeex_len 
          && find->docno == docno) {
            *mtype = find->mtype;
        } else {
            *mtype = MIME_TYPE_APPLICATION_X_TREC;
        }

        return DOCMAP_OK;
    } else if ((dmret = traverse(dm, &dm->read, docno)) == DOCMAP_OK) {
        *fileno = dm->read.entry.fileno;
        *disk_offset_ptr = dm->read.entry.offset;
        *flags = dm->read.entry.flags;
        *bytes = dm->read.entry.bytes;
        *mtype = dm->read.entry.mtype;
        return DOCMAP_OK;
    } else {
        return dmret;
    }
}

unsigned int docmap_get_bytes_cached(struct docmap *dm,
  unsigned int docno) {
    struct vec v;
    unsigned int index = docno / LOC_REL_FREQ,
                 offset = docno % LOC_REL_FREQ;
    struct reposset_record *rec;
    uintmax_t disk_offset = 0;
    unsigned long int arr[LOC_REL_FREQ];
    unsigned int bytes,
                 readbytes;

    assert(dm->cache.cache & DOCMAP_CACHE_LOCATION);
    assert(docno < dm->entries);

    rec = reposset_record(dm->rset, index * LOC_REL_FREQ);
    assert(rec);

    v.pos = dm->cache.loc.buf + dm->cache.loc_off[index];
    v.end = dm->cache.loc.buf + dm->cache.loc.len;

    if (rec->rectype == REPOSSET_SINGLE_FILE 
      && rec->docno != index * LOC_REL_FREQ) {
        /* need to retrieve offset as well */
        readbytes = vec_maxint_arr_read(&v, &disk_offset, 1, &bytes);
        assert(readbytes);
    }

    /* retrieve bytes entries up until entry we're after */
    assert(offset < LOC_REL_FREQ);
    readbytes = vec_vbyte_arr_read(&v, arr, offset + 1, &bytes);
    assert(readbytes == offset + 1);
    return arr[offset];
}

enum docmap_ret docmap_get_bytes(struct docmap *dm,
  unsigned long int docno, unsigned int *bytes) {
    enum docmap_ret dmret;

    if (docno < dm->entries && dm->cache.cache & DOCMAP_CACHE_LOCATION) {
        *bytes = docmap_get_bytes_cached(dm, docno);
        return DOCMAP_OK;
    } else if ((dmret = traverse(dm, &dm->read, docno)) == DOCMAP_OK) {
        *bytes = dm->read.entry.bytes;
        return DOCMAP_OK;
    } else {
        return dmret;
    }
}

enum docmap_ret docmap_get_words(struct docmap *dm,
  unsigned long int docno, unsigned int *words) {
    enum docmap_ret dmret;

    if (docno < dm->entries && dm->cache.cache & DOCMAP_CACHE_WORDS) {
        *words = dm->cache.words[docno];
        return DOCMAP_OK;
    } else if ((dmret = traverse(dm, &dm->read, docno)) == DOCMAP_OK) {
        *words = dm->read.entry.words;
        return DOCMAP_OK;
    } else {
        return dmret;
    }
}

enum docmap_ret docmap_get_distinct_words(struct docmap *dm,
  unsigned long int docno, unsigned int *distinct_words) {
    enum docmap_ret dmret;

    if (docno < dm->entries && dm->cache.cache & DOCMAP_CACHE_DISTINCT_WORDS) {
        *distinct_words = dm->cache.dwords[docno];
        return DOCMAP_OK;
    } else if ((dmret = traverse(dm, &dm->read, docno)) == DOCMAP_OK) {
        *distinct_words = dm->read.entry.dwords;
        return DOCMAP_OK;
    } else {
        return dmret;
    }
}

enum docmap_ret docmap_get_weight(struct docmap *dm,
  unsigned long int docno, double *weight) {
    enum docmap_ret dmret;

    if (docno < dm->entries && dm->cache.cache & DOCMAP_CACHE_WEIGHT) {
        *weight = dm->cache.weight[docno];
        return DOCMAP_OK;
    }
    if ((dmret = traverse(dm, &dm->read, docno)) == DOCMAP_OK) {
        *weight = dm->read.entry.weight;
        return DOCMAP_OK;
    } else {
        return dmret;
    }
}

enum docmap_cache docmap_get_cache(struct docmap *dm) {
    return dm->cache.cache;
}

static void cache_cleanup(struct docmap *dm) {
    if (!(dm->cache.cache & DOCMAP_CACHE_WORDS)) {
        if (dm->cache.words) {
            free(dm->cache.words);
            dm->cache.words = NULL;
        }
    } 
    if (!(dm->cache.cache & DOCMAP_CACHE_DISTINCT_WORDS)) {
        if (dm->cache.dwords) {
            free(dm->cache.dwords);
            dm->cache.dwords = NULL;
        }
    } 
    if (!(dm->cache.cache & DOCMAP_CACHE_WEIGHT)) {
        if (dm->cache.weight) {
            free(dm->cache.weight);
            dm->cache.weight = NULL;
        }
    }
    if (!(dm->cache.cache & DOCMAP_CACHE_TRECNO)) {
        if (dm->cache.trecno.buf) {
            free(dm->cache.trecno.buf);
            dm->cache.trecno.buf = NULL;
        }
        if (dm->cache.trecno_off) {
            free(dm->cache.trecno_off);
            dm->cache.trecno_off = NULL;
        }
        dm->cache.trecno.size = dm->cache.trecno.len = 0;
    }
    if (!(dm->cache.cache & DOCMAP_CACHE_LOCATION)) {
        if (dm->cache.loc.buf) {
            free(dm->cache.loc.buf);
            dm->cache.loc.buf = NULL;
            dm->cache.loc.len = dm->cache.loc.size = 0;
        }
        if (dm->cache.loc_off) {
            free(dm->cache.loc_off);
            dm->cache.loc_off = NULL;
        }
        if (dm->cache.typeex) {
            free(dm->cache.typeex);
            dm->cache.typeex = NULL;
            dm->cache.typeex_len = dm->cache.typeex_size = 0;
        }
    }
}

static enum docmap_ret map_realloc(struct docmap *dm) {
    void *ptr = realloc(dm->map, sizeof(*dm->map) * dm->map_size * 2);

    if (ptr) {
        dm->map = ptr;
        dm->map_size *= 2;
        return DOCMAP_OK;
    } else {
        assert(!CRASH);
        return DOCMAP_MEM_ERROR;
    }
}

static enum docmap_ret docmap_cache_int(struct docmap *dm, 
  enum docmap_cache tocache, int reread) {
    enum docmap_cache prev = dm->cache.cache;
    enum docmap_ret dmret;
    unsigned int page,
                 pages = dm->map_len,
                 prev_entries = dm->entries,
                 prev_size = dm->cache.size,
                 prev_fileno = -1;

    assert(pages);

    if (!reread && prev == tocache) {
        /* nothing to do */
        return DOCMAP_OK;

    } else if (!reread && (tocache - (dm->cache.cache & tocache)) == 0) {
        /* all required entries are cached, just turn some off */
        dm->cache.cache = tocache;

        /* now use tocache to figure out what to remove */
        tocache = (dm->cache.cache & tocache) - tocache;

        if (tocache & DOCMAP_CACHE_WORDS) {
            assert(dm->cache.words);
            free(dm->cache.words);
            dm->cache.words = NULL;
        }
        if (tocache & DOCMAP_CACHE_DISTINCT_WORDS) {
            assert(dm->cache.dwords);
            free(dm->cache.dwords);
            dm->cache.dwords = NULL;
        }
        if (tocache & DOCMAP_CACHE_WEIGHT) {
            assert(dm->cache.weight);
            free(dm->cache.weight);
            dm->cache.weight = NULL;
        }
        if (tocache & DOCMAP_CACHE_TRECNO) {
            assert(dm->cache.trecno.buf && dm->cache.trecno_off);
            free(dm->cache.trecno.buf);
            free(dm->cache.trecno_off);
            dm->cache.trecno.buf = NULL;
            dm->cache.trecno_off = NULL;
            dm->cache.trecno.len = dm->cache.trecno.size = 0;
        }
        if (tocache & DOCMAP_CACHE_LOCATION) {
            assert(dm->cache.loc.buf && dm->cache.loc_off);
            free(dm->cache.loc.buf);
            free(dm->cache.loc_off);
            dm->cache.loc.buf = NULL;
            dm->cache.loc_off = NULL;
            dm->cache.loc.len = dm->cache.loc.size = 0;
            if (dm->cache.typeex) {
                free(dm->cache.typeex);
                dm->cache.typeex = NULL;
            }
        }
        return DOCMAP_OK;
    }

    dm->dirty = 1;
    dm->agg.sum_weight = dm->agg.sum_bytes 
      = dm->agg.sum_words = dm->agg.sum_dwords = 0;

    /* ensure that we've got initial memory for cached items */
    dm->cache.cache = tocache;
    if (!(dmret = cache_realloc(dm)) == DOCMAP_OK) {
        assert(!CRASH);
        dm->cache.cache = prev;
        cache_cleanup(dm);
        return DOCMAP_MEM_ERROR;
    }

    /* make sure we've got enough room on the map */
    while (dm->map_len + 1 >= dm->map_size) {
        dm->map_size = dm->map_len + 1;
        if ((dmret = map_realloc(dm)) != DOCMAP_OK) {
            dm->cache.cache = prev;
            cache_cleanup(dm);
            return dmret;
        }
    }

    /* need to read all entries in the docmap (rebuilding map and reposset 
     * and entries along the way) */
    prev_entries = dm->entries;
    dm->entries = 0;
    dm->cache.len = 0;
    dm->cache.typeex_len = 0;
    dm->cache.trecno.len = 0;
    dm->cache.loc.len = 0;
    reposset_clear(dm->rset);
    for (page = 0; page < pages; page++) {
        if ((dmret = page_in(dm, page)) == DOCMAP_OK) {
            unsigned int entry = 0;

            if (page >= dm->readbuf.page 
              && page < dm->readbuf.page + dm->readbuf.buflen) {
                reset_cursor(dm, &dm->read, &dm->readbuf, page, ULONG_MAX);
            } else if (page >= dm->appendbuf.page 
              && page < dm->appendbuf.page + dm->appendbuf.buflen) {
                reset_cursor(dm, &dm->read, &dm->appendbuf, page, ULONG_MAX);
            } else {
                assert("can't get here" && 0);
                return DOCMAP_FMT_ERROR;
            }

            /* update map entry for this page */
            dm->map[page] = dm->read.entry.docno;

            /* iterate through page entries */
            do {
                /* recreate reposset */
                while (prev_fileno != dm->read.entry.fileno) {
                    unsigned int reposno;
                    enum reposset_ret rret;
                    assert(prev_fileno == -1 
                      || prev_fileno < dm->read.entry.fileno);
                    rret = reposset_append(dm->rset, 
                        dm->read.entry.docno, &reposno);
                    assert(rret == REPOSSET_OK 
                      && reposno == dm->read.entry.fileno);

                    prev_fileno++;
                }
                reposset_append_docno(dm->rset, dm->read.entry.docno, 1);
                if (dm->read.entry.flags == DOCMAP_COMPRESSED 
                  && dm->read.entry.offset == 0) {
                    enum reposset_ret rret;
                    /* XXX: add in a checkpoint at the start of each compressed
                     * file, because that's what the indexing code does */
                    rret = reposset_add_checkpoint(dm->rset, 
                      dm->read.entry.fileno, MIME_TYPE_APPLICATION_X_GZIP, 0);
                    assert(rret == REPOSSET_OK);
                }

                /* ensure we have space for cache stuff */
                if (dm->cache.len >= dm->cache.size) {
                    dm->cache.size = dm->cache.size * 2 + 1;
                    if ((dmret = cache_realloc(dm)) != DOCMAP_OK) {
                        dm->cache.size = prev_size;
                        dm->entries = prev_entries;
                        dm->cache.cache = prev;
                        cache_cleanup(dm);
                        return dmret;
                    }
                }

                /* update aggregates */
                entry++;
                dm->agg.sum_words += dm->read.entry.words;
                dm->agg.sum_dwords += dm->read.entry.dwords;
                dm->agg.sum_bytes += dm->read.entry.bytes;
                dm->agg.sum_weight += dm->read.entry.weight;
                dm->agg.sum_trecno += dm->read.entry.trecno_len;

                /* update cache */
                if ((dmret = cache(dm, &dm->read.entry)) != DOCMAP_OK) {
                    dm->entries = prev_entries;
                    dm->cache.cache = prev;
                    cache_cleanup(dm);
                    return dmret;
                }
            } while ((dmret = decode(&dm->read)) 
              == DOCMAP_OK);

            assert(dmret == DOCMAP_BUFSIZE_ERROR 
              && entry == dm->read.entries);
            dm->entries += entry;
        } else {
            assert(!CRASH);
            dm->cache.cache = prev;
            cache_cleanup(dm);
            return DOCMAP_IO_ERROR;
        }
    }

    /* update last map entry */
    dm->map[dm->map_len] = ULONG_MAX;

    aggregate(dm);   /* recalculate aggregates */

    /* clean up after all non-required entries */
    cache_cleanup(dm);
    return DOCMAP_OK;
}

enum docmap_ret docmap_cache(struct docmap *dm, enum docmap_cache tocache) {
    /* need to have a separate function so that we can force reread of the
     * docmap when necessary */
    return docmap_cache_int(dm, tocache, 0);
}

enum docmap_ret docmap_avg_bytes(struct docmap *dm, double *avg_bytes) {
    *avg_bytes = dm->agg.avg_bytes;
    return DOCMAP_OK;
}

enum docmap_ret docmap_total_bytes(struct docmap *dm, double *total_bytes) {
    *total_bytes = dm->agg.sum_bytes;
    return DOCMAP_OK;
}

enum docmap_ret docmap_avg_words(struct docmap *dm, double *avg_words) {
    *avg_words = dm->agg.avg_words;
    return DOCMAP_OK;
}

enum docmap_ret docmap_avg_distinct_words(struct docmap *dm,
  double *avg_dwords) {
    *avg_dwords = dm->agg.avg_dwords;
    return DOCMAP_OK;
}

enum docmap_ret docmap_avg_weight(struct docmap *dm, double *avg_weight) {
    *avg_weight = dm->agg.avg_weight;
    return DOCMAP_OK;
}

unsigned long int docmap_entries(struct docmap *dm) {
    return dm->entries;
}

const char *docmap_strerror(enum docmap_ret dmret) {
    switch (dmret) {
    case DOCMAP_OK:
        return "success";
    case DOCMAP_MEM_ERROR:
        return "memory error";
    case DOCMAP_IO_ERROR:
        return "I/O error";
    case DOCMAP_BUFSIZE_ERROR:
        return "buffer too small";
    case DOCMAP_FMT_ERROR:
        return "format error";
    case DOCMAP_ARG_ERROR:
        return "argument (programmer) error";
    default:
        return "unknown error";
    }
}

enum docmap_ret docmap_save(struct docmap *dm) {
    enum docmap_ret dmret;
    unsigned int fileno,
                 docno;
    unsigned long int offset;
    int fd;
    TIMINGS_DECL();
    TIMINGS_START();

    if ((!dm->readbuf.dirty 
        || (dmret = commit(dm, &dm->readbuf)) == DOCMAP_OK)
      && (!dm->appendbuf.dirty
        /* mark current last page in append buffer as final */
        || ((dm->appendbuf.buf[(dm->appendbuf.buflen - 1) * dm->pagesize] 
            = FINAL_DATA_BYTE) 
          /* update the entries record in the append page */
          && (update_append_entries(dm), 1)
          /* write it to disk */
          && ((dmret = commit(dm, &dm->appendbuf)) == DOCMAP_OK)
          /* unmark current last page in append buffer as final */
          && (dm->appendbuf.buf[(dm->appendbuf.buflen - 1) * dm->pagesize] 
            = DATA_BYTE)))) {

        fileno = dm->map_len / dm->file_pages;
        offset = dm->map_len % dm->file_pages;
        offset *= dm->pagesize;

/* macro to prepare a new page for writing */
#define NEW_PAGE()                                                            \
        if (1) {                                                              \
            if (page >= dm->readbuf.bufsize) {                                \
                /* need to clear buffer */                                    \
                unsigned int target = dm->readbuf.bufsize * dm->pagesize;     \
                unsigned int bytes = write(fd, dm->readbuf.buf, target);      \
                                                                              \
                if (bytes == target) {                                        \
                    page = 0;                                                 \
                } else {                                                      \
                    /* writing error */                                       \
                    fdset_unpin(dm->fdset, dm->fd_type, fileno, fd);          \
                    return DOCMAP_IO_ERROR;                                   \
                }                                                             \
            }                                                                 \
            v.pos = dm->readbuf.buf + dm->pagesize * page;                    \
            v.end = v.pos + dm->pagesize;                                     \
            *v.pos++ = CACHE_BYTE;                                            \
            vec_vbyte_write(&v, dm->entries);                                 \
            vec_vbyte_write(&v, dm->map_len);                                 \
            page++;                                                           \
        } else

        /* now write out cache pages */
        if (dm->dirty 
          && (fd = fdset_pin(dm->fdset, dm->fd_type, fileno, offset, SEEK_SET)) 
            >= 0) {
            struct vec v;
            unsigned int i,
                         page = 0,
                         bytes,
                         written;
            char *entry_pos,
                 *end_pos,
                 byte;
            struct reposset_check* check;

            /* dump out the contents of the read buffer to give us 
             * working space */
            invalidate_buffer(dm, &dm->readbuf);

            NEW_PAGE();

            /* write aggregate quantities */
            *v.pos++ = CACHE_ID_AGG;
            vec_flt_write(&v, (float) dm->agg.sum_bytes, 
              VEC_FLT_FULL_PRECISION);
            vec_flt_write(&v, (float) dm->agg.sum_words, 
              VEC_FLT_FULL_PRECISION);
            vec_flt_write(&v, (float) dm->agg.sum_dwords, 
              VEC_FLT_FULL_PRECISION);
            vec_flt_write(&v, (float) dm->agg.sum_weight, 
              VEC_FLT_FULL_PRECISION);
            vec_flt_write(&v, (float) dm->agg.sum_trecno, 
              VEC_FLT_FULL_PRECISION);

/* macro to perform repetitive paging out of arrays */
#define PAGE_OUT_INT 1
#define PAGE_OUT_FLT 2
#define PAGE_OUT_CHR 3
#define PAGE_OUT_LONG 4
#define PAGE_OUT_CACHE(id, elem, num, masked, mask, type)                     \
            for (written = 0; (!masked || dm->cache.cache & mask)             \
              && written < num; ) {                                           \
                uint32_t entries = 0;                                         \
                                                                              \
                end_pos = v.pos;                                              \
                byte = id; vec_byte_write(&v, (void *) &byte, 1);             \
                vec_vbyte_write(&v, num);                                     \
                vec_vbyte_write(&v, written);                                 \
                entry_pos = v.pos;                                            \
                vec_byte_write(&v, (void *) &entries, sizeof(entries));       \
                if ((type == PAGE_OUT_LONG                                    \
                  && (entries = vec_vbyte_arr_write(&v,                       \
                      (void *) ((elem) + written), num - written, &bytes)))   \
                  || (type == PAGE_OUT_INT                                    \
                    && (entries = vec_int_arr_write(&v,                       \
                      (void *) ((elem) + written), num - written, &bytes)))   \
                  || (type == PAGE_OUT_FLT                                    \
                    && (entries = vec_flt_arr_write(&v,                       \
                      (void *) ((elem) + written), num - written,             \
                      DOCMAP_WEIGHT_PRECISION, &bytes)))                      \
                  || (type == PAGE_OUT_CHR                                    \
                    && (entries = vec_byte_write(&v,                          \
                      (void *) ((elem) + written), num - written)))) {        \
                                                                              \
                    mem_hton(entry_pos, &entries, sizeof(entries));           \
                    written += entries;                                       \
                } else {                                                      \
                    /* ran out of space, terminate this page and start anew */\
                    v.pos = end_pos;                                          \
                    vec_byte_write(&v, "", 1);                                \
                    if (DEAR_DEBUG) {                                         \
                        memset(v.pos, 0, vec_len(&v));                        \
                    }                                                         \
                    NEW_PAGE();                                               \
                }                                                             \
            }

            PAGE_OUT_CACHE(CACHE_ID_MAP, dm->map, dm->map_len, 0, 0, 
              PAGE_OUT_LONG);
            PAGE_OUT_CACHE(CACHE_ID_WORDS, dm->cache.words, dm->cache.len, 
              1, DOCMAP_CACHE_WORDS, PAGE_OUT_INT);
            PAGE_OUT_CACHE(CACHE_ID_DWORDS, dm->cache.dwords, dm->cache.len, 
              1, DOCMAP_CACHE_DISTINCT_WORDS, PAGE_OUT_INT);
            PAGE_OUT_CACHE(CACHE_ID_WEIGHT, dm->cache.weight, dm->cache.len, 
              1, DOCMAP_CACHE_WEIGHT, PAGE_OUT_FLT);
            PAGE_OUT_CACHE(CACHE_ID_TRECNO, dm->cache.trecno_off, 
              (dm->cache.len + TRECNO_FRONT_FREQ - 1) / TRECNO_FRONT_FREQ, 1, 
              DOCMAP_CACHE_TRECNO, PAGE_OUT_INT);
            PAGE_OUT_CACHE(CACHE_ID_TRECNO_CODE, dm->cache.trecno.buf, 
              dm->cache.trecno.len, 1, DOCMAP_CACHE_TRECNO, PAGE_OUT_CHR);
            PAGE_OUT_CACHE(CACHE_ID_LOC, dm->cache.loc_off, 
              (dm->cache.len + LOC_REL_FREQ - 1) / LOC_REL_FREQ, 1, 
              DOCMAP_CACHE_LOCATION, PAGE_OUT_INT);
            PAGE_OUT_CACHE(CACHE_ID_LOC_CODE, dm->cache.loc.buf, 
              dm->cache.loc.len, 1, DOCMAP_CACHE_LOCATION, PAGE_OUT_CHR);

            /* note that we're being dodgy and just processing the type 
             * exception array as an array of unsigned ints */
            PAGE_OUT_CACHE(CACHE_ID_TYPEEX, (unsigned int *) dm->cache.typeex,
              dm->cache.typeex_len * 2, 1, DOCMAP_CACHE_LOCATION, 
              PAGE_OUT_INT);

            /* write out reposset details */
            docno = 0;
            while (docno < dm->entries) {
                struct reposset_record *rec;
                char idc = CACHE_ID_REPOS_REC;

                /* retrieve reposset record dealing with this entry */
                if ((rec = reposset_record(dm->rset, docno))) {
                    char *pos = v.pos;

                    /* try to write record to output */
                    if (vec_byte_write(&v, &idc, 1)
                      && vec_vbyte_write(&v, (rec->reposno << 1) | rec->rectype)
                      && vec_vbyte_write(&v, rec->docno)
                      && vec_vbyte_write(&v, rec->quantity)) {
                        /* succeeded in writing out this record */
                        docno += rec->quantity;
                    } else {
                        v.pos = pos;
                        vec_byte_write(&v, "", 1);
                        if (DEAR_DEBUG) {
                            memset(v.pos, 0, vec_len(&v));
                        }
                        NEW_PAGE();
                    }
                } else {
                    assert("can't get here" && 0);
                    fdset_unpin(dm->fdset, dm->fd_type, fileno, fd);
                    return DOCMAP_ARG_ERROR;
                }
            }

            /* write out checkpoint details */
            check = reposset_check_first(dm->rset);
            for (i = 0; i < reposset_checks(dm->rset); i++) {
                char idc = CACHE_ID_REPOS_CHECK;
                char *pos = v.pos;

                /* try to write record to output */
                if (vec_byte_write(&v, &idc, 1)
                  && vec_vbyte_write(&v, check[i].reposno)
                  && vec_vbyte_write(&v, check[i].offset)
                  && vec_vbyte_write(&v, check[i].comp)) {
                    /* succeeded in writing out this record */
                } else {
                    v.pos = pos;
                    vec_byte_write(&v, "", 1);
                    if (DEAR_DEBUG) {
                        memset(v.pos, 0, vec_len(&v));
                    }
                    NEW_PAGE();
                }
            }

            /* terminate final page, and mark it as final */
            vec_byte_write(&v, "", 1);
            if (DEAR_DEBUG) {
                memset(v.pos, 0, vec_len(&v));
            }
            v.pos = dm->readbuf.buf + (page - 1) * dm->pagesize;
            *v.pos++ = FINAL_CACHE_BYTE;

            /* write it out */
            bytes = page * dm->pagesize;
            written = write(fd, dm->readbuf.buf, bytes);
            dmret = fdset_unpin(dm->fdset, dm->fd_type, fileno, fd);
            assert(dmret == FDSET_OK);

            if (written != bytes) {
                return DOCMAP_IO_ERROR;
            }
        }

        dm->dirty = 0;

        TIMINGS_END("docmap save");
        return DOCMAP_OK;
    } else {
        return dmret;
    }
}

struct docmap *docmap_new(struct fdset *fdset, 
  int fd_type, unsigned int pagesize, unsigned int pages, 
  unsigned long int max_filesize, enum docmap_cache cache, 
  enum docmap_ret *ret) {
    struct docmap *dm 
      = docmap_init(fdset, fd_type, pagesize, pages, max_filesize, cache);
    dm->dirty = 1;

    if (dm) {
        /* create a new file for the docmap */
        int fd = fdset_create(dm->fdset, dm->fd_type, 0);
        if (fd >= 0) {
            fdset_unpin(dm->fdset, dm->fd_type, 0, fd);
            *ret = DOCMAP_OK;
        } else {
            *ret = DOCMAP_IO_ERROR;
            docmap_delete(dm);
            return NULL;
        }
    } else {
        assert(!CRASH);
        *ret = DOCMAP_MEM_ERROR;
    }

    return dm;
}

/* internal function to check that the cache contains the same as the on-disk
 * docmap */
static enum docmap_ret docmap_cache_check(struct docmap *dm);

struct docmap *docmap_load(struct fdset *fdset, 
  int fd_type, unsigned int pagesize, unsigned int bufpages, 
  unsigned long int max_filesize, enum docmap_cache cache, 
  enum docmap_ret *ret) {
    struct docmap *dm = docmap_init(fdset, fd_type, pagesize, bufpages, 
        max_filesize, 0);
    int fd = -1,
        prev_fd,
        corrupt,
        map;
    unsigned long int offset,
                      tmpl;
    unsigned int fileno,
                 page,
                 pages;
    struct vec v;
    enum docmap_ret dmret;
    TIMINGS_DECL();
    TIMINGS_START();

    if (!dm) {
        assert(!CRASH);
        *ret = DOCMAP_MEM_ERROR;
        return NULL;
    }
    max_filesize = dm->file_pages * dm->pagesize;

#define FAIL(retval)                                                          \
    if (fd >= 0) {                                                            \
        fdset_unpin(fdset, fd_type, fileno, fd);                              \
    }                                                                         \
    *ret = retval; docmap_delete(dm); assert(!CRASH); return NULL

    /* find out number of pages */
    for (fileno = 0, pages = 0; (prev_fd = fd), 
      (fd = fdset_pin(fdset, fd_type, fileno, 0, SEEK_END)) 
        >= 0;
      fileno++) {
        if (prev_fd >= 0) {
            fdset_unpin(fdset, fd_type, fileno - 1, prev_fd);
        }
        if (fileno && offset != max_filesize) {
            /* corrupt */
            FAIL(DOCMAP_FMT_ERROR);
        }
        offset = lseek(fd, 0, SEEK_CUR);
        if (offset != -1) {
            if (((offset / dm->pagesize) * dm->pagesize) != offset) {
                FAIL(DOCMAP_FMT_ERROR);
            }
            pages += offset / dm->pagesize;
        } else {
            FAIL(DOCMAP_IO_ERROR);
        }
    }
    fd = prev_fd;
    assert(fd >= 0);
    fileno--;

    /* read last page */
    if (lseek(fd, - (int) dm->pagesize, SEEK_END) != -1 
      && read(fd, dm->readbuf.buf, dm->pagesize) 
        == (ssize_t) dm->pagesize) {

        /* determine location of last data page from last page */
        v.pos = dm->readbuf.buf;
        v.end = v.pos + dm->pagesize;
        if (*v.pos++ != FINAL_CACHE_BYTE) {
            FAIL(DOCMAP_FMT_ERROR);
        }
        vec_vbyte_read(&v, &tmpl);
        dm->entries = dm->cache.size = dm->cache.len = tmpl;
        vec_vbyte_read(&v, &tmpl);
        assert(tmpl);
        dm->map_len = tmpl;
        dm->map_size = dm->map_len + 1;

        if (!(dm->map = realloc(dm->map, sizeof(*dm->map) * dm->map_size))) {
            assert(!CRASH);
            FAIL(DOCMAP_MEM_ERROR);
        }
    } else {
        FAIL(DOCMAP_IO_ERROR);
    }

    /* pin correct fd, and seek to the correct location */
    page = dm->map_len - 1; /* address of last data page */
    offset = (page % dm->file_pages) * dm->pagesize;
    if (page / dm->file_pages != fileno) {
        fdset_unpin(fdset, fd_type, fileno, fd);
        fileno = page / dm->file_pages;
        if ((fd = fdset_pin(fdset, fd_type, fileno, offset, SEEK_SET)) < 0) {
            FAIL(DOCMAP_IO_ERROR);
        }
    /* seek fd to correct location */
    } else if (lseek(fd, offset, SEEK_SET) == -1) {
        FAIL(DOCMAP_IO_ERROR);
    } 

    /* page last data page into append buffer, read until the end */
    if ((dmret = take_read_buffer(dm)) == DOCMAP_OK 
      && (dm->appendbuf.buflen = read(fd, dm->appendbuf.buf, dm->pagesize)) 
        == dm->pagesize) {

        dm->appendbuf.buflen /= dm->pagesize;
        dm->appendbuf.page = page;
        assert(dm->appendbuf.buflen == 1 && dm->appendbuf.bufsize == 1);
        if (*dm->appendbuf.buf != FINAL_DATA_BYTE) {
            FAIL(DOCMAP_FMT_ERROR);
        }
        reset_cursor(dm, &dm->write, &dm->appendbuf, page, ULONG_MAX);

        /* read until the end of the entries */
        while (dm->write.past < dm->write.entries) {
            if ((dmret = decode(&dm->write)) != DOCMAP_OK) {
                FAIL(DOCMAP_FMT_ERROR);
            }
        }
    } else {
        FAIL(DOCMAP_IO_ERROR);
    }
    fdset_unpin(fdset, fd_type, fileno, fd);
    fd = -1;

    /* read cache pages */
    corrupt = 0;
    map = 0;
    for (page = dm->map_len; !corrupt && page < pages; page++) {
        if ((dmret = page_in(dm, page)) == DOCMAP_OK) {
            assert(page >= dm->readbuf.page && page 
              && page < dm->readbuf.page + dm->readbuf.buflen);
            v.pos = dm->readbuf.buf + dm->pagesize * (page - dm->readbuf.page);
            v.end = v.pos + dm->pagesize;

            if ((((page + 1 < pages) && (*v.pos == CACHE_BYTE))
              || ((page + 1 == pages) && *v.pos == FINAL_CACHE_BYTE))) {
                v.pos++;
            } else {
                corrupt = 1;
            }

            vec_vbyte_read(&v, &tmpl);
            vec_vbyte_read(&v, &offset);
            if (!corrupt && (tmpl == dm->entries) && (offset == dm->map_len)) {
                int finished = 0,
                    type = 0;
                uint32_t entries,
                         tmptries;
                unsigned long int **larrptr = NULL;
                float **farrptr = NULL;
                char **carrptr = NULL;
                unsigned int target = 0,
                             bytes,
                             **arrptr = NULL;
                float tmpf;
                struct reposset_record tmprec;
                struct reposset_check tmpcheck;

                while (!corrupt && !finished && vec_len(&v)) {
                    switch (*v.pos++) {
                    case CACHE_ID_END: 
                        finished = 1; 
                        break;

                    case CACHE_ID_AGG:
                        vec_flt_read(&v, &tmpf, VEC_FLT_FULL_PRECISION);
                        dm->agg.sum_bytes = tmpf;
                        vec_flt_read(&v, &tmpf, VEC_FLT_FULL_PRECISION);
                        dm->agg.sum_words = tmpf;
                        vec_flt_read(&v, &tmpf, VEC_FLT_FULL_PRECISION);
                        dm->agg.sum_dwords = tmpf;
                        vec_flt_read(&v, &tmpf, VEC_FLT_FULL_PRECISION);
                        dm->agg.sum_weight = tmpf;
                        if (!vec_len(&v) 
                          || !vec_flt_read(&v, &tmpf, VEC_FLT_FULL_PRECISION)) {
                            corrupt = 1;
                        }
                        dm->agg.sum_trecno = tmpf;
                        break;

                    case CACHE_ID_REPOS_REC:
                        vec_vbyte_read(&v, &tmpl);
                        tmprec.reposno = tmpl >> 1;
                        tmprec.rectype = tmpl & 1;
                        vec_vbyte_read(&v, &tmpl);
                        tmprec.docno = tmpl;
                        if (vec_vbyte_read(&v, &tmpl)) {
                            tmprec.quantity = tmpl;

                            if (reposset_set_record(dm->rset, &tmprec) 
                              != REPOSSET_OK) {
                                assert(!CRASH);
                                assert(!CRASH);
                                FAIL(DOCMAP_MEM_ERROR);
                            }
                        } else {
                            assert(!CRASH);
                            FAIL(DOCMAP_FMT_ERROR);
                        }
                        break;

                    case CACHE_ID_REPOS_CHECK:
                        vec_vbyte_read(&v, &tmpl);
                        tmpcheck.reposno = tmpl;
                        vec_vbyte_read(&v, &tmpcheck.offset);
                        if (vec_vbyte_read(&v, &tmpl) 
                          && reposset_add_checkpoint(dm->rset, tmpcheck.reposno,
                              tmpl, tmpcheck.offset) == REPOSSET_OK) {

                            assert(tmpl == MIME_TYPE_APPLICATION_X_GZIP);
                            tmpcheck.comp = tmpl;
                        } else {
                            assert(!CRASH);
                            FAIL(DOCMAP_FMT_ERROR);
                        }
                        break;

                    case CACHE_ID_MAP:
                    case CACHE_ID_WEIGHT:
                    case CACHE_ID_TRECNO:
                    case CACHE_ID_TRECNO_CODE:
                    case CACHE_ID_DWORDS:
                    case CACHE_ID_WORDS:
                    case CACHE_ID_LOC:
                    case CACHE_ID_LOC_CODE:
                    case CACHE_ID_TYPEEX:
                        v.pos--;
                        bytes = *v.pos++;
                        vec_vbyte_read(&v, &tmpl);
                        vec_vbyte_read(&v, &offset);
                        vec_byte_read(&v, (void *) &tmptries, sizeof(tmptries));
                        mem_ntoh(&entries, &tmptries, sizeof(entries));
                        switch (bytes) {
                        case CACHE_ID_TRECNO_CODE:
                            dm->cache.cache |= DOCMAP_CACHE_TRECNO;
                            type = PAGE_OUT_CHR;
                            target = tmpl;
                            carrptr = &dm->cache.trecno.buf;
                            dm->cache.trecno.len = dm->cache.trecno.size = tmpl;
                            break;
                        case CACHE_ID_LOC_CODE:
                            dm->cache.cache |= DOCMAP_CACHE_LOCATION;
                            type = PAGE_OUT_CHR;
                            target = tmpl;
                            carrptr = &dm->cache.loc.buf;
                            dm->cache.loc.len = dm->cache.loc.size = tmpl;
                            break;
                        case CACHE_ID_MAP:
                            map = 1;
                            type = PAGE_OUT_LONG;
                            target = dm->entries;
                            larrptr = &dm->map;
                            break;
                        case CACHE_ID_WEIGHT:
                            dm->cache.cache |= DOCMAP_CACHE_WEIGHT;
                            type = PAGE_OUT_FLT;
                            target = dm->entries;
                            farrptr = &dm->cache.weight;
                            break;
                        case CACHE_ID_TRECNO:
                            dm->cache.cache |= DOCMAP_CACHE_TRECNO;
                            type = PAGE_OUT_INT;
                            target = (dm->entries + TRECNO_FRONT_FREQ - 1) 
                              / TRECNO_FRONT_FREQ;
                            arrptr = &dm->cache.trecno_off;
                            break;
                        case CACHE_ID_LOC:
                            dm->cache.cache |= DOCMAP_CACHE_LOCATION;
                            type = PAGE_OUT_INT;
                            target = (dm->entries + LOC_REL_FREQ - 1) 
                              / LOC_REL_FREQ;
                            arrptr = &dm->cache.loc_off;
                            break;
                        case CACHE_ID_DWORDS: 
                            dm->cache.cache |= DOCMAP_CACHE_DISTINCT_WORDS;
                            type = PAGE_OUT_INT;
                            target = dm->entries;
                            arrptr = &dm->cache.dwords; 
                            break;
                        case CACHE_ID_TYPEEX: 
                            /* note that we're being dodgy and just processing
                             * the type exception array as an array of 
                             * unsigned ints */
                            dm->cache.cache |= DOCMAP_CACHE_LOCATION;
                            type = PAGE_OUT_INT;
                            assert(!(tmpl % 2));
                            target = tmpl;
                            dm->cache.typeex_len = dm->cache.typeex_size 
                              = tmpl / 2;
                            arrptr = (unsigned int **) &dm->cache.typeex; 
                            break;
                        case CACHE_ID_WORDS: 
                            dm->cache.cache |= DOCMAP_CACHE_WORDS;
                            type = PAGE_OUT_INT;
                            target = dm->entries;
                            arrptr = &dm->cache.words; 
                            break;
                        default: assert("can't get here" && 0);
                        }

                        /* arrange memory and read in cache data for each 
                         * type */
                        assert(dm->cache.size == dm->entries);
                        switch (type) {
                        case PAGE_OUT_LONG:
                            if (corrupt || !vec_len(&v)) break;
                            if (!*larrptr 
                              && !(*larrptr = malloc(sizeof(long) * target))) {
                                assert(!CRASH);
                                FAIL(DOCMAP_MEM_ERROR);
                            }

                            if ((tmpl = vec_vbyte_arr_read(&v, 
                                *larrptr + offset, entries, &bytes)) 
                              != entries) {
                                corrupt = 1;
                            }
                            break;

                        case PAGE_OUT_INT:
                            if (corrupt || !vec_len(&v)) break;
                            if (!*arrptr 
                              && !(*arrptr = malloc(sizeof(int) * target))) {
                                assert(!CRASH);
                                FAIL(DOCMAP_MEM_ERROR);
                            }

                            if ((tmpl = vec_int_arr_read(&v, 
                                *arrptr + offset, entries, &bytes)) 
                              != entries) {
                                corrupt = 1;
                            }
                            break;

                        case PAGE_OUT_FLT:
                            if (corrupt || !vec_len(&v)) break;
                            if (!*farrptr 
                              && !(*farrptr = malloc(sizeof(float) * target))) {
                                assert(!CRASH);
                                FAIL(DOCMAP_MEM_ERROR);
                            }

                            if ((tmpl = vec_flt_arr_read(&v, 
                                *farrptr + offset, entries, 
                                DOCMAP_WEIGHT_PRECISION, &bytes)) 
                              != entries) {
                                corrupt = 1;
                            }
                            break;

                        case PAGE_OUT_CHR:
                            if (corrupt || !vec_len(&v)) break;
                            if (!*carrptr 
                              && !(*carrptr = malloc(sizeof(float) * target))) {
                                assert(!CRASH);
                                FAIL(DOCMAP_MEM_ERROR);
                            }

                            if ((tmpl = vec_byte_read(&v, 
                                *carrptr + offset, entries)) != entries) {
                                corrupt = 1;
                            }
                            break;
                        }
                        break;

                    default:
                        corrupt = 1;
                        break;
                    }
                }
            } else {
                corrupt = 1;
            }
        } else {
            FAIL(DOCMAP_IO_ERROR);
        }
    }
    dm->map[dm->map_len] = ULONG_MAX;
    TIMINGS_END("docmap fastload");

    /* if anything goes wrong, docmap_cache_int() at the end */
    if (corrupt || !map || cache != dm->cache.cache) {
        /* note that we only force a reread (last argument) if we encountered
         * corruption or didn't read the mapping entries */
        if ((dmret = docmap_cache_int(dm, cache, corrupt | !map)) 
          != DOCMAP_OK) {
            FAIL(dmret);
        }
    }

    if (DEAR_DEBUG) {
        dmret = docmap_cache_check(dm);
        assert(dmret == DOCMAP_OK);
    }

    aggregate(dm);
    *ret = DOCMAP_OK;
    return dm;
}

void docmap_delete(struct docmap *dm) {
    dm->cache.cache = 0;
    cache_cleanup(dm);
    if (dm->write.entry.trecno) {
        free(dm->write.entry.trecno);
    }
    if (dm->read.entry.trecno) {
        free(dm->read.entry.trecno);
    }
    if (dm->map) {
        free(dm->map);
    }
    reposset_delete(dm->rset);
    free(dm->buf);
    dm->buf = NULL;
    free(dm);
}

static enum docmap_ret docmap_cache_check(struct docmap *dm) {
    unsigned int page;
    enum docmap_ret dmret;
    TIMINGS_DECL();
    TIMINGS_START();

    if (!dm->cache.cache) {
        return DOCMAP_OK;
    }

    /* need to read all entries in the docmap */
    for (page = 0; page < dm->map_len; page++) {
        if ((dmret = page_in(dm, page)) == DOCMAP_OK) {
            unsigned int len;

            if (page >= dm->readbuf.page 
              && page < dm->readbuf.page + dm->readbuf.buflen) {
                reset_cursor(dm, &dm->read, &dm->readbuf, page, ULONG_MAX);
            } else if (page >= dm->appendbuf.page 
              && page < dm->appendbuf.page + dm->appendbuf.buflen) {
                reset_cursor(dm, &dm->read, &dm->appendbuf, page, ULONG_MAX);
            } else {
                assert("can't get here" && 0);
                return DOCMAP_FMT_ERROR;
            }

            /* iterate through page entries */
            while ((dmret = decode(&dm->read)) 
              == DOCMAP_OK) {
                char buf[BUFSIZ + 1];

                if (((dm->cache.cache & DOCMAP_CACHE_WORDS)
                    && dm->read.entry.words
                      != dm->cache.words[dm->read.entry.docno])
                  || ((dm->cache.cache & DOCMAP_CACHE_LOCATION)
                    && dm->read.entry.bytes
                      != docmap_get_bytes_cached(dm, dm->read.entry.docno))
                  || ((dm->cache.cache & DOCMAP_CACHE_DISTINCT_WORDS)
                    && dm->read.entry.dwords
                      != dm->cache.dwords[dm->read.entry.docno])) {
                    assert(!CRASH);
                    return DOCMAP_FMT_ERROR;
                }

                if ((dm->cache.cache & DOCMAP_CACHE_WEIGHT)
                    && (dm->read.entry.weight
                      < 0.95 * dm->cache.weight[dm->read.entry.docno]
                      || dm->read.entry.weight
                      > 1.05 * dm->cache.weight[dm->read.entry.docno])) {
                    assert(!CRASH);
                    return DOCMAP_FMT_ERROR;
                }

                if (dm->cache.cache & DOCMAP_CACHE_TRECNO) {
                    dmret = docmap_get_trecno(dm, dm->read.entry.docno,
                        buf, BUFSIZ, &len);

                    if (len < BUFSIZ) {
                        buf[len] = '\0';
                        if (str_cmp(buf, dm->read.entry.trecno)) {
                            assert(!CRASH);
                            return DOCMAP_FMT_ERROR;
                        }
                    } else {
                        buf[BUFSIZ] = '\0';
                        if (str_ncmp(buf, dm->read.entry.trecno, BUFSIZ)) {
                            assert(!CRASH);
                            return DOCMAP_FMT_ERROR;
                        }
                    }
                }

                if (dm->cache.cache & DOCMAP_CACHE_LOCATION) {
                    unsigned int fileno,
                                 bytes;
                    off_t offset;
                    enum docmap_flag flags;
                    enum mime_types mtype;
                    
                    if (docmap_get_location(dm, dm->read.entry.docno, &fileno,
                        &offset, &bytes, &mtype, &flags) != DOCMAP_OK
                      || (fileno != dm->read.entry.fileno)) {
                        assert(!CRASH);
                        return DOCMAP_FMT_ERROR;
                    } else if (offset != dm->read.entry.offset) {
                        assert(!CRASH);
                        return DOCMAP_FMT_ERROR;
                    } else if (bytes != dm->read.entry.bytes) {
                        assert(!CRASH);
                        return DOCMAP_FMT_ERROR;
                    } else if (flags != dm->read.entry.flags) {
                        assert(!CRASH);
                        return DOCMAP_FMT_ERROR;
                    } else if (mtype != dm->read.entry.mtype) {
                        assert(!CRASH);
                        return DOCMAP_FMT_ERROR;
                    }
                }
            }
            assert(dmret == DOCMAP_BUFSIZE_ERROR);
        } else {
            assert(!CRASH);
            return DOCMAP_IO_ERROR;
        }
    }

    TIMINGS_END("docmap scan");
    return DOCMAP_OK;
}

