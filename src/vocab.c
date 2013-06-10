/* vocab.c implements compression and decompression routines for
 * vocabulary entries
 *
 * written nml 2003-02-10
 *
 */

#include "firstinclude.h"

#include "vocab.h"

#include "bit.h"
#include "vec.h"
#include "error.h"
#include "zvalgrind.h"

#include <assert.h>
#include <string.h>
#include <limits.h>

unsigned int vocab_len(struct vocab_vector *vocab) {
    unsigned int len = 1;   /* 1 for combination of attr, type and location */

    if (vocab->attr & VOCAB_ATTRIBUTES_PERLIST) {
        len += vec_vbyte_len(vocab->attribute);
    }

    len += vec_vbyte_len(vocab->header.doc.docs) 
      + vec_vbyte_len(vocab->header.doc.occurs)
      + vec_vbyte_len(vocab->header.doc.last)
      + vec_vbyte_len(vocab->size);

    switch (vocab->type) {
    case VOCAB_VTYPE_DOC:
    case VOCAB_VTYPE_DOCWP:
        break;

    case VOCAB_VTYPE_IMPACT:
        break;

    default: return 0;
    }

    switch (vocab->location) {
    case VOCAB_LOCATION_VOCAB:
        len += vocab->size;
        break;
       
    case VOCAB_LOCATION_FILE:
        len += vec_vbyte_len(vocab->loc.file.fileno) 
          + vec_vbyte_len(vocab->loc.file.offset) 
          + vec_vbyte_len(vocab->loc.file.capacity);
        break;

    default: return 0;
    }

    return len;
}

enum vocab_ret vocab_decode(struct vocab_vector *vocab, struct vec *v) {
    unsigned long int tmp;
    unsigned int bytes = 0,
                 ret;
    unsigned char byte;

    VALGRIND_CHECK_WRITABLE(vocab, sizeof(*vocab));

    /* if debugging, clear the vocab vector first */
    assert((memset(vocab, 0, sizeof(*vocab)), 1));

    /* check that memory can be accessed, then mark vocab entry as 
     * uninitialised */
    VALGRIND_MAKE_WRITABLE(vocab, sizeof(*vocab));
    VALGRIND_CHECK_READABLE(v->pos, VEC_LEN(v));

    /* first, get first byte which contains attribute, location and type
     * indications */
    if (v->pos < v->end) {
        vec_byte_read(v, (char *) &byte, 1);
        bytes++;
        vocab->attr = byte & BIT_LMASK(2);
        byte >>= 2;
        vocab->location = byte & BIT_LMASK(2);
        byte >>= 2;
        vocab->type = byte;

        if (vocab->attr & VOCAB_ATTRIBUTES_PERLIST) {
            if ((ret = vec_vbyte_read(v, &tmp))) {
                vocab->attribute = (unsigned int) tmp;
                bytes += ret;
            } else {
                if (((unsigned int) VEC_LEN(v)) <= vec_vbyte_len(UINT_MAX)) {
                    v->pos -= bytes;
                    return VOCAB_ENOSPC;
                } else {
                    v->pos -= bytes;
                    return VOCAB_EOVERFLOW;
                }
            }
        }

        /* get common header entries */
        if ((ret = vec_vbyte_read(v, &vocab->size))
          && (bytes += ret)
          && (ret = vec_vbyte_read(v, &vocab->header.doc.docs))
          && (bytes += ret)
          && (ret = vec_vbyte_read(v, &vocab->header.doc.occurs))
          && (bytes += ret)
          && (ret = vec_vbyte_read(v, &vocab->header.doc.last))
          && (bytes += ret)) {
            /* succeeded, do nothing */
        } else {
            if (((unsigned int) VEC_LEN(v)) <= vec_vbyte_len(UINT_MAX)) {
                v->pos -= bytes;
                return VOCAB_ENOSPC;
            } else {
                v->pos -= bytes;
                return VOCAB_EOVERFLOW;
            }
        }

        /* get specific header entries */
        switch (vocab->type) {
        case VOCAB_VTYPE_DOC:
        case VOCAB_VTYPE_DOCWP:
            /* ok, so i cheated a little and just read the common, uh, not
             * common ones above (they're not common because future vector 
             * types might not have them)... */
            break;

        case VOCAB_VTYPE_IMPACT:
            break;

        default: 
            v->pos -= bytes; 
            return VOCAB_EINVAL;
        }

        /* get location */
        switch (vocab->location) {
        case VOCAB_LOCATION_VOCAB:
            if (((unsigned int) VEC_LEN(v)) >= vocab->size) {
                /* note that we increment vector over in-vocab vector so that
                 * successive _decode calls will work as planned */
                vocab->loc.vocab.vec = v->pos;
                v->pos += vocab->size;
                bytes += vocab->size;
            } else {
                v->pos -= bytes; 
                return VOCAB_ENOSPC;
            }
            break;
       
        case VOCAB_LOCATION_FILE:
            if ((ret = vec_vbyte_read(v, &tmp))
              && ((vocab->loc.file.fileno = tmp), (bytes += ret))
              && (ret = vec_vbyte_read(v, &vocab->loc.file.offset))
              && (bytes += ret)
              && (ret = vec_vbyte_read(v, &tmp))
              && ((vocab->loc.file.capacity = tmp), (bytes += ret))) {
                /* succeeded, do nothing */
            } else {
                if (((unsigned int) VEC_LEN(v)) <= vec_vbyte_len(UINT_MAX)) {
                    v->pos -= bytes;
                    return VOCAB_ENOSPC;
                } else {
                    v->pos -= bytes;
                    return VOCAB_EOVERFLOW;
                }
            }
            break;

        default: 
            v->pos -= bytes;
            return VOCAB_EINVAL;
        }

        return VOCAB_OK;
    } else {
        return VOCAB_END;
    }
}

enum vocab_ret vocab_encode(struct vocab_vector *vocab, struct vec *v) {
    unsigned int bytes = 0,
                 ret;
    unsigned char byte;

    /* first, write byte which contains attribute, location and type
     * indications */
    if (v->pos < v->end) {
        bytes++;
        byte = vocab->type << 4 | vocab->location << 2 | vocab->attr;
        vec_byte_write(v, (char *) &byte, 1);

        if (vocab->attr & VOCAB_ATTRIBUTES_PERLIST) {
            if ((ret = vec_vbyte_write(v, vocab->attribute))) {
                bytes += ret;
            } else {
                v->pos -= bytes;
                return VOCAB_ENOSPC;
            }
        }

        /* get common header entries */
        if ((ret = vec_vbyte_write(v, vocab->size))
          && (bytes += ret)
          && (ret = vec_vbyte_write(v, vocab->header.doc.docs))
          && (bytes += ret)
          && (ret = vec_vbyte_write(v, vocab->header.doc.occurs))
          && (bytes += ret)
          && (ret = vec_vbyte_write(v, vocab->header.doc.last))
          && (bytes += ret)) {
            /* succeeded, do nothing */
        } else {
            v->pos -= bytes;
            return VOCAB_ENOSPC;
        }

        /* get specific header entries */
        switch (vocab->type) {
        case VOCAB_VTYPE_DOC:
        case VOCAB_VTYPE_DOCWP:
            /* ok, so i cheated a little and just read the common, uh, not
             * common ones above (they're not common because future vector 
             * types might not have them)... */
            break;

        case VOCAB_VTYPE_IMPACT:
            break;

        default: 
            v->pos -= bytes; 
            return VOCAB_EINVAL;
        }

        /* get location */
        switch (vocab->location) {
        case VOCAB_LOCATION_VOCAB:
            if (((unsigned int) VEC_LEN(v)) >= vocab->size) {
                /* note that we increment vector over in-vocab vector so that
                 * successive _encode calls will work as planned */
                v->pos += vocab->size;
                bytes += vocab->size;
            } else {
                v->pos -= bytes; 
                return VOCAB_ENOSPC;
            }
            break;
       
        case VOCAB_LOCATION_FILE:
            if ((ret = vec_vbyte_write(v, vocab->loc.file.fileno))
              && (bytes += ret)
              && (ret = vec_vbyte_write(v, vocab->loc.file.offset))
              && (bytes += ret)
              && (ret = vec_vbyte_write(v, vocab->loc.file.capacity))
              && (bytes += ret)) {
                /* succeeded, do nothing */
            } else {
                v->pos -= bytes;
                return VOCAB_ENOSPC;
            }
            break;

        default: 
            v->pos -= bytes;
            return VOCAB_EINVAL;
        }

        return VOCAB_OK;
    } else {
        return VOCAB_ENOSPC;
    }
}

unsigned long int vocab_docs(struct vocab_vector *vocab) {
    switch (vocab->type) {
    case VOCAB_VTYPE_DOC:
        return vocab->header.doc.docs;
    case VOCAB_VTYPE_DOCWP:
        return vocab->header.docwp.docs;
    case VOCAB_VTYPE_IMPACT:
        return vocab->header.impact.docs;
    default:
        assert("shouldn't happen");
        return 0;
    }
}

unsigned long int vocab_occurs(struct vocab_vector *vocab) {
    switch (vocab->type) {
    case VOCAB_VTYPE_DOC:
        return vocab->header.doc.occurs;
    case VOCAB_VTYPE_DOCWP:
        return vocab->header.docwp.occurs;
    case VOCAB_VTYPE_IMPACT:
        return vocab->header.impact.occurs;
    default:
        assert("shouldn't happen");
        return 0;
    }
}

unsigned long int vocab_last(struct vocab_vector *vocab) {
    switch (vocab->type) {
    case VOCAB_VTYPE_DOC:
        return vocab->header.doc.last;
    case VOCAB_VTYPE_DOCWP:
        return vocab->header.docwp.last;
    case VOCAB_VTYPE_IMPACT:
        return vocab->header.impact.last;
    default:
        assert("shouldn't happen");
        return 0;
    }
}
