/* pyramid.c implements the object declared in pyramid.h
 *
 * written nml 2003-03-3
 *
 */

#include "firstinclude.h"

#include "pyramid.h"

#include "def.h"
#include "fdset.h"
#include "freemap.h"
#include "merge.h"
#include "storagep.h"
#include "str.h"
#include "timings.h"
#include "error.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* absolute minimum size of an input buffer during a merge */
#define MIN_INPUT 100

/* struct to represent a file that is waiting to be merged */
struct file {
    unsigned int fileno;          /* number of file in fdset */
    unsigned int level;           /* how many times this fileset has merged */
    int limited;                  /* whether this entry is at maximum size */
    int fd;                       /* file descriptor for this file, 
                                   * typically closed */
};

/* struct to manage a bunch of files waiting to be merged, which
 * maintains reception order during merging */
struct pyramid {
    unsigned int nextfile;        /* what fileid next file will receive */

    struct file *file;            /* array of files */
    unsigned int files;           /* how many in array */
    unsigned int capacity;        /* how many array can handle */
    unsigned int width;           /* how many files can be on each level 
                                     before we need to merge them */

    struct fdset *fds;            /* temporary files */
    unsigned int tmp_type;        /* type number for temporary files */
    unsigned int final_type;      /* type number of final files */
    unsigned int vocab_type;      /* type number of vocab files */

    struct storagep *storagep;    /* storage parameters for final merge */
    struct freemap *map;          /* freemap to remove stuff from in final 
                                   * merge */
    struct freemap *vmap;         /* vocab freemap to remove stuff from in 
                                   * final merger */

    int finished;                 /* whether the merger object has performed a 
                                   * final merge or not */
};

struct pyramid* pyramid_new(struct fdset *fds, unsigned int tmp_type, 
  unsigned int final_type, unsigned int vocab_type,
  unsigned int maxfiles, struct storagep *storagep, 
  struct freemap *map, struct freemap *vmap) {
    struct pyramid* p = malloc(sizeof(*p));

    if (p) {
        p->fds = fds;
        p->tmp_type = tmp_type;
        p->final_type = final_type;
        p->vocab_type = vocab_type;
        p->file = NULL;
        p->files = p->capacity = p->nextfile = 0;
        p->width = maxfiles;
        p->finished = 0;
        p->storagep = storagep;
        p->map = map;
        p->vmap = vmap;
    }

    return p;
}

/* internal function to check whether partial merge is required,
 * returns the first file requiring merging in entry, and the number
 * of files requiring merging in count.  Returns true if pyramid is
 * required. */
static int pyramid_merge_required(struct pyramid* pyramid, unsigned int* entry, 
  unsigned int* count) {
    unsigned int pos = 0,          /* our position in the array */
                 level = UINT_MAX, /* current level, starts highest level */
                 consecutive = 0,  /* number of consecutive mergeable files */
                 lastlimit = 0;    /* the last limited file + 1 */
    struct file *pf;               /* current file */

    /* the logic here is that either a too long sequential run of same
     * level files, or any multiple files prior to a filesize-limited
     * file require merging */
    while (pos < pyramid->files) {
        pf = &pyramid->file[pos];

        /* check for a limited size file */
        if (pf->limited) {
            if (consecutive > 1) {
                /* got to merge everything before a limited file to
                 * maintain merging order */
                *entry = lastlimit;
                *count = consecutive;
                return 1;
            } else {
                lastlimit = pos + 1;
                consecutive = 0;
                level = pf->level;
            }

        /* else check for a level change, cos we have to reset state then */
        } else if (pf->level != level) {
            /* if we have a same-level consecutive run of greater than our 
             * width parameter, we need to merge */
            if (consecutive >= pyramid->width) {
                *entry = pos - consecutive;
                *count = consecutive;
                return 1;
            }
            level = pf->level;
            consecutive = 1;
        } else {
            assert(pf->level == level);
            consecutive++;
        }
        pos++;
    }

    /* final check for too many consecutive entries */
    if (consecutive >= pyramid->width) {
        *entry = pos - consecutive;
        *count = consecutive;
        return 1;
    } else {
        return 0;
    }
}

/* internal function to grow the dynamic array */
static int pyramid_expand(struct pyramid* pyramid) {
    unsigned int newcapacity = pyramid->capacity * 2 + 1;
    void* ptr = realloc(pyramid->file, sizeof(*pyramid->file) * newcapacity);

    if (ptr) {
        pyramid->file = ptr;
        pyramid->capacity = newcapacity;
        return 1;
    }

    assert(!CRASH);
    return 0;
}

void pyramid_delete(struct pyramid* pyramid) {
    unsigned int i;
    struct file* pf;

    for (i = 0; i < pyramid->files; i++) {
        pf = &pyramid->file[i];

        if (pf->fd >= 0) {
            fdset_unpin(pyramid->fds, pyramid->tmp_type, pf->fileno, pf->fd);
            pf->fd = -1;
        }
    }

    for (i = 0; i <= pyramid->nextfile; i++) {
        /* unlink files (best effort) */
        fdset_unlink(pyramid->fds, pyramid->tmp_type, i);
    }

    pyramid->files = 0;

    if (pyramid->file) {
        free(pyramid->file);
    }

    free(pyramid);
    return;
}

#define MERGE_FAIL(pyramid, entry, count, ofd, vfd, fileno, vf, fm, imerge) \
  (ERROR("merge failed"), \
   merge_fail(pyramid, entry, count, ofd, vfd, fileno, vf, fm, imerge))

/* internal function to clean up after failed merging */
static void merge_fail(struct pyramid *pyramid, unsigned int entry, 
  unsigned int count, int outfd, int vfd, unsigned int fileno, 
  unsigned int vfileno, struct merge_final *fmerge, 
  struct merge_inter *imerge) {
    unsigned int i;

    if (outfd >= 0) {
        fdset_unpin(pyramid->fds, pyramid->tmp_type, fileno, outfd);
    }

    if (vfd >= 0) {
        fdset_unpin(pyramid->fds, pyramid->vocab_type, vfileno, vfd);
    }

    for (i = 0; i < count; i++) {
        if (pyramid->file[entry + i].fd >= 0) {
            fdset_unpin(pyramid->fds, pyramid->tmp_type, 
              pyramid->file[entry + i].fileno, 
              pyramid->file[entry + i].fd);
            pyramid->file[entry + i].fd = -1;
        }
    }
    if (fmerge) {
        merge_final_delete(fmerge);
    }
    if (imerge) {
        merge_inter_delete(imerge);
    }
}

int pyramid_merge(struct pyramid *pyramid, unsigned int *files, 
  unsigned int *vocabs, unsigned long int *dterms, unsigned int *terms_high, 
  unsigned int *terms_low, unsigned int *root_fileno, 
  unsigned long int *root_offset, void *buf, unsigned int bufsize) {
    struct merge_final merge;            /* merge object */
    struct merge_input *inputs = buf;    /* inputs array for merge */
    char *inbuf,                         /* input buffers */
         *outbuf,                        /* output buffer */
         *bigbuf,                        /* buffer for big reads */
         *tmp;                           /* writing location, so we don't 
                                          * move buf_out */
    unsigned int inbufsz,                /* size of each input buffer */
                 outbufsz,               /* size of output buffer */
                 bigbufsz;               /* size of big buffer */
    int len;                             /* length of write */
    enum merge_ret ret;                  /* return value from merge_final */
    unsigned int file = 0,               /* file we're writing to */ 
                 vfile = 0,              /* vocab file we're writing to */
                 i, 
                 input,                  /* id of the input we need to renew */
                 next_read;              /* how big the next read will be */
    int outfd = -1,                      /* output file descriptor */
        voutfd = -1;                     /* vocab output file descriptor */
    unsigned long int start,             /* where we began writing current 
                                          * file */
                      end,               /* where we ended writing current 
                                          * file */
                      vpos = 0;          /* where we are in vocab output file */

    /* save the number of pinned file descriptors, since it should be the same
     * when we exit */
    unsigned int pinned = fdset_pinned(pyramid->fds);

    TIMINGS_DECL();
    TIMINGS_START();

    /* can't full merge after a previous full merge */
    if (pyramid->finished) {
        assert(!CRASH);
        return -EINVAL;
    }

    if (bufsize < sizeof(*inputs) * pyramid->files) {
        assert(!CRASH);
        return -ENOMEM;
    }

    /* split up the remaining buffer evenly (one third for regular input,
     * one third for output, one third for large reads)  */
    inbufsz = outbufsz = bigbufsz 
      = (bufsize - (sizeof(*inputs) * pyramid->files)) / 3;

    if (pyramid->files) {
        inbufsz /= pyramid->files;
    }

    bigbuf = ((char *) buf) + sizeof(*inputs) * pyramid->files;
    outbuf = bigbuf + bigbufsz;
    inbuf = outbuf + outbufsz;

    /* ensure that we have enough space to do a decent merge */
    if (inbufsz < MIN_INPUT) {
        assert(!CRASH);
        return -ENOMEM;
    }

    /* pin all required fds */
    i = 0;
    while ((i < pyramid->files) 
      && ((pyramid->file[i].fd 
        = fdset_pin(pyramid->fds, pyramid->tmp_type, 
          pyramid->file[i].fileno, 0, SEEK_SET)) >= 0)) {
        inputs[i].next_in = inbuf + inbufsz * i;
        inputs[i].avail_in = 0;
        i++;
    }

    /* we may not have opened all of the files, fail (could possibly partial 
     * merge here, although what good it would do is questionable) */
    if (i < pyramid->files) {
        unsigned int j;

        assert(!CRASH);

        for (j = 0; j < i; j++) {
            fdset_unpin(pyramid->fds, pyramid->tmp_type, 
              pyramid->file[i].fileno, pyramid->file[i].fd);
        }

        assert(pinned == fdset_pinned(pyramid->fds));
        return -EMFILE;
    } else if (!i) {
        assert(!CRASH);
        MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file, vfile, 
          NULL, NULL);
        assert(pinned == fdset_pinned(pyramid->fds));
        return -EINVAL;
    }

    merge.input = inputs;
    merge.inputs = pyramid->files;

    if (!(merge_final_new(&merge, NULL, NULL, NULL, pyramid->storagep, 
        outbuf, outbufsz))) {

        assert(!CRASH);
        MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file, vfile, 
          NULL, NULL);
        assert(pinned == fdset_pinned(pyramid->fds));
        return -EINVAL;
    }

    /* pin output fd and seek it to start */
    if (((outfd = fdset_create_seek(pyramid->fds, pyramid->final_type, file, 
        merge.out.offset_out)) < 0) 
      && ((outfd != -EEXIST) 
        || ((outfd = fdset_pin(pyramid->fds, pyramid->final_type, file, 
            merge.out.offset_out, SEEK_SET)) < 0))) {

        unsigned int j;

        ERROR1("unable to open output file number %u for final merge", file);
        assert(!CRASH);

        for (j = 0; j < i; j++) {
            fdset_unpin(pyramid->fds, pyramid->tmp_type, 
              pyramid->file[i].fileno, pyramid->file[i].fd);
        }
        MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file, vfile, 
          &merge, NULL);

        assert(pinned == fdset_pinned(pyramid->fds));
        return outfd;
    }

    if ((voutfd = fdset_create_seek(pyramid->fds, pyramid->vocab_type, vfile,
        0)) < 0) {

        ERROR1("unable to open vocab output file number %u for final merge", 
          vfile);
        assert(!CRASH);
        MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file, vfile, 
          &merge, NULL);

        assert(pinned == fdset_pinned(pyramid->fds));
        return outfd;
    }

    file = merge.out.fileno_out;
    start = end = merge.out.offset_out;

    /* ok, now merge them */
    while ((ret = merge_final(&merge, &input, &next_read)) != MERGE_OK) {
        switch (ret) {
        case MERGE_INPUT:
            if ((bigbufsz > inbufsz) && (next_read > inbufsz)) {
                /* ensure that the big buffer is not in use */
                for (i = 0; i < pyramid->files; i++) {
                    assert(((merge.input[i].next_in < bigbuf) 
                        || (merge.input[i].next_in 
                          > bigbuf + bigbufsz)) 
                      || (merge.input[i].avail_in == 0));
                }

                /* use the big buffer */
                merge.input[input].next_in = bigbuf;

                /* ensure we don't try to read too much into the buffer */
                if (next_read > bigbufsz) {
                    next_read = bigbufsz;
                }
            } else {
                next_read = inbufsz;
                merge.input[input].next_in = inbuf + input * inbufsz;
            }

            /* use the normal buffer */
            errno = 0;
            if ((len = read(pyramid->file[input].fd, 
                merge.input[input].next_in, next_read)) > 0) {

                /* read succeeded (note that we can't assign to avail_in
                 * directly, because its an unsigned int) */
                merge.input[input].avail_in = len;

            /* reached EOF, let the merge module know */
            } else if (!len && !errno && (merge.input[input].next_in != bigbuf)
              && (merge_final_input_finish(&merge, input) == MERGE_OK)

              /* unlink file */
              && (fdset_unpin(pyramid->fds, pyramid->tmp_type, 
                  pyramid->file[input].fileno, pyramid->file[input].fd)
                == FDSET_OK)
              && ((pyramid->file[input].fd = -1), 1)
              && (fdset_unlink(pyramid->fds, pyramid->tmp_type, 
                  pyramid->file[input].fileno) == FDSET_OK)) {

                /* succeeded, do nothing */
            } else {
                assert(!CRASH);
                MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file, 
                  vfile, &merge, NULL);
                assert(pinned == fdset_pinned(pyramid->fds));
                return errno ? -errno : -EINVAL;
            }
            break;

        case MERGE_OUTPUT_BTREE:
            /* watch for switch of output file */
            if (vfile == merge.out_btree.fileno_out) {
                /* everything is fine, do nothing */
            } else if (vfile + 1 == merge.out_btree.fileno_out) {
                /* need to switch to the next file */
                unsigned int tmp = (unsigned int) vpos;

                /* allocate space used in current file */
                if (!pyramid->vmap 
                  || freemap_malloc(pyramid->vmap, &vfile, &vpos, &tmp, 
                    FREEMAP_OPT_EXACT | FREEMAP_OPT_LOCATION, vfile, 0)) {
                    /* allocation succeeded, do nothing */
                } else {
                    assert(!CRASH);
                    MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, 
                      file, vfile, &merge, NULL);
                    assert(pinned == fdset_pinned(pyramid->fds));
                    return errno ? -errno : -EINVAL;
                }
                vpos = 0;

                /* unpin existing output file */
                if ((fdset_unpin(pyramid->fds, pyramid->vocab_type, 
                    vfile, voutfd) == FDSET_OK) 
                  && ((voutfd = fdset_create_seek(pyramid->fds, 
                      pyramid->vocab_type, ++vfile, 
                      merge.out_btree.offset_out)) >= 0)) {

                } else {
                    assert(!CRASH);
                    MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file,
                      vfile, &merge, NULL);
                    assert(pinned == fdset_pinned(pyramid->fds));
                    return errno ? -errno : -EINVAL;
                }
            } else {
                /* don't know whats going on */
                assert(0);

                MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file, 
                  vfile, &merge, NULL);
                assert(pinned == fdset_pinned(pyramid->fds));
                return -EINVAL;
            }

            assert(vfile == merge.out_btree.fileno_out);
            assert((off_t) merge.out_btree.offset_out 
              == lseek(voutfd, 0, SEEK_CUR));

            /* write output to file */
            tmp = merge.out_btree.buf_out;
            while (merge.out_btree.size_out 
              && ((len = write(voutfd, tmp, merge.out_btree.size_out)) >= 0)) {
                merge.out_btree.size_out -= len;
                tmp += len;
                merge.out_btree.offset_out += len;
                vpos += len;
            }

            if (merge.out_btree.size_out) {
                assert(!CRASH);
                MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file, 
                  vfile, &merge, NULL);
                assert(pinned == fdset_pinned(pyramid->fds));
                return errno ? -errno : -EINVAL;
            }
            break;

        case MERGE_OUTPUT:
            /* watch for switch of output file */
            if (file == merge.out.fileno_out) {
                /* everything is fine, do nothing */
            } else if (file + 1 == merge.out.fileno_out) {
                /* need to switch to the next file */
                unsigned int tmpsize = end - start;

                /* allocate space used in current file */
                if (!pyramid->map 
                  || freemap_malloc(pyramid->map, &file, &start, &tmpsize, 
                    FREEMAP_OPT_EXACT | FREEMAP_OPT_LOCATION, file, start)) {
                    /* allocation succeeded, do nothing */
                } else {
                    assert(!CRASH);
                    MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file,
                      vfile, &merge, NULL);
                    assert(pinned == fdset_pinned(pyramid->fds));
                    return errno ? -errno : -EINVAL;
                }

                /* unpin existing output file */
                if ((fdset_unpin(pyramid->fds, pyramid->final_type, 
                    file, outfd) == FDSET_OK) 
                  && ((outfd = fdset_create_seek(pyramid->fds, 
                      pyramid->final_type, ++file, 
                      merge.out.offset_out)) >= 0)) {

                    start = merge.out.offset_out;
                } else {
                    assert(!CRASH);
                    MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file,
                      vfile, &merge, NULL);
                    assert(pinned == fdset_pinned(pyramid->fds));
                    return errno ? -errno : -EINVAL;
                }
            } else {
                /* don't know whats going on */
                assert(0);

                MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file, 
                  vfile, &merge, NULL);
                assert(pinned == fdset_pinned(pyramid->fds));
                return -EINVAL;
            }

            end = merge.out.offset_out + merge.out.size_out;

            assert(file == merge.out.fileno_out);
            assert((off_t) merge.out.offset_out == lseek(outfd, 0, SEEK_CUR));

            /* write output to file */
            tmp = merge.out.buf_out;
            while (merge.out.size_out 
              && ((len = write(outfd, tmp, merge.out.size_out)) >= 0)) {
                merge.out.size_out -= len;
                tmp += len;
                merge.out.offset_out += len;
            }

            if (merge.out.size_out) {
                assert(!CRASH);
                MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file, 
                  vfile, &merge, NULL);
                assert(pinned == fdset_pinned(pyramid->fds));
                return errno ? -errno : -EINVAL;
            }
            break;

        default:
            assert(!CRASH);
            MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file, vfile, 
              &merge, NULL);
            assert(pinned == fdset_pinned(pyramid->fds));
            return -EINVAL;
            break;
        }
    }

    /* allocate space used in current files */
    i = end - start;
    if (!pyramid->map 
      || freemap_malloc(pyramid->map, &file, &start, &i, 
        FREEMAP_OPT_EXACT | FREEMAP_OPT_LOCATION, file, start)) {
        /* allocation succeeded, do nothing */
    } else {
        assert(!CRASH);
        MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file, vfile, 
          &merge, NULL);
        assert(pinned == fdset_pinned(pyramid->fds));
        return errno ? -errno : -EINVAL;
    }
    i = vpos;
    if (!pyramid->vmap 
      || freemap_malloc(pyramid->vmap, &vfile, &vpos, &i, 
        FREEMAP_OPT_EXACT | FREEMAP_OPT_LOCATION, vfile, 0)) {
        /* allocation succeeded, do nothing */
    } else {
        assert(!CRASH);
        MERGE_FAIL(pyramid, 0, pyramid->files, outfd, voutfd, file, vfile, 
          &merge, NULL);
        assert(pinned == fdset_pinned(pyramid->fds));
        return errno ? -errno : -EINVAL;
    }

    *files = file + 1;
    *vocabs = vfile + 1;
    if (merge_final_finish(&merge, root_fileno, root_offset, dterms, 
        terms_high, terms_low) == MERGE_OK) {

        merge_final_delete(&merge);

        /* unpin fds */
        for (i = 0; i < pyramid->files; i++) {
            if (pyramid->file[i].fd >= 0) {
                fdset_unpin(pyramid->fds, pyramid->tmp_type, 
                  pyramid->file[i].fileno, pyramid->file[i].fd);
                pyramid->file[i].fd = -1;
            }
        }
        fdset_unpin(pyramid->fds, pyramid->final_type, file, outfd);
        fdset_unpin(pyramid->fds, pyramid->vocab_type, vfile, voutfd);
        outfd = -1;
        voutfd = -1;

        TIMINGS_END("final merging");

        /* use btsibling to apply sibling threading to btree leaves */
        pyramid->finished = 1;
        pyramid->nextfile = 0;      /* prevent attempted unlinking in delete */
        if (pinned == fdset_pinned(pyramid->fds)) {
            return PYRAMID_OK;
        } else {
            assert(!CRASH);
            return -EINVAL;
        }
    } else {
        assert(!CRASH);

        merge_final_delete(&merge);

        /* unpin fds */
        for (i = 0; i < pyramid->files; i++) {
            fdset_unpin(pyramid->fds, pyramid->tmp_type, 
              pyramid->file[i].fileno, pyramid->file[i].fd);
            pyramid->file[i].fd = -1;
        }
        fdset_unpin(pyramid->fds, pyramid->final_type, file, outfd);
        outfd = -1;

        pyramid->finished = 1;
        assert(pinned == fdset_pinned(pyramid->fds));
        return errno ? -errno : -EINVAL;
    }
}

int pyramid_pin_next(struct pyramid *pyramid) {
    if (!pyramid->finished) {
        int retval = fdset_create(pyramid->fds, pyramid->tmp_type, 
          pyramid->nextfile);
        if (retval < 0) {
            ERROR2("error pinning next file (number %u, type %u)", 
              pyramid->nextfile, pyramid->tmp_type);
            assert(!CRASH);
        }
        return retval; 

    } else {
        assert(!CRASH);
        return -1;
    }
}

int pyramid_unpin_next(struct pyramid *pyramid, int fd) {
    if (!pyramid->finished) {
        return fdset_unpin(pyramid->fds, pyramid->tmp_type, pyramid->nextfile, 
          fd);
    } else {
        assert(!CRASH);
        return -1;
    }
}

/* XXX: somewhat hacky struct to allow us to access merging state from
 * intermediate merge newfile callback */
struct pyramid_state {
    struct pyramid *p;      /* merging pyramid */
    int fd;                 /* fd we're merging into */
    unsigned int pos;       /* position in files array in pyramid we're 
                             * inserting to */
    int err;                /* last error code or 0 */
    unsigned int level;     /* level that highest level fd we're merging is */
};

void pyramid_change_file(void *opaque) {
    struct pyramid_state *state = opaque;
    struct pyramid *pyramid = state->p;
    int newfd;

    /* XXX: have a bit of a problem in that error reporting isn't really
     * possible from here.  Workaround is to have err and check it at
     * the end of the merge.  As a result, ensure that fd is rewound and 
     * working whenever we leave, even though that could produce funny 
     * results. */

    /* add this file into the merging pyramid */

    /* copy other files up to make room */
    assert(pyramid->files >= state->pos);
    if ((pyramid->capacity <= pyramid->files) && !pyramid_expand(pyramid)) {
        assert(!CRASH);
        state->err = ENOMEM;
        lseek(state->fd, 0, SEEK_SET);      
        return;
    }
    memcpy(&pyramid->file[state->pos + 1], &pyramid->file[state->pos],
      sizeof(*pyramid->file) * (pyramid->files - state->pos));

    /* pin new fd */
    if (((newfd = fdset_create_seek(pyramid->fds, pyramid->tmp_type, 
        pyramid->nextfile + 1, 0)) >= 0)) {

        /* unpin old fd */
        fdset_unpin(pyramid->fds, pyramid->tmp_type, pyramid->nextfile, 
          state->fd);
        state->fd = newfd;
        pyramid->file[state->pos].fileno = pyramid->nextfile++;
        pyramid->file[state->pos].level = state->level + 1;
        pyramid->file[state->pos].limited = 1;
        pyramid->file[state->pos].fd = -1;

        pyramid->files++;
        state->pos++;
    } else {
        assert(!CRASH);
        state->err = -newfd;
        lseek(state->fd, 0, SEEK_SET);      
    }
}

/* hint: the most common error i've made in this function is to address entries
 * in the pyramid->file array without using [entry + ...] */
int pyramid_partial_merge(struct pyramid* pyramid, unsigned int entry, 
  unsigned int count, void *buf, unsigned int bufsize) {
    struct merge_inter merge;            /* merge object */
    struct merge_input *inputs = buf;    /* inputs array for merge */
    char *inbuf,                         /* input buffers */
         *outbuf,                        /* output buffer */
         *bigbuf,                        /* buffer for big reads */
         *tmp;                           /* writing location, so we don't 
                                          * move buf_out */
    unsigned int inbufsz,                /* size of each input buffer */
                 outbufsz,               /* size of output buffer */
                 bigbufsz,               /* size of big buffer */
                 i,
                 input,                  /* index of input we need to renew */
                 next_read;              /* how big the next read will be */
    struct pyramid_state state;          /* some bits and pieces of merge 
                                          * state */
    int len;                             /* length of write */
    enum merge_ret ret;                  /* return value from merge_inter */

    /* keep number of pinned fds, should be the same when we exit */
    unsigned int pinned = fdset_pinned(pyramid->fds);

    assert(count && (count + entry <= pyramid->files));

    if (bufsize < sizeof(*inputs) * count) {
        assert(!CRASH);
        return -ENOMEM;
    }

    /* split up the remaining buffer evenly (one third for regular input,
     * one third for output, one third for large reads)  */
    inbufsz = outbufsz = bigbufsz 
      = (bufsize - (sizeof(*inputs) * count)) / 3;

    inbufsz /= count;

    bigbuf = ((char *) buf) + sizeof(*inputs) * count;
    outbuf = bigbuf + bigbufsz;
    inbuf = outbuf + outbufsz;

    assert(sizeof(*inputs) * count + bigbufsz + outbufsz + inbufsz <= bufsize);

    /* ensure that we have enough space to do a decent merge */
    if (inbufsz < MIN_INPUT) {
        assert(!CRASH);
        return -ENOMEM;
    }

    /* create output fd */
    state.p = pyramid;
    if ((state.fd = fdset_create(pyramid->fds, pyramid->tmp_type, 
        pyramid->nextfile)) < 0) {

        assert(!CRASH);
        return state.fd;
    }
    state.pos = entry + count;
    state.level = 0;
    state.err = 0;

    /* pin all required fds */
    i = 0;
    while ((i < count) 
      && ((pyramid->file[entry + i].fd 
        = fdset_pin(pyramid->fds, pyramid->tmp_type, 
          pyramid->file[entry + i].fileno, 0, SEEK_SET)) >= 0)) {

        if (pyramid->file[entry + i].level > state.level) {
            state.level = pyramid->file[entry + i].level;
        }

        inputs[i].next_in = inbuf + inbufsz * i;
        inputs[i].avail_in = 0;
        i++;
    }

    /* we may not have opened all of the files, thats (probably) ok */
    if (i < count) {
        /* check we didn't leak fd if lseek failed above */
        if (pyramid->file[entry + i].fd >= 0) {
            fdset_unpin(pyramid->fds, pyramid->tmp_type, 
              pyramid->file[entry + i].fileno, 
              pyramid->file[entry + i].fd);
            pyramid->file[entry + i].fd = -1;
        }

        count = i;
    } else if (!i) {
        assert(!CRASH);
        MERGE_FAIL(pyramid, entry, count, state.fd, -1, pyramid->nextfile, 0, 
          NULL, NULL);
        assert(pinned == fdset_pinned(pyramid->fds));
        return errno ? -errno : -EINVAL;
    }

    merge.input = inputs;
    merge.inputs = count;

    if (!(merge_inter_new(&merge, NULL, NULL, NULL, outbuf, outbufsz, 
        pyramid->storagep->max_termlen, &state, pyramid_change_file, 
        pyramid->storagep->max_filesize))) {

        assert(!CRASH);
        MERGE_FAIL(pyramid, entry, count, state.fd, -1, pyramid->nextfile, 0,
          NULL, NULL);
        assert(pinned == fdset_pinned(pyramid->fds));
        return -EINVAL;
    }

    /* ok, now merge them */
    while ((ret = merge_inter(&merge, &input, &next_read)) != MERGE_OK) {
        switch (ret) {
        case MERGE_INPUT:
            if ((bigbufsz > inbufsz) && (next_read > inbufsz)) {
                /* ensure that the big buffer is not in use */
                for (i = 0; i < count; i++) {
                    assert(((merge.input[i].next_in + merge.input[i].avail_in 
                          <= bigbuf) 
                        || (merge.input[i].next_in 
                          > bigbuf + bigbufsz)) 
                      || (merge.input[i].avail_in == 0));
                }

                /* use the big buffer */
                merge.input[input].next_in = bigbuf;

                /* ensure we don't try to read too much into the buffer */
                /* note that we restrict our read to size next_read so as to
                 * ensure that the big buffer is available next time we need it
                 * (otherwise it may still have stuff in it) */
                if (next_read > bigbufsz) {
                    next_read = bigbufsz;
                }
            } else {
                /* use the normal buffer */
                merge.input[input].next_in = inbuf + input * inbufsz;

                next_read = inbufsz;

                /* ensure that nothing else is using the normal buffer for this
                 * input */
                for (i = 0; i < count; i++) {
                    assert(((i == input) 
                      || ((merge.input[i].next_in + merge.input[i].avail_in
                          <= merge.input[input].next_in)
                        || (merge.input[i].next_in 
                          >= merge.input[input].next_in + inbufsz))));
                    assert((outbuf + outbufsz <= merge.input[i].next_in) 
                      || (outbuf >= merge.input[i].next_in + inbufsz));
                }
            }

            errno = 0;
            if ((len = read(pyramid->file[entry + input].fd, 
                merge.input[input].next_in, next_read)) > 0) {

                /* read succeeded (note that we can't assign to avail_in
                 * directly, because its an unsigned int, which hides 
                 * errors returned as negative numbers) */
                merge.input[input].avail_in = len;

            /* reached EOF, let the merge module know (its an error for the 
             * bigbuf, since we only tried to read an expected amount) */
            } else if (!len && !errno && (merge.input[input].next_in != bigbuf)
              && (merge_inter_input_finish(&merge, input) == MERGE_OK)

              /* unlink file */
              && (fdset_unpin(pyramid->fds, pyramid->tmp_type, 
                  pyramid->file[entry + input].fileno, 
                  pyramid->file[entry + input].fd) == FDSET_OK)
              && ((pyramid->file[entry + input].fd = -1), 1)
              && (fdset_unlink(pyramid->fds, pyramid->tmp_type, 
                  pyramid->file[entry + input].fileno) == FDSET_OK)) {

                /* succeeded, do nothing */
            } else {
                assert(!CRASH);
                MERGE_FAIL(pyramid, entry, count, state.fd, -1, 
                  pyramid->nextfile, 0, NULL, &merge);
                assert(pinned == fdset_pinned(pyramid->fds));
                return errno ? -errno : -EINVAL;
            }
            break;

        case MERGE_OUTPUT:
            /* write output to file */
            tmp = merge.buf_out;
            while (merge.size_out 
              && ((len = write(state.fd, tmp, merge.size_out)) >= 0)) {
                merge.size_out -= len;
                tmp += len;
            }

            if (merge.size_out) {
                assert(!CRASH);
                MERGE_FAIL(pyramid, entry, count, state.fd, -1, 
                  pyramid->nextfile, 0, NULL, &merge);
                assert(pinned == fdset_pinned(pyramid->fds));
                return errno ? -errno : -EINVAL;
            }
            break;

        default:
            assert(!CRASH);
            MERGE_FAIL(pyramid, entry, count, state.fd, -1, 
              pyramid->nextfile, 0, NULL, &merge);
            assert(pinned == fdset_pinned(pyramid->fds));
            return -EINVAL;
            break;
        }
    }

    merge_inter_delete(&merge);

    /* add final new file */

    /* copy other files up to make room */
    assert(pyramid->files >= state.pos);
    if ((pyramid->capacity <= pyramid->files) && !pyramid_expand(pyramid)) {
        assert(!CRASH);
        MERGE_FAIL(pyramid, entry, count, state.fd, -1, pyramid->nextfile, 0, 
          NULL, &merge);
        return errno ? -errno : -EINVAL;
    }
    memcpy(&pyramid->file[state.pos + 1], &pyramid->file[state.pos],
      sizeof(*pyramid->file) * (pyramid->files - state.pos));

    fdset_unpin(pyramid->fds, pyramid->tmp_type, pyramid->nextfile, state.fd);
    pyramid->file[state.pos].fileno = pyramid->nextfile++;
    pyramid->file[state.pos].level = state.level + 1;
    pyramid->file[state.pos].limited = 0;
    pyramid->file[state.pos].fd = -1;
    pyramid->files++;

    /* source fds should have already been unpinned */
    for (i = 0; i < count; i++) {
        assert(pyramid->file[entry + i].fd < 0);
    }

    /* remove old files */
    memmove(&pyramid->file[entry], &pyramid->file[entry + count], 
      (pyramid->files - entry - count) * sizeof(*pyramid->file));
    pyramid->files -= count;

    /* check that we didn't get any errors */
    if (state.err) {
        assert(!CRASH);
        return -state.err;
    }

    if (pinned == fdset_pinned(pyramid->fds)) {
        return PYRAMID_OK;
    } else {
        assert(!CRASH);
        return -EINVAL;
    }
}

int pyramid_add_file(struct pyramid* pyramid, int allow, void *buf, 
  unsigned int bufsize) {
    unsigned int entry,
                 count;
    int ret;

    TIMINGS_DECL();
    TIMINGS_START();

    if (pyramid->finished) {
        assert(!CRASH);
        return -EINVAL;
    }

    if ((pyramid->capacity <= pyramid->files) && !pyramid_expand(pyramid)) {
        assert(!CRASH);
        return -ENOMEM;
    }

    pyramid->file[pyramid->files].fileno = pyramid->nextfile++;
    pyramid->file[pyramid->files].level = 0;
    pyramid->file[pyramid->files].limited = 0;
    pyramid->file[pyramid->files].fd = -1;
    pyramid->files++;

    while (allow && pyramid_merge_required(pyramid, &entry, &count)) {
        if ((ret = pyramid_partial_merge(pyramid, entry, count, buf, bufsize)) 
          != PYRAMID_OK) {
            assert(!CRASH);
            ERROR("performing pyramid partial merge");
            return ret;
        }
    }

    TIMINGS_END("merging");
    return PYRAMID_OK;
}

