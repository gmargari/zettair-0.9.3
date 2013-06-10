/* fdset.c implements the interface declared in fdset.h.  It will be
 * implemented by keeping a hashtable full of names for each of the different
 * types of fds, along with a template name that will be used if no specific
 * name is provided for a fileno.  The template is the type name specified with
 * a single ".%u" printf expansion appended to it.
 *
 * written nml 2003-02-13
 *
 */

#include "firstinclude.h"

#include "fdset.h"

#include "def.h"
#include "chash.h"
#include "str.h"
#include "zstdint.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

/* Make multi-thread safe.  Note: only the fdset_pin and fdset_unpin
   functions obey this. */
#ifdef MT_ZET
#include <pthread.h>
#endif /* MT_ZET */

/* struct to represent a specific filename for a given type, fileno pair */
struct specific {
    const char *filename;                 /* name of specific file */
    int write;                            /* whether to enable writing */
};

/* struct to hold specific and general information about a type */
struct type {
    const char *template;                 /* filename template (containing one 
                                           * %u printf conversion) */
    int write;                            /* whether to enable writing */
    struct chash *specific;               /* hashtable of specific entries */
};

/* struct to represent an open file descriptor */
struct fd {
    int fd;                               /* file descriptor */
    unsigned int lru_count;               /* count to approximate LRU, 
                                           * UINT_MAX if fd is pinned */
    unsigned int type;                    /* type of fd */
    unsigned int fileno;                  /* fd file number */
    struct fd * next;                     /* to place in a list */
};

/* internal function to give hash values for fds */
static unsigned int fd_hash(const void *vfd) {
    const struct fd *fd = vfd;

    /* basic hash function - give type upper byte only (since its likely to be
     * small) and xor with fileno */
    return fd->fileno ^ (fd->type << ((sizeof(unsigned int) - 1) * 8));
}

/* internal function to compare fds */
static int fd_cmp(const void *vone, const void *vtwo) {
    const struct fd *one = vone, 
                    *two = vtwo;

    if (one->type == two->type) {
        if (one->fileno == two->fileno) {
            return 0;
        } else if (one->fileno < two->fileno) {
            return -1;
        } else {
            return 1;
        }
    } else if (one->type < two->type) {
        return -1;
    } else {
        return 1;
    }
}

struct fdset {
    struct chash *typehash;               /* hashtable of type entries */
    struct fd **fd;                       /* array of open fds */
    unsigned int fds;                     /* number of open fds */
    unsigned int fdsize;                  /* size of fd array */
    struct fd **key;                      /* keys in hash table */
    unsigned int keys;                    /* number of keys in hash table */
    unsigned int keysize;                 /* size of key array */
    struct chash *fdhash;                 /* hashtable for fast lookup of 
                                           * fds */
    int umask;                            /* permission mask for opening new 
                                           * files */
    unsigned int clock_pos;               /* last position in clock 
                                           * algorithm */
    unsigned int lru_default;             /* number of second chances in clock 
                                           * algorithm */
    unsigned int limit;                   /* maximum number of fds to open */
#ifdef MT_ZET
    pthread_mutex_t mutex;
#endif /* MT_ZET */
};

static struct fd * new_fd() {
    struct fd * fd = NULL;

    if (!(fd = malloc(sizeof(*fd))))
        return NULL;
    fd->lru_count = 0;
    fd->fd = -1;
    fd->next = NULL;
    return fd;
}

static struct fd **make_fd_array(unsigned int size) {
    struct fd **fd = NULL;
    unsigned int i;
    if (!(fd = calloc(size, sizeof(*fd))))
        return NULL;

    for (i = 0; i < size; i++) {
        if (!(fd[i] = new_fd())) {
            for (i--; i >= 0; i--)
                free(fd[i]);
            free(fd);
            return NULL;
        }
    }
    return fd;
}

static int expand_fd_array(struct fd *** array_p, unsigned int * size_p) {
    unsigned int new_size = *size_p * 2 + 1;
    struct fd ** new_array = realloc(*array_p, sizeof(*new_array) * new_size);
    unsigned int i;
    
    if (new_array == NULL) {
        return -ENOMEM;
    }
    for (i = *size_p; i < new_size; i++) {
        if (!(new_array[i] = new_fd())) {
            unsigned int j;

            for (j = *size_p; j < i; j++) {
                free(new_array[j]);
            }
            *array_p = new_array;
            return -ENOMEM;
        }
    }
    *array_p = new_array;
    *size_p = new_size;
    return 0;
}

struct fdset *fdset_new(int umask, unsigned int sizehint) {
    struct fdset *set;

    if ((set = malloc(sizeof(*set))) 
      && (set->fdhash = chash_ptr_new(1, 1.0, fd_hash, fd_cmp))
      && (set->typehash = chash_luint_new(1, 1.0))) {
        set->umask = umask;
        set->fds = 0;
        set->keys = 0;
        set->lru_default = 3;  /* FIXME: get as param */
        set->limit = UINT_MAX;   /* no limit */
        set->clock_pos = 0;
 
        if (sizehint) {
            if (!(set->fd = make_fd_array(sizehint))
              || !(set->key = make_fd_array(sizehint))) {
                fdset_delete(set);
                return NULL;
            }
            set->fdsize = sizehint;
            set->keysize = sizehint;
        } else {
            set->fdsize = 0;
            set->fd = NULL;
            set->keysize = 0;
            set->key = NULL;
        }
#ifdef MT_ZET
        pthread_mutex_init(&set->mutex, NULL);
#endif /* MT_ZET */
    }

    return set;
}

static void specific_delete(void *userdata, unsigned long int key, 
  void **data) {
    struct specific *sp = *data;

    free((void *) sp->filename);
    free(sp);
}

static void type_delete(void *userdata, unsigned long int key, void **data) {
    struct type *type = *data;
    chash_luint_ptr_foreach(type->specific, NULL, specific_delete);
    chash_delete(type->specific);
    free((void *) type->template);
    free(type);
}

void fdset_delete(struct fdset *set) {
    unsigned int i;

    chash_luint_ptr_foreach(set->typehash, NULL, type_delete);
    chash_delete(set->typehash);
    chash_delete(set->fdhash);

    for (i = 0; i < set->fds; i++) {
        close(set->fd[i]->fd);
        set->fd[i]->fd = -1;
        free(set->fd[i]);
    }

    for (i = 0; i < set->keysize; i++) {
        if (set->key[i]) {
            free(set->key[i]);
        }
    }

    for (i = set->fds; i < set->fdsize; i++) {
        if (set->fd[i]) {
            free(set->fd[i]);
        }
    }

#ifdef MT_ZET
    pthread_mutex_destroy(&set->mutex);
#endif /* MT_ZET */

    free(set->fd);
    free(set->key);
    free(set);
}

/* convert a string into a printf specifying by escaping potential format
 * specifiers and adding our own */
static int maketemplate(char *dst, unsigned int dstlen, const char *src, 
  unsigned int srclen) {
    char *dstend = dst + dstlen;
    const char *srcend = src + srclen;

    while ((dst < dstend) && (src < srcend) && *src) {
        switch (*src) {
        case '%':
            *dst++ = *src++;
            if (dst < dstend) {
                *dst++ = '%';
            } else {
                return 0;
            }
            break;

        default:
            *dst++ = *src++;
            break;

        case '\0':
            return 0;
            break;
        };
    }

    if (dst < dstend) {
        if (dstend - dst < 4) {
            return 0;
        }
        *dst++ = '.';
        *dst++ = '%';
        *dst++ = 'u';
        *dst = '\0';
        return 1;
    }

    return 0;
}

/* undo maketemplate conversion, returning length of created string */
static unsigned int untemplate(char *dst, unsigned int dstlen, 
  const char *src, unsigned int srclen) {
    char *dstend = dst + dstlen,
         *dstcpy = dst;
    const char *srcend = src + srclen;

    while ((dst < dstend) && (src < srcend) && *src) {
        switch (*src) {
        case '%':
            *dst++ = *src++;
            if (*src == '%') {
                /* escaped it, skip */
                src++;
            } else if (*src == 'u') {
                /* end of string, end dst on second previous char */
                dst -= 2;
                assert(*dst == '.');
                *dst = '\0';
                return dst - dstcpy;
            } else {
                /* don't know what happened */
                assert(0);
            }
            break;

        default:
            *dst++ = *src++;
            break;

        case '\0':
            /* shouldn't happen */
            assert(0);
            break;
        };
    }

    /* shouldn't happen */
    assert(0);
    return 0;
}

int fdset_set_type_name(struct fdset *set, unsigned int typeno, 
  const char *name, unsigned int namelen, int write) {
    char buf[FILENAME_MAX * 2 + 1];
    void **find;
    struct type *type = NULL;

    if (maketemplate(buf, FILENAME_MAX * 2 + 1, name, namelen)) {
        if ((chash_luint_ptr_find(set->typehash, typeno, &find) == CHASH_OK)
            /* trying to change the template for an existing type, don't do
             * this as it may have unintended consequences for files already
             * open. */
          || !((type = malloc(sizeof(*type)))
            && (type->specific = chash_luint_new(1, 1.0))
            && (chash_luint_ptr_insert(set->typehash, typeno, type) 
              == CHASH_OK))) {
            /* couldn't insert new entry */

            if (type) {
                if (type->specific) {
                    chash_delete(type->specific);
                }
                free(type);
            }
            return -EINVAL;
        }

        if ((type->template = str_dup(buf))) {
            type->write = !!write;
            return FDSET_OK;
        } else {
            void *remove;

            chash_luint_ptr_remove(set->typehash, typeno, &remove);
            chash_delete(type->specific);
            free(type);
            return -ENOMEM;
        }
    }

    return -EINVAL;
}

int fdset_create_new_type(struct fdset *set, const char *basename,
  const char *suffix, int write, unsigned int *typeno) {
    char name_buf[FILENAME_MAX + 1];
    int ret;

    ret = snprintf((char *) name_buf, FILENAME_MAX, "%s.%s", basename, suffix);
    if (ret >= FILENAME_MAX)
        return -EINVAL;
    else if (ret < 0)
        return -errno;

    *typeno = fdset_types(set);
    return fdset_set_type_name(set, *typeno, name_buf, ret, write);
}

int fdset_set_fd_name(struct fdset *set, unsigned int typeno, 
  unsigned int fileno, const char *name, unsigned int namelen, 
  int write) {
    struct type *type;
    void **find;
    struct specific *sp = NULL;

    if ((chash_luint_ptr_find(set->typehash, typeno, &find) == CHASH_OK)
      && (type = *find)) {
        if (chash_luint_ptr_find(type->specific, fileno, &find) == CHASH_OK) {
            /* trying to set name thats already been set */
            sp = *find;
            if ((namelen == str_len(sp->filename)) 
              && !str_ncmp(name, sp->filename, namelen)) {
                /* setting it to the same thing it was, allow it (rebuild
                 * depends on this behaviour) */
                return FDSET_OK;
            } else {
                /* setting it to something different, return error */
                return -EEXIST;
            }
        }

        if (!((sp = malloc(sizeof(*sp)))
            && (sp->filename 
              = str_ndup(name, namelen))
            && (chash_luint_ptr_insert(type->specific, fileno, sp) 
              == CHASH_OK))) {
            /* couldn't insert new entry */

            if (sp) {
                if (sp->filename) {
                    free((void *) sp->filename);
                }
                free(sp);
            }
            return -ENOMEM;
        }

        sp->write = !!write;
        return FDSET_OK;
    }

    return -ENOENT;
}

static int fdset_ensure_fd_array_space(struct fdset *set) {
    if (set->fds >= set->fdsize) {
        int retval = expand_fd_array(&set->fd, &set->fdsize);
        if (retval < 0)
            return retval;
        assert(set->fdsize > set->fds);
    }

    if (set->keys >= set->keysize) {
        int retval = expand_fd_array(&set->key, &set->keysize);
        if (retval < 0)
            return retval;
        assert(set->keysize > set->keys);
    }
    return 0;
}

/* Insert a fd into our fdhash. */
static int fdset_insert_fd(struct fdset *set, unsigned int typeno,
  unsigned int fileno, int fd) {
    int found;
    void **find;
    int retval;
    if ( (retval = fdset_ensure_fd_array_space(set)) < 0) {
        return retval;
    }
    set->fd[set->fds]->fd = fd;
    set->fd[set->fds]->type = typeno;
    set->fd[set->fds]->fileno = fileno;
    set->fd[set->fds]->lru_count = UINT_MAX;
    set->key[set->keys]->type = typeno;
    set->key[set->keys]->fileno = fileno;
    set->key[set->keys]->lru_count = UINT_MAX;
    if (chash_ptr_ptr_find_insert(set->fdhash, set->key[set->keys],
        &find, set->fd[set->fds], &found) != CHASH_OK) {

        return -errno;
    }
    if (found) {
        struct fd * found = *find;
        set->fd[set->fds]->next = found;
        *find = set->fd[set->fds];
    } else {
        set->keys++;
    }
    set->fds++;
    return FDSET_OK;
}

int fdset_create(struct fdset *set, unsigned int typeno, unsigned int fileno) {
    char filename[FILENAME_MAX + 1];
    int retval;
    unsigned int namelen = 0;
    int write = 0;
    int fd;
    int flags;

    if ((retval = fdset_name(set, typeno, fileno, filename, FILENAME_MAX, 
        &namelen, &write)) != FDSET_OK) {

        return retval;
    }
    flags = (write * O_RDWR) | (!write * O_RDONLY) | O_CREAT | O_EXCL 
      | O_BINARY; 

    /* make sure we're not over the limit */ 
    if (set->fds >= set->limit) {
        if ((retval = fdset_close(set)) != FDSET_OK) {
            return retval;
        }
    }

    /* ok, try to open this file non-create. */
    if ((fd = open((const char *) filename, flags, set->umask)) < 0) {
        if (errno == EMFILE || errno == ENFILE) {
            errno = 0;
            if ((retval = fdset_close(set)) != FDSET_OK) {
                return retval;
            }
            if ((fd = open((const char *) filename, flags, set->umask)) < 0) {
                return -errno;
            }
        } else {
            return -errno;
        }
    }
    assert(fd >= 0);

    /* we've got our fd; put it into our hashtable.
       We assume that if the file was just created, it can't be in
       the hashtable. */
    if ((retval = fdset_insert_fd(set, typeno, fileno, fd)) != FDSET_OK) {
        close(fd);
        return retval;
    }

    return fd;
}

int fdset_debug_create(struct fdset *set, unsigned int typeno, 
  unsigned int fileno, const char * src_file, int src_line) {
    int retval;
    static int count = 0;
    retval = fdset_create(set, typeno, fileno);
    fprintf(stderr, "<create %d: fd %d, type %d, fileno %d (%s: %d)>\n", 
      count++, retval, typeno, fileno, src_file, src_line);
    return retval;
}

int fdset_create_seek(struct fdset *set, unsigned int typeno, 
  unsigned int fileno, off_t offset) {
    int fd;
    if ((fd = fdset_create(set, typeno, fileno)) < 0) {
        return fd;
    }

    if (lseek(fd, offset, SEEK_SET) == (off_t) -1) {
        fdset_unpin(set, typeno, fileno, fd);
        return -errno;
    }
    return fd;
}

int fdset_debug_create_seek(struct fdset *set, unsigned int typeno, 
  unsigned int fileno, off_t offset, const char * src_file,
  int src_line) {
    int retval;
    static int count = 0;
    retval = fdset_create_seek(set, typeno, fileno, offset);
    fprintf(stderr, "<create %d: fd %d, type %d, fileno %d, "
      "truncated offset %lu (%s: %d)>\n", count++, retval, typeno, fileno, 
      (unsigned long int) offset, src_file, src_line);
    return retval;
}

static int fdset_pin_locked(struct fdset *set, unsigned int typeno, 
  unsigned int fileno, off_t offset, int whence) {
    const char *filename;
    char buf[FILENAME_MAX + 1];
    struct fd key,
              *found;
    void **find;
    struct type *type;
    struct specific *sp;
    int write;
    int retval;
    int fnd;

    /* first, search open fd hashtable for match */
    key.type = typeno;
    key.fileno = fileno;
    if (chash_ptr_ptr_find(set->fdhash, &key, &find) == CHASH_OK) {
        found = *find;
        while (found != NULL && found->lru_count == UINT_MAX) {
            found = found->next;
        }
        if (found != NULL) {
            found->lru_count = UINT_MAX;
            assert(found->fd > 0);

            if (((whence == SEEK_CUR) && (offset == 0)) 
              || (lseek(found->fd, offset, whence) != (off_t) -1)) {
                return found->fd;
            } else {
                return -errno;
            }
        }
    }

    /* otherwise, try to open a new fd */
    if ((retval = fdset_ensure_fd_array_space(set)) < 0) {
        return retval;
    }

    /* lookup filename in hash tables */
    if (chash_luint_ptr_find(set->typehash, typeno, &find) == CHASH_OK) {
        type = *find;
        find = NULL;
        if (((chash_luint_ptr_find(type->specific, fileno, &find) == CHASH_OK)
            && ((sp = *find), (write = sp->write), (filename = sp->filename)))
          || ((snprintf((char *) buf, FILENAME_MAX, 
            (const char *) type->template, fileno) 
              <= FILENAME_MAX) 
            && ((write = type->write), (filename = buf)))) {
            void **find = NULL;
            int flags = (write * O_RDWR) | (!write * O_RDONLY) | O_BINARY;

            if ((set->fds < set->limit) 
              && ((set->fd[set->fds]->fd 
                = open((const char *) filename, flags, set->umask)) != -1)
              && ((set->fd[set->fds]->type = typeno), 
                (set->fd[set->fds]->fileno = fileno), 
                (set->fd[set->fds]->lru_count = UINT_MAX))
              && ((set->key[set->keys]->type = typeno), 
                (set->key[set->keys]->fileno = fileno), 
                (set->key[set->keys]->lru_count = UINT_MAX))
              /* insert into fds hashtable */
              && (chash_ptr_ptr_find_insert(set->fdhash, set->key[set->keys], 
                  &find, set->fd[set->fds], &fnd) == CHASH_OK)) {

                if (fnd) {
                    struct fd * found = *find;
                    set->fd[set->fds]->next = found;
                    *find = set->fd[set->fds];
                } else {
                    set->keys++;
                }
                /* this is a new file, so SEEK_CUR is equivalent
                   to SEEK_SET.  We make this distinction to allow
                   indexing of non-seekable file descriptors, e.g.
                   <(gunzip foo.txt.gz); this is a bit of a hack. */
                if (((whence == SEEK_CUR || whence == SEEK_SET) 
                      && (offset == 0)) 
                  || (lseek(set->fd[set->fds]->fd, offset, whence) 
                    != (off_t) -1)) {

                    return set->fd[set->fds++]->fd;
                } else {
                    /* failed to seek, leave fd open but mark it as 
                     * replaceable */
                    set->fd[set->fds++]->lru_count = 0;
                    return -errno;
                }
            } else if (set->fd[set->fds]->fd >= 0) {
                close(set->fd[set->fds]->fd);
                set->fd[set->fds]->fd = -1;
                return -ENOMEM;
            } else if (((set->fds >= set->limit) 
                || (errno == EMFILE) || (errno == ENFILE))
              && (fdset_close(set) == FDSET_OK)) {
                /* having closed a file, try again */
                void **find = NULL;

                errno = 0;
                if (((set->fd[set->fds]->fd 
                    = open((const char *) filename, flags, set->umask)) != -1)
                  && ((set->fd[set->fds]->type = typeno), 
                    (set->fd[set->fds]->fileno = fileno), 
                    (set->fd[set->fds]->lru_count = UINT_MAX))
                  && ((set->key[set->keys]->type = typeno), 
                      (set->key[set->keys]->fileno = fileno), 
                      (set->key[set->keys]->lru_count = UINT_MAX))
                  /* insert into fds hashtable */
                  && (chash_ptr_ptr_find_insert(set->fdhash, set->fd[set->fds],
                      &find, set->fd[set->fds], &fnd) == CHASH_OK)) {

                    if (fnd) {
                        struct fd * found = *find;
                        set->fd[set->fds]->next = found;
                        *find = set->fd[set->fds];
                    } else {
                        set->keys++;
                    }
                    if (((whence == SEEK_CUR) && (offset == 0)) 
                      || (lseek(set->fd[set->fds]->fd, offset, whence) 
                          != (off_t) -1)) {

                        return set->fd[set->fds++]->fd;
                    } else {
                        /* failed to seek, leave fd open but mark it as 
                         * replaceable */
                        set->fd[set->fds++]->lru_count = 0;
                        return -errno;
                    }
                } else {
                    /* couldn't open file */
                    return -errno;
                }
            } else {
                /* open failed */
                assert(set->fd[set->fds]->fd == -1);
                assert(errno);
                return -errno;
            }
        } else {
            return -ENOSPC;  /* filename was too long */
        }
    }

    return -EINVAL;
}

int fdset_pin(struct fdset *set, unsigned int typeno, unsigned int fileno,
  off_t offset, int whence) {
    int retval = 0;
#ifdef MT_ZET
    pthread_mutex_lock(&set->mutex);
#endif /* MT_ZET */
    retval = fdset_pin_locked(set, typeno, fileno, offset, whence);
#ifdef MT_ZET
    pthread_mutex_unlock(&set->mutex);
#endif /* MT_ZET */
    return retval;
}

int fdset_debug_pin(struct fdset *set, unsigned int typeno, 
  unsigned int fileno, off_t offset, int whence, 
  const char * src_file, int src_line) {
    static int count = 0;
    int retval = 0;
    retval = fdset_pin(set, typeno, fileno, offset, whence);
    fprintf(stderr, "<pin %d: fd %d, type %d, fileno %d (%s: %d)>\n", count++,
      retval, typeno, fileno, src_file, src_line);
    return retval;
}

int fdset_name(struct fdset *set, unsigned int typeno, unsigned int fileno, 
  char *buf, unsigned int buflen, unsigned int *len, int *write) {
    void **find;
    struct type *type;
    struct specific *sp;

    /* lookup name in hash tables */
    if (chash_luint_ptr_find(set->typehash, typeno, &find) == CHASH_OK) {
        type = *find;
        if ((chash_luint_ptr_find(type->specific, fileno, &find) == CHASH_OK)
            && (sp = *find)) {
            *len = str_lcpy(buf, sp->filename, buflen);
            *write = sp->write;
        } else {
            *len = snprintf((char *) buf, buflen, 
                (const char *) type->template, fileno);
            *write = type->write;
        }
        return FDSET_OK;
    } else {
        return -ENOENT;
    }
}

int fdset_type_name(struct fdset *set, unsigned int typeno, 
  char *buf, unsigned int buflen, unsigned int *len, int *write) {
    void **find;
    struct type *type;

    /* lookup name in hash tables */
    if (chash_luint_ptr_find(set->typehash, typeno, &find) == CHASH_OK) {
        type = *find;
        *write = type->write;
        *len 
          = untemplate(buf, buflen, type->template, str_len(type->template));
        return FDSET_OK;
    } else {
        return -ENOENT;
    }
}

static int fdset_unpin_locked(struct fdset *set, unsigned int typeno, 
  unsigned int fileno, int fd) {
    struct fd key,
              *found;
    void **find;

    /* search open fd hashtable for match */
    key.type = typeno;
    key.fileno = fileno;
    if (chash_ptr_ptr_find(set->fdhash, &key, &find) == CHASH_OK) {
        found = *find;
        while (found != NULL && found->fd != fd)
            found = found->next;
        if (found == NULL) {
            return -ENOENT;
        }
        assert(found->lru_count == UINT_MAX);
        found->lru_count = set->lru_default;
        return FDSET_OK;
    } else {
        return -ENOENT;
    }
}

int fdset_unpin(struct fdset *set, unsigned int typeno, unsigned int fileno, 
  int fd) {
    int retval = 0;
#ifdef MT_ZET
    pthread_mutex_lock(&set->mutex);
#endif /* MT_ZET */
    retval = fdset_unpin_locked(set, typeno, fileno, fd);
#ifdef MT_ZET
    pthread_mutex_unlock(&set->mutex);
#endif /* MT_ZET */
    return retval;
}

int fdset_debug_unpin(struct fdset *set, unsigned int typeno, 
  unsigned int fileno, int fd, const char * src_file, int src_line) {
    static int count = 0;
    fprintf(stderr, "<unpin %d: fd %d, type %d, fileno %d (%s: %d)>\n", 
      count++, fd, typeno, fileno, src_file, src_line);
    return fdset_unpin(set, typeno, fileno, fd);
}

static int fdset_fd_close(struct fdset *set, int index) {
    void **find;
    struct fd *found;
    struct fd *prev = NULL;
    int ret;

    /* XXX: for some reason (?) this close call can take an looong time (10s or
     * so) on mac OS X */
    if (close(set->fd[index]->fd) != 0) {
        assert(!CRASH);
        assert(errno);
        return -errno;
    }
    set->fd[index]->fd = -1;

    /* remove from fdhash */
    ret = chash_ptr_ptr_find(set->fdhash, set->fd[index], &find);
    assert(ret == CHASH_OK);
    found = *find;
    while (found != set->fd[index]) {
        prev = found;
        found = found->next;
        assert(found != NULL);
    }
    if (prev == NULL) {
        if (found->next) {
            *find = found->next; 
        } else {
            void *val;

            /* have removed last from list */
            ret = chash_ptr_ptr_remove(set->fdhash, set->fd[index], &val);
            assert(ret == CHASH_OK);
        }
    } else {
        prev->next = found->next;
    }
    found->next = NULL;

    /* move it to the end of the array of fds */
    found = set->fd[index];
    set->fds--;
    memmove(&set->fd[index], &set->fd[index + 1], 
      sizeof(found) * (set->fds - index));
    set->fd[set->fds] = found;
    return FDSET_OK;
}

int fdset_close(struct fdset *set) {
    int changed;
    unsigned int i;

    /* ensure that clock pos is valid */
    while (set->clock_pos > set->fds) {
        set->clock_pos -= set->fds;
    }

    /* clock approximation of LRU algorithm */
    do {
        changed = 0;

        for (i = set->clock_pos; i < set->fds; i++) {
            if (!set->fd[i]->lru_count) {
                set->clock_pos = i;
                return fdset_fd_close(set, i);
            } else {
                /* decrements count by one if count is not set to UINT_MAX
                 * (UINT_MAX indicates that it is pinned and can't be 
                 * closed) */
                if (set->fd[i]->lru_count != UINT_MAX) {
                    set->fd[i]->lru_count--;
                    changed = 1;
                }
            }
        }

        for (i = 0; i < set->clock_pos; i++) {
            if (!set->fd[i]->lru_count) {
                set->clock_pos = i;
                return fdset_fd_close(set, i);
            } else {
                /* decrements count by one if count is not set to UINT_MAX
                 * (UINT_MAX indicates that it is pinned and can't be 
                 * closed) */
                if (set->fd[i]->lru_count != UINT_MAX) {
                    set->fd[i]->lru_count--;
                    changed = 1;
                }
            }
        }
    } while (changed);

    /* all available fds are pinned */
    assert(fdset_opened(set) == fdset_pinned(set));
    return -ENOENT;
}

unsigned int fdset_opened(struct fdset *set) {
    return set->fds;
}

unsigned int fdset_pinned(struct fdset *set) {
    unsigned int i,
                 count = 0;

    for (i = 0; i < set->fds; i++) {
        if (set->fd[i]->lru_count == UINT_MAX) {
            count++;
        }
    }

    return count;
}

int fdset_isset(struct fdset *set, unsigned int typeno, unsigned int fileno, 
  int *isset) {
    void **find;
    struct type *type;
    struct specific *sp;

    /* lookup name in hash tables */
    if (chash_luint_ptr_find(set->typehash, typeno, &find) == CHASH_OK) {
        type = *find;
        if ((chash_luint_ptr_find(type->specific, fileno, &find) == CHASH_OK)
            && (sp = *find)) {
            *isset = 1;
        } else {
            *isset = 0;
        }
        return FDSET_OK;
    }

    return -EINVAL;
}

unsigned int fdset_types(struct fdset *set) {
    return chash_size(set->typehash);
}

int fdset_close_file(struct fdset *set, unsigned int typeno, 
  unsigned int fileno) {
    struct fd key,
              *found;
    void **find;
    unsigned int i;

    /* first, search open fd hashtable for match */
    key.type = typeno;
    key.fileno = fileno;
    /* close all of the open fd's for this file */
    if (chash_ptr_ptr_find(set->fdhash, &key, &find) == CHASH_OK) {
        found = *find;
        while (found) {
            if (found->lru_count != UINT_MAX) {
                /* XXX: linear search through fd array to determine position */
                for (i = 0; (i < set->fds) && (set->fd[i] != found); i++) ;
                assert((i < set->fds) && (set->fd[i] == found));

                found = found->next;
                fdset_fd_close(set, i);
            } else {
                fprintf(stderr, "Still pinned: fd: %d, lru_count: %u, "
                  "type %u, fileno %u\n", found->fd, found->lru_count,
                  found->type, found->fileno);
                /* can't unlink, we still have open references to it */
                return -EBUSY;
            }
        }
    }
    return FDSET_OK;
}

int fdset_unlink(struct fdset *set, unsigned int typeno, unsigned int fileno) {
    char buf[FILENAME_MAX + 1];
    void **find;
    struct specific *sp;
    struct type *type;
    unsigned int len;
    int ret;

    ret = fdset_close_file(set, typeno, fileno);
    if (ret < 0)
        return ret;

    /* lookup name in hash tables */
    if (chash_luint_ptr_find(set->typehash, typeno, &find) == CHASH_OK) {
        type = *find;
        if ((chash_luint_ptr_find(type->specific, fileno, &find) == CHASH_OK)
            && (sp = *find)) {

            if (sp->write) {
                if (unlink(sp->filename) == 0) {
                    return FDSET_OK;
                } else {
                    return -errno;
                }
            } else {
                return -EBADF;
            }
        } else if (type->write) {
            len = snprintf((char *) buf, FILENAME_MAX, 
                (const char *) type->template, fileno);
            assert(len <= FILENAME_MAX);

            if (unlink(buf) == 0) {
                return FDSET_OK;
            } else {
                return -errno;
            }
        } else {
            return -EBADF;
        }
    } else {
        return -ENOENT;
    }
}

/* debugging function to return the name of a repository.  DONT USE THIS FOR
 * ANYTHING SERIOUS (uses static buffer) */
const char *fdset_debug_name(struct fdset *set, int typeno, 
  unsigned int fileno) {
    void **find;
    struct type *type;
    struct specific *sp;
    static char buf[FILENAME_MAX + 1];

    /* lookup name in hash tables */
    if (chash_luint_ptr_find(set->typehash, typeno, &find) == CHASH_OK) {
        type = *find;
        if ((chash_luint_ptr_find(type->specific, fileno, &find) == CHASH_OK)
            && (sp = *find)) {
            str_lcpy(buf, sp->filename, FILENAME_MAX);
        } else {
            snprintf((char *) buf, FILENAME_MAX, 
                (const char *) type->template, fileno);
        }
        return buf;
    } else {
        return NULL;
    }
}

