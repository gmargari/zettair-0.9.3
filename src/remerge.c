/* remerge.c implements update via merging new postings with the old index
 *
 * written nml 2003-06-01 
 *
 */

#include "firstinclude.h"

#include "index.h"
#include "_index.h"

#include "def.h"
#include "btbulk.h"
#include "fdset.h"
#include "_postings.h"
#include "postings.h"
#include "iobtree.h"
#include "str.h"
#include "vec.h"
#include "vocab.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

/* pointer to a file within the index */
struct filep {
    int fd;
    unsigned int type;
    unsigned int fileno;
    unsigned long int offset;
    char *buf;
    unsigned int bufpos;
    unsigned int bufsize;
    unsigned int buflen;
};

static ssize_t outbuf(struct filep *out, char *buf, unsigned int len, 
  struct fdset *fd) {
    unsigned int bufspace = out->bufsize - out->buflen;
    ssize_t ret;

    assert(out->buflen <= out->bufsize);
    if (len >= bufspace) {
        if (len >= out->bufsize) {
            /* too big for the buffer, write current contents of buffer then 
             * out buffer to file */
            if (out->buflen) {
                if ((ret = index_atomic_write(out->fd, out->buf, out->buflen)) 
                  == (ssize_t) out->buflen) {
                    out->offset += out->buflen;
                } else {
                    assert(ret < 0);
                    return ret;
                }
            }
            if ((ret = index_atomic_write(out->fd, buf, len)) 
              == (ssize_t) len) {
                out->offset += len;
            } else {
                assert(ret < 0);
                return ret;
            }
            out->buflen = out->bufpos = 0;
        } else {
            /* small enough for buffer individually, fill buffer to limit, 
             * write it out and buffer rest */
            assert(bufspace <= len);
            assert(out->buflen == out->bufpos);
            memcpy(out->buf + out->bufpos, buf, bufspace);
            if ((ret = index_atomic_write(out->fd, out->buf, out->bufsize)) 
              == (ssize_t) out->bufsize) {
                out->offset += out->bufsize;
            } else {
                assert(ret < 0);
                return ret;
            }
            memcpy(out->buf, buf + bufspace, len - bufspace);
            out->buflen = out->bufpos = len - bufspace;
        }
    } else {
        /* small enough to fit into buffer now */
        assert(out->buflen == out->bufpos);
        memcpy(out->buf + out->bufpos, buf, len);
        out->buflen += len;
        out->bufpos += len;
        assert(out->buflen <= out->bufsize);
    }

    return len;
}

/* internal function to load next vocab entry */
static enum btbulk_ret vocab_load(struct btbulk_read *vocab, struct filep *fp, 
  struct fdset *fdset, unsigned int pagesize) {
    enum btbulk_ret ret;
    int fdret;

    do {
        ret = btbulk_read(vocab);
        switch (ret) {
        case BTBULK_READ:
            if (vocab->output.read.fileno != fp->fileno) {
                /* switch to new fd */

                /* unpin old fd, if necessary */
                if (fp->fd >= 0) {
                    if ((fdret 
                      = fdset_unpin(fdset, fp->type, fp->fileno, fp->fd)) 
                        != FDSET_OK) {

                        assert(!CRASH);
                        return BTBULK_ERR;
                    }
                }

                /* pin new fd */
                fp->fileno = vocab->output.read.fileno;
                if ((fp->fd = fdset_pin(fdset, fp->type, fp->fileno, 
                    vocab->output.read.offset, SEEK_SET)) >= 0) {

                    fp->offset = vocab->output.read.offset;
                    vocab->fileno_in = fp->fileno;
                } else {
                    fp->fileno = -1;
                    return BTBULK_ERR;
                }
            }

            /* seek to appropriate offset */
            if (vocab->output.read.offset != fp->offset) {
                if (lseek(fp->fd, vocab->output.read.offset, SEEK_SET) 
                  == (ssize_t) vocab->output.read.offset) {
                    fp->offset = vocab->output.read.offset;
                } else {
                    assert(!CRASH);
                    return BTBULK_ERR;
                }
            }

            /* read from fd */
            if ((vocab->avail_in = read(fp->fd, fp->buf, fp->bufsize)) 
              >= pagesize) {
                fp->bufpos = 0;
                fp->buflen = vocab->avail_in;
                vocab->next_in = fp->buf;
                vocab->offset_in = fp->offset;
                fp->offset += vocab->avail_in;
            } else {
                vocab->avail_in = 0;
                fp->buflen = fp->bufpos = 0;
                fp->offset = -1;
                return BTBULK_ERR;
            }
            break;

        case BTBULK_OK:
        case BTBULK_FINISH:
        default:
            return ret;
        }
    } while (1);
}

/* internal function to perform remerge, since the real function had too much
 * other crap in it.  Returns number of vector files created, or 0 on error */
static unsigned int actual_remerge(struct index *idx, struct filep *in, 
  struct filep *vin, struct btbulk_read *old_vocab, struct filep *out, 
  struct filep *vout, struct btbulk *new_vocab, 
  struct fdset *fd, struct postings_node **posting, unsigned int postings,
  unsigned long int *nt, unsigned int *root_fileno,
  unsigned long int *root_offset) {
    struct postings_node **posting_end = posting + postings;
    enum btbulk_ret readret,
                    btret;
    int cmp;
    unsigned long int terms = 0;
    struct vocab_vector nve,
                        ve;
    const unsigned int pagesize = idx->storage.pagesize;
    ssize_t sizeret;

    /* load first entry from old vocab */
    if ((readret = vocab_load(old_vocab, vin, fd, pagesize)) != BTBULK_OK 
      && readret != BTBULK_FINISH) {
        assert(!CRASH);
        return 0;
    }

    /* these properties are invariant over all new vocab entries written out */
    nve.attr = VOCAB_ATTRIBUTES_NONE;
    nve.type = VOCAB_VTYPE_DOCWP;
    nve.location = VOCAB_LOCATION_FILE;

    while ((posting < posting_end) || (readret != BTBULK_FINISH)) {
        if (posting < posting_end) {
            new_vocab->termlen = str_len((*posting)->term);
            if (readret == BTBULK_OK) {
                cmp = str_nncmp(old_vocab->output.ok.term, 
                    old_vocab->output.ok.termlen, 
                    (*posting)->term, new_vocab->termlen);
            } else {
                /* use new postings only */
                cmp = 1;
            }
        } else {
            /* use old postings only */
            assert(readret == BTBULK_OK);
            cmp = -1;
        }

        terms++;
        nve.size = 0;
        nve.header.docwp.docs = 0;
        nve.header.docwp.occurs = 0;
        new_vocab->datasize = 0;
        nve.header.docwp.last = -1;
        nve.loc.file.fileno = out->fileno;
        nve.loc.file.offset = out->offset + out->buflen;

        if (cmp <= 0) {
            struct vec vv;
            enum vocab_ret vret;
            unsigned int bytes;

            new_vocab->term = old_vocab->output.ok.term;
            new_vocab->termlen = old_vocab->output.ok.termlen;

            vv.pos = (void *) old_vocab->output.ok.data;
            vv.end = vv.pos + old_vocab->output.ok.datalen;
            while ((vret = vocab_decode(&ve, &vv)) == VOCAB_OK) {
                switch (ve.type) {
                case VOCAB_VTYPE_DOC:
                case VOCAB_VTYPE_DOCWP:
                    /* must be the only vector available */
                    assert(nve.size == 0);
                    assert(nve.location == VOCAB_LOCATION_FILE);

                    /* copy it to the new index */
                    nve.size += ve.size;
                    nve.header.doc.docs += ve.header.doc.docs;
                    nve.header.doc.occurs += ve.header.doc.occurs;
                    nve.header.doc.last = ve.header.doc.last;

                    /* check whether we're about to go above file size 
                     * limit for output file */
                    if (((cmp == 0) 
                        && (out->offset + out->buflen 
                          > idx->storage.max_filesize - nve.size 
                            - ((*posting)->vec.pos - (*posting)->vecmem)))
                      || ((cmp < 0) 
                        && (out->offset + out->buflen
                          > idx->storage.max_filesize - nve.size))) {
                        /* unpin old fd */
                        if (out->fd >= 0) {
                            /* flush buffer */
                            if (out->buflen && index_atomic_write(out->fd, 
                                out->buf, out->buflen) 
                              == (ssize_t) out->buflen) {

                                out->buflen = out->bufpos = 0;
                            } else {
                                assert(!CRASH);
                                return 0;
                            }
                            fdset_unpin(fd, out->type, out->fileno, out->fd);
                        }

                        /* pin new fd */
                        if ((out->fd 
                          = fdset_create(fd, out->type, out->fileno + 1)) 
                            >= 0) {

                            out->fileno++;
                            out->offset = 0;
                        } else {
                            out->offset = -1;
                            assert(!CRASH);
                            return 0;
                        }

                        nve.loc.file.fileno = out->fileno;
                        nve.loc.file.offset = out->offset + out->buflen;
                    }

                    /* pin correct in fd */
                    assert((in->fd < 0) 
                      || lseek(in->fd, 0, SEEK_CUR) 
                        == (ssize_t) in->offset);
                    if (ve.loc.file.fileno != in->fileno) {
                        if (in->fd >= 0) {
                            fdset_unpin(fd, in->type, in->fileno, in->fd);
                            in->fd = -1;
                        }
                        if ((in->fd = fdset_pin(fd, in->type, in->fileno + 1, 
                            ve.loc.file.offset, SEEK_SET)) >= 0) {

                            in->fileno++;
                            in->offset = ve.loc.file.offset;
                        } else {
                            assert(!CRASH);
                            return 0;
                        }
                    }

                    /* seek to correct in position */
                    assert(lseek(in->fd, 0, SEEK_CUR) 
                      == (ssize_t) in->offset);
                    assert(ve.loc.file.offset 
                      == in->offset + in->bufpos - in->buflen);
                    if (ve.loc.file.offset < in->offset - in->buflen
                      || (ve.loc.file.offset > in->offset)) {
                        if (lseek(in->fd, ve.loc.file.offset, SEEK_SET) 
                          == (ssize_t) ve.loc.file.offset) {
                            in->offset = ve.loc.file.offset;
                        } else {
                            assert(!CRASH);
                            return 0;
                        }
                    }
                    assert(lseek(in->fd, 0, SEEK_CUR) 
                      == (ssize_t) in->offset);

                    /* copy (possibly partially) buffered segment from input to
                     * output */
                    bytes = nve.size;
                    if (ve.size && (ve.size >= (in->buflen - in->bufpos))) {
                        if (outbuf(out, in->buf + in->bufpos, 
                            in->buflen - in->bufpos, fd)
                          == (ssize_t) (in->buflen - in->bufpos)) {
                            ve.size -= in->buflen - in->bufpos;
                            in->buflen = in->bufpos = 0;
                        } else {
                            assert(!CRASH);
                            return 0;
                        }
                    }

                    /* copy full bufferloads from input to output */
                    while (ve.size > in->bufsize) {
                        assert(in->buflen == 0);
                        assert(in->bufpos == 0);
                        if (((in->buflen
                          = read(in->fd, in->buf, in->bufsize)) > 0)
                          && (outbuf(out, in->buf, in->buflen, fd)) 
                            == (ssize_t) in->buflen) {

                            ve.size -= in->buflen;
                            in->offset += in->buflen;
                            assert(lseek(in->fd, 0, SEEK_CUR) 
                              == (ssize_t) in->offset);
                        } else {
                            assert(!CRASH);
                            return 0;
                        }
                    }
                    assert((ve.size < (in->buflen - in->bufpos)) 
                      || !in->buflen);

                    /* read in last bufferload if necessary */
                    if (ve.size && !in->buflen 
                      && (bytes = read(in->fd, in->buf, in->bufsize)) 
                          != -1) {

                        in->buflen = bytes;
                        in->offset += in->buflen;
                        assert(lseek(in->fd, 0, SEEK_CUR) 
                          == (ssize_t) in->offset);
                    } else if (ve.size && !in->buflen) {
                        assert(!CRASH);
                        return 0;
                    }

                    /* copy last buffered segment from input to output */
                    assert(ve.size <= (in->buflen - in->bufpos));
                    if (ve.size 
                      && (outbuf(out, in->buf + in->bufpos, ve.size, fd) 
                        == (ssize_t) ve.size)) {

                        in->bufpos += ve.size;
                    } else if (ve.size) {
                        assert(!CRASH);
                        return 0;
                    }
                    break;

                case VOCAB_VTYPE_IMPACT:
                    /* impacts are now out-of-date, remove them from the 
                     * index */
                    break;
                }
            }
            assert(vret == VOCAB_END);
            assert(vec_len(&vv) == 0);
        } else {
            assert(cmp > 0);

            assert(new_vocab->termlen == str_len((*posting)->term));
            new_vocab->term = (*posting)->term;

            /* check whether we're about to go above file size 
             * limit for output file */
            if (out->offset + out->buflen > idx->storage.max_filesize 
                  - ((*posting)->vec.pos - (*posting)->vecmem)) {
                /* unpin old fd */
                if (out->fd >= 0) {
                    /* flush buffer */
                    if (out->buflen && index_atomic_write(out->fd, 
                        out->buf, out->buflen) 
                      == (ssize_t) out->buflen) {

                        out->buflen = out->bufpos = 0;
                    } else {
                        assert(!CRASH);
                        return 0;
                    }
                    fdset_unpin(fd, out->type, out->fileno, out->fd);
                }

                /* pin new fd */
                if ((out->fd 
                  = fdset_create(fd, out->type, out->fileno + 1))
                    >= 0) {

                    out->fileno++;
                    out->offset = 0;
                } else {
                    out->offset = -1;
                    assert(!CRASH);
                    return 0;
                }

                nve.loc.file.fileno = out->fileno;
                nve.loc.file.offset = out->offset + out->buflen;
            }
        }

        if (cmp >= 0) {
            struct vec front;
            unsigned long int first;
            unsigned int bytes;

            /* re-encode first docno in in-memory vector */
            front.pos = (*posting)->vecmem;
            front.end = (*posting)->vec.pos;

            bytes = vec_vbyte_read(&front, &first);
            assert(bytes);
            first -= (nve.header.doc.last + 1);
            bytes = vec_vbyte_len(first);
            front.pos -= bytes;
            vec_vbyte_write(&front, first);
            front.pos -= bytes;

            nve.size += vec_len(&front);
            nve.header.doc.docs += (*posting)->docs;
            nve.header.doc.occurs += (*posting)->occurs;
            nve.header.doc.last = (*posting)->last_docno;

            /* write in-memory vector to output */
            bytes = vec_len(&front);
            if (outbuf(out, front.pos, bytes, fd) == (ssize_t) bytes) {
                /* do nothing */
            } else {
                assert(!CRASH);
                return 0;
            }
        }

        /* insert new vocab entry */
        nve.loc.file.capacity = nve.size;
        new_vocab->datasize = vocab_len(&nve);
        do {
            struct vec datav;
            enum vocab_ret vret;

            switch ((btret = btbulk_insert(new_vocab))) {
            case BTBULK_WRITE:
                /* seek to correct location */
                assert(vout->fileno == new_vocab->fileno);
                if (new_vocab->offset != vout->offset) {
                    if (lseek(vout->fd, new_vocab->offset, SEEK_SET) 
                      == (ssize_t) new_vocab->offset) {
                        vout->offset = new_vocab->offset;
                    } else {
                        assert(!CRASH);
                        return 0;
                    }
                }

                /* write new output */
                if ((sizeret = index_atomic_write(vout->fd, 
                      new_vocab->output.write.next_out, 
                      new_vocab->output.write.avail_out)) 
                    == (ssize_t) new_vocab->output.write.avail_out) {

                    new_vocab->offset += sizeret;
                } else {
                    assert(!CRASH);
                    return 0;
                }
                break;

            case BTBULK_FLUSH:
                /* need to start a new file */

                /* unpin previous fd */
                if (vout->fd >= 0) {
                    fdset_unpin(fd, vout->type, vout->fileno, vout->fd);
                    vout->fd = -1;
                }

                /* pin new fd */
                if ((vout->fd 
                  = fdset_create(fd, vout->type, vout->fileno + 1)) 
                    >= 0) {

                    vout->fileno++;
                    new_vocab->offset = vout->offset = 0;
                    new_vocab->fileno = vout->fileno;
                } else {
                    assert(!CRASH);
                    return 0;
                }
                break;

            case BTBULK_OK:
                /* insertion succeeded, write vocab entry in */
                datav.pos = new_vocab->output.ok.data;
                datav.end = datav.pos + new_vocab->datasize;
                vret = vocab_encode(&nve, &datav);
                assert(vret == VOCAB_OK);
                assert(vec_len(&datav) == 0);
                break;

            default:
                assert(!CRASH);
                return 0;
            }
        } while (btret != BTBULK_OK);

        if (cmp <= 0) {
            /* advance to next entry from old vocab */
            if ((readret = vocab_load(old_vocab, vin, fd, pagesize)) 
                != BTBULK_OK 
              && readret != BTBULK_FINISH) {
                assert(!CRASH);
                return 0;
            }
        }
        if (cmp >= 0) {
            /* advance to next entry from new postings */
            posting++;
        }
    }

    /* finalise vocabulary */
    do {
        switch ((btret 
          = btbulk_finalise(new_vocab, root_fileno, root_offset))) {
        case BTBULK_WRITE:
            /* seek to correct location */
            assert(vout->fileno == new_vocab->fileno);
            if (new_vocab->offset != vout->offset) {
                if (lseek(vout->fd, new_vocab->offset, SEEK_SET) 
                  == (ssize_t) new_vocab->offset) {
                    vout->offset = new_vocab->offset;
                } else {
                    assert(!CRASH);
                    return 0;
                }
            }

            /* write new output */
            if ((sizeret = index_atomic_write(vout->fd, 
                  new_vocab->output.write.next_out, 
                  new_vocab->output.write.avail_out)) 
                == (ssize_t) new_vocab->output.write.avail_out) {

                new_vocab->offset += sizeret;
            } else {
                assert(!CRASH);
                return 0;
            }
            break;

        case BTBULK_FLUSH:
            /* need to start a new file */

            /* unpin previous fd */
            if (vout->fd >= 0) {
                fdset_unpin(fd, vout->type, vout->fileno, vout->fd);
                vout->fd = -1;
            }

            /* pin new fd */
            if ((vout->fd 
              = fdset_create(fd, vout->type, vout->fileno + 1))
                >= 0) {

                vout->fileno++;
                new_vocab->offset = vout->offset = 0;
                new_vocab->fileno = vout->fileno;
            } else {
                assert(!CRASH);
                return 0;
            }
            break;

        case BTBULK_FINISH:
        case BTBULK_OK:
            btret = BTBULK_FINISH;
            break;

        default:
            assert(!CRASH);
            return 0;
        }
    } while (btret != BTBULK_FINISH);

    /* flush output buffer */
    if (out->bufpos) {
        if ((sizeret = index_atomic_write(out->fd, out->buf, out->bufpos)) 
            != (ssize_t) out->bufpos) {
            assert(!CRASH);
            return 0;
        }
    }

    *nt = terms;
    return out->fileno + 1;
}

int index_remerge(struct index *idx, unsigned int opts, 
  struct index_commit_opt *opt) {
    struct filep in = {-1, -1, -1, -1, NULL, 0, 0, 0},
                 out = {-1, -1, -1, -1, NULL, 0, 0, 0},
                 vin = {-1, -1, -1, -1, NULL, 0, 0, 0},
                 vout = {-1, -1, -1, -1, NULL, 0, 0, 0};
    struct postings_node **posting = NULL,
                         *node;
    struct btbulk new_vocab,
                  *new_vocab_ptr = NULL;
    struct btbulk_read old_vocab;
    unsigned int i,
                 j,
                 vectors = idx->vectors,
                 len,
                 bufsize,
                 root_fileno,
                 postings = postings_distinct_terms(idx->post);
    unsigned long int terms,
                      root_offset;
    struct iobtree *tmpbtree;

    assert(!postings_needs_update(idx->post));

    if (opts & INDEX_COMMIT_DUMPBUF) {
        bufsize = opt->dumpbuf;

        /* minimum of a page for each buffer */
        if (bufsize < 4 * idx->storage.pagesize) {
            bufsize = 4 * idx->storage.pagesize;
        }
    } else {
        /* default is for 5 pages per buffer */
        bufsize = 5 * 4 * idx->storage.pagesize;
    }

    vin.type = idx->vocab_type;
    vout.type = idx->vtmp_type;
    in.type = idx->index_type;
    out.type = idx->tmp_type;

    /* first, need to create a new set of index files */
    if ((posting = malloc(sizeof(*posting) * postings))
      /* note that vout gets a buffer within btbulk */
      && (in.bufsize = bufsize / 4)
      && (in.buf = malloc(in.bufsize))
      && (out.bufsize = bufsize / 4)
      && (out.buf = malloc(out.bufsize))
      && (vin.bufsize = bufsize / 4)
      && (vin.buf = malloc(vin.bufsize))
      /* pin first vocab output fd, for convenience later */
      && ((vout.fd = fdset_create(idx->fd, vout.type, 0)) >= 0)
      && (new_vocab_ptr = btbulk_new(idx->storage.pagesize, 
          idx->storage.max_filesize, idx->storage.btleaf_strategy, 
          idx->storage.btnode_strategy, 1.0, 
          bufsize / (4 * idx->storage.pagesize), &new_vocab))
      && btbulk_read_new(idx->storage.pagesize, idx->storage.btleaf_strategy, 
          0, 0, &old_vocab)) {

        new_vocab.fileno = 0;
        new_vocab.offset = 0;

        vout.fileno = 0;
        vout.offset = 0;

        /* copy nodes into array */
        j = 0;
        for (i = 0; i < idx->post->tblsize; i++) {
            node = idx->post->hash[i];
            while (node) {
                posting[j++] = node;
                node = node->next;
            }
        }
        assert(j == postings);

        /* sort postings lexographically by term and perform merge */
        qsort(posting, postings, sizeof(*posting), post_cmp);
        idx->vectors
          = actual_remerge(idx, &in, &vin, &old_vocab, &out, &vout, &new_vocab,
            idx->fd, posting, postings, &terms, &root_fileno, &root_offset);

        btbulk_delete(&new_vocab);
        btbulk_read_delete(&old_vocab);
        free(posting);
        free(vin.buf);
        free(in.buf);
        free(out.buf);

        /* unpin fds */
        if (vin.fd >= 0) {fdset_unpin(idx->fd, vin.type, vin.fileno, vin.fd);}
        if (vout.fd >= 0) {
            fdset_unpin(idx->fd, vout.type, vout.fileno, vout.fd);
        }
        if (in.fd >= 0) {fdset_unpin(idx->fd, in.type, in.fileno, in.fd);}
        if (out.fd >= 0) {fdset_unpin(idx->fd, out.type, out.fileno, out.fd);}

        /* quickload new vocab */
        if (idx->vectors 
          && (tmpbtree = iobtree_load_quick(idx->storage.pagesize, 
            idx->storage.btleaf_strategy, idx->storage.btnode_strategy, 
            NULL, idx->fd, idx->vtmp_type, root_fileno, root_offset, terms))) {

            assert(iobtree_size(tmpbtree) >= iobtree_size(idx->vocab));
            iobtree_delete(idx->vocab);
            for (i = 0; fdset_unlink(idx->fd, idx->vocab_type, i) == FDSET_OK; 
              i++) ;
            idx->vocab = tmpbtree;
            len = idx->vocab_type;
            idx->vocab_type = idx->vtmp_type;
            idx->vtmp_type = len;
            idx->vocabs = vout.fileno + 1;
        } else {
            assert(!CRASH);
            return 0;
        }

        if (idx->vectors) {
            /* swap index and tmp types so that queries/updates start 
             * occurring from new index */
            len = idx->index_type;
            idx->index_type = idx->tmp_type;
            idx->tmp_type = len;

            /* unlink all of the old files */
            for (i = 0; i < vectors; i++) {
                if (fdset_unlink(idx->fd, idx->tmp_type, i) == FDSET_OK) {
                    /* successful, do nothing */
                } else {
                    /* unexpected error */
                    assert(!CRASH);
                    return 0;
                }
            }

            return 1;
        } else {
            idx->vectors = vectors;
            assert(!CRASH);
            return 0;
        }
    } else {
        assert(!CRASH);
        if (vout.fd >= 0) {
            fdset_unpin(idx->fd, vout.type, 0, vout.fd);
        }
        if (in.buf) {
            free(in.buf);
        }
        if (out.buf) {
            free(out.buf);
        }
        if (vin.buf) {
            free(vin.buf);
        }
        if (new_vocab_ptr) {
            btbulk_delete(new_vocab_ptr);
        }
        if (posting) {
            free(posting);
        }
    }

    return 0;
}

