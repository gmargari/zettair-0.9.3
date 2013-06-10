/* gunzipfilter.c implements a stream filter module (see stream.h) that
 * decompresses files created by the gzip utility, as described in RFC 1952.
 * The zlib library is used to actually decompress the data, this module merely
 * parses the miscellaneous junk in the header and figures out which bits
 * constitute data and verifies that the data matches checksum and recorded
 * size. 
 *
 * As of 1.2.x the zlib library also performs this functionality.  It was a bit
 * of a tough choice as to whether to duplicate it, since 1.2.x is not
 * widespread yet, but in the end i decided to make this code as widely-usable
 * as possible and duplicate the functionality.
 *
 * A good extension to this bit of code would be to be able to decompress data
 * in zlib format, and raw deflate format (requires a bit of buggering around
 * with states).  Also nice would be to check header checksum and possibly to
 * provide access to the extra header data.
 *
 * written nml 2004-08-26
 *
 */

/* FIXME: there are periods in this state machine (search for FIXME, commented
 * out assert) where the input position is not correct */

#include "stream.h"

#include "crc.h"
#include "def.h"
#include "str.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

enum gunzipfilter_states {
    STATE_ERR = -1,

    STATE_CM,
    STATE_DECOMPRESS,
    STATE_DECOMPRESS_DECOMP,
    STATE_DECOMPRESS_DECOMP8,
    STATE_DECOMPRESS_POST8,
    STATE_END,
    STATE_END_FLUSH,
    STATE_END_CHECK,
    STATE_EXTRA,
    STATE_EXTRA_SECOND,
    STATE_EXTRA_VAR,
    STATE_FLG,
    STATE_HCRC,
    STATE_HCRC_SECOND,
    STATE_ID2,
    STATE_MTIME,
    STATE_MTIME_FOURTH,
    STATE_MTIME_SECOND,
    STATE_MTIME_THIRD,
    STATE_OS,
    STATE_START,
    STATE_STRING,
    STATE_XFL
};

/* flags that can be in a gzip header, straight from the RFC */
enum gunzipfilter_flags {
    FLAG_FTEXT = (1 << 0),
    FLAG_FHCRC = (1 << 1),
    FLAG_FEXTRA = (1 << 2),
    FLAG_FNAME = (1 << 3),
    FLAG_FCOMMENT = (1 << 4)
};

struct gunzipfilter {
    struct stream_filter filter;     /* filter object (must be first) */
    z_stream zstate;                 /* zlib state */
    unsigned int len;                /* length of various things */
    unsigned int bufsize;            /* size of allocated buffer */
    enum gunzipfilter_states state;  /* state of decompression */
    struct crc *crc;                 /* running checksum */
    unsigned char prebuf[8];         /* buffer for last 8 bytes */
    unsigned char flags;             /* remaining flags from gzip header 
                                      * (bitfield composed of items from 
                                      * enum gunzipfilter_flags) */
    char buf[1];                     /* allocated buffer (not 
                                      * necessarily of length one, via struct 
                                      * hack) */
};

static const char *gunzipfilter_id(struct stream_filter *filter) {
    return "gunzip";
}

static enum stream_ret gunzipfilter_filter(struct stream_filter *filter, 
  enum stream_flush flush) {
    struct gunzipfilter *state = (void *) filter;
    unsigned int tmp;

    assert(filter->idfn(filter) == gunzipfilter_id(filter));

    /* jump to correct state */
    switch (state->state) {
    case STATE_CM: goto cm_label;
    case STATE_DECOMPRESS: goto decompress_label;
    case STATE_DECOMPRESS_DECOMP: goto decompress_decomp_label;
    case STATE_DECOMPRESS_DECOMP8: goto decompress_decomp8_label;
    case STATE_DECOMPRESS_POST8: goto decompress_post8_label;
    case STATE_END: goto end_label;
    case STATE_END_FLUSH: goto end_flush_label;
    case STATE_END_CHECK: goto end_check_label;
    case STATE_EXTRA: goto extra_label;
    case STATE_EXTRA_SECOND: goto extra_second_label;
    case STATE_EXTRA_VAR: goto extra_var_label;
    case STATE_FLG: goto flg_label;
    case STATE_HCRC: goto hcrc_label;
    case STATE_HCRC_SECOND: goto hcrc_second_label;
    case STATE_ID2: goto id2_label;
    case STATE_MTIME: goto mtime_label;
    case STATE_MTIME_FOURTH: goto mtime_fourth_label;
    case STATE_MTIME_SECOND: goto mtime_second_label;
    case STATE_MTIME_THIRD: goto mtime_third_label;
    case STATE_OS: goto os_label;
    case STATE_START: goto start_label;
    case STATE_STRING: goto string_label;
    case STATE_XFL: goto xfl_label;
    default: assert(!CRASH); goto err_label;
    }

/* reading ID1 byte, which must be \037 */
start_label:
    if (filter->avail_in) {
        filter->avail_in--;
        if (*filter->next_in++ != '\037') {
            assert(!CRASH);
            goto err_label;
        }
    } else {
        state->state = STATE_START;
        return STREAM_INPUT;
    }

    goto id2_label;

/* reading ID2 byte, which must be \213 */
id2_label:
    if (filter->avail_in) {
        filter->avail_in--;
        if (*filter->next_in++ != '\213') {
            assert(!CRASH);
            goto err_label;
        }
    } else {
        state->state = STATE_ID2;
        return STREAM_INPUT;
    }

    goto cm_label;

/* reading compression method (CM) byte, which must be 8 */
cm_label:
    if (filter->avail_in) {
        filter->avail_in--;
        if (*filter->next_in++ != 8) {
            assert(!CRASH);
            goto err_label;
        }
    } else {
        state->state = STATE_CM;
        return STREAM_INPUT;
    }

    goto flg_label;

/* read flag byte */
flg_label:
    if (filter->avail_in) {
        filter->avail_in--;
        state->flags = *filter->next_in++;
    } else {
        state->state = STATE_FLG;
        return STREAM_INPUT;
    }

    goto mtime_label;

/* ignore the 4-byte, little-endian modification time */
mtime_label:
    if (filter->avail_in) {
        filter->avail_in--;
        filter->next_in++;
    } else {
        state->state = STATE_MTIME;
        return STREAM_INPUT;
    }
    goto mtime_second_label;
mtime_second_label:
    if (filter->avail_in) {
        filter->avail_in--;
        filter->next_in++;
    } else {
        state->state = STATE_MTIME_SECOND;
        return STREAM_INPUT;
    }
    goto mtime_third_label;
mtime_third_label:
    if (filter->avail_in) {
        filter->avail_in--;
        filter->next_in++;
    } else {
        state->state = STATE_MTIME_THIRD;
        return STREAM_INPUT;
    }
    goto mtime_fourth_label;
mtime_fourth_label:
    if (filter->avail_in) {
        filter->avail_in--;
        filter->next_in++;
    } else {
        state->state = STATE_MTIME_FOURTH;
        return STREAM_INPUT;
    }
    goto xfl_label;

/* ignore the extra flags byte */
xfl_label:
    if (filter->avail_in) {
        filter->avail_in--;
        filter->next_in++;
    } else {
        state->state = STATE_XFL;
        return STREAM_INPUT;
    }
    goto os_label;

/* ignore the operating system (OS) indication byte */
os_label:
    if (filter->avail_in) {
        filter->avail_in--;
        filter->next_in++;
    } else {
        state->state = STATE_OS;
        return STREAM_INPUT;
    }

    goto flag_switch_label;

/* figure out, based on flags, where to go next */
flag_switch_label:
    if (state->flags & FLAG_FHCRC) {
        state->flags &= ~FLAG_FHCRC;
        goto hcrc_label;
    } else if (state->flags & FLAG_FEXTRA) {
        state->flags &= ~FLAG_FEXTRA;
        goto extra_label;
    } else if (state->flags & FLAG_FNAME) {
        state->flags &= ~FLAG_FNAME;
        goto string_label;
    } else if (state->flags & FLAG_FCOMMENT) {
        state->flags &= ~FLAG_FCOMMENT;
        goto string_label;
    } else if (state->flags & FLAG_FTEXT) {
        state->flags &= ~FLAG_FTEXT;
        /* don't care about FTEXT */
        goto flag_switch_label;
    } else if (state->flags) {
        /* got a flag we don't understand */
        assert(!CRASH);
        goto err_label;
    } else {
        state->len = 0;
        goto decompress_label;
    }

/* ignore two bytes (XXX: implement header checksum at some stage)*/
hcrc_label:
    if (filter->avail_in) {
        filter->avail_in--;
        filter->next_in++;
    } else {
        state->state = STATE_HCRC;
        return STREAM_INPUT;
    }

    goto hcrc_second_label;

/* ignore second byte of header CRC */
hcrc_second_label:
    if (filter->avail_in) {
        filter->avail_in--;
        filter->next_in++;
    } else {
        state->state = STATE_HCRC_SECOND;
        return STREAM_INPUT;
    }

    goto flag_switch_label;

/* ignore string bytes, finished by '\0' */
string_label:
    while (filter->avail_in) {
        filter->avail_in--;
        if (*filter->next_in++ == '\0') {
            goto flag_switch_label;
        }
    }

    state->state = STATE_STRING;
    return STREAM_INPUT;

/* read extra field length */
extra_label:
    if (filter->avail_in) {
        filter->avail_in--;
        state->len = *filter->next_in++;
    } else {
        state->state = STATE_EXTRA;
        return STREAM_INPUT;
    }

    goto extra_second_label;

/* read second byte of extra field length */
extra_second_label:
    if (filter->avail_in) {
        filter->avail_in--;
        state->len += *filter->next_in++ << 8;
    } else {
        state->state = STATE_EXTRA_SECOND;
        return STREAM_INPUT;
    }

    goto extra_second_label;

/* ignore contents of extra field */
extra_var_label:
    while (filter->avail_in && state->len) {
        filter->avail_in--;
        state->len--;
        filter->next_in++;
    }

    if (state->len) {
        state->state = STATE_EXTRA_VAR;
        return STREAM_INPUT;
    } else {
        goto flag_switch_label;
    }

/* decompressing data, buffering 8 bytes that may be the end of the stream */
decompress_label:
    if (state->len + filter->avail_in >= 8) {
        memcpy(&state->prebuf[state->len], filter->next_in, 8 - state->len);
        filter->avail_in -= (8 - state->len);
        filter->next_in += (8 - state->len);
        /* buffer filled, start decompressing data */
        goto decompress_post8_label;
    } else if (flush & STREAM_FLUSH_FINISH) {
        /* ended prematurely */
        assert(!CRASH);
        goto err_label;
    } else if (flush) {
        assert("don't know other flush values" && 0);
    } else {
        memcpy(&state->prebuf[state->len], filter->next_in, filter->avail_in);
        filter->next_in += filter->avail_in;
        state->len += filter->avail_in;
        filter->avail_in = 0;
        state->state = STATE_DECOMPRESS;
        return STREAM_INPUT;
    }

/* decompressing data, after we have received at least 8 bytes */
decompress_post8_label:
    assert(!state->zstate.avail_in);
    state->zstate.next_in = state->prebuf;
    if (filter->avail_in >= 8) {
        state->zstate.avail_in = 8;
    } else if (filter->avail_in) {
        state->zstate.avail_in = filter->avail_in;
    } else if (flush & STREAM_FLUSH_FINISH) {
        /* stream has ended */
        goto end_flush_label;
    } else if (flush) {
        assert("don't know other flush values" && 0);
        goto err_label;
    } else {
        state->state = STATE_DECOMPRESS_POST8;
        return STREAM_INPUT;
    }
    goto decompress_decomp8_label;

/* decompress the 8 previously buffered characters */
decompress_decomp8_label:
    if (((tmp = inflate(&state->zstate, Z_NO_FLUSH)) == Z_OK) 
      || (tmp == Z_BUF_ERROR)) {
        if (state->zstate.avail_out) {
            assert(!state->zstate.avail_in);
            if (filter->avail_in > 8) {
                /* have enough data to perform full decompression */

                /* fill prebuf up with data from the end of the buffer */
                memcpy(state->prebuf, filter->next_in + filter->avail_in - 8, 
                  8);

                state->zstate.next_in = ((unsigned char *) filter->next_in);
                state->zstate.avail_in = filter->avail_in - 8;
                filter->next_in = NULL;
                filter->avail_in = 0;
                goto decompress_decomp_label;
            } else {
                /* just fiddle the data into the prebuf buffer and go back for 
                 * more */
                memmove(state->prebuf, &state->prebuf[filter->avail_in], 
                  8 - filter->avail_in);
                memcpy(&state->prebuf[8 - filter->avail_in], filter->next_in, 
                  filter->avail_in);
                filter->next_in = NULL;
                filter->avail_in = 0;
                goto decompress_post8_label;
            }
        } else {
            /* didn't finish inflating */
            state->zstate.next_out 
              = ((unsigned char *) state) + sizeof(*state);
            filter->out.ok.curr_out = (char *) state->zstate.next_out;
            filter->out.ok.avail_out = state->zstate.avail_out 
              = state->bufsize;
            state->state = STATE_DECOMPRESS_DECOMP8;
            crc(state->crc, filter->out.ok.curr_out, filter->out.ok.avail_out);
            /* FIXME: assert(!filter->avail_in);*/
            return STREAM_OK;
        }
    } else {
        assert(!CRASH);
        goto err_label;
    }
    assert("can't get here" && 0);

/* decompress stuff straight from the stream */
decompress_decomp_label:
    switch ((tmp = inflate(&state->zstate, Z_NO_FLUSH))) {

    case Z_BUF_ERROR:
    case Z_OK:
        if (state->zstate.avail_out) {
            /* decompressed available data, start again */
            goto decompress_post8_label;
        } else {
            /* didn't finish inflating */
            state->zstate.next_out 
              = ((unsigned char *) state) + sizeof(*state);
            filter->out.ok.curr_out = (char *) state->zstate.next_out;
            filter->out.ok.avail_out = state->zstate.avail_out 
              = state->bufsize;
            state->state = STATE_DECOMPRESS_DECOMP;
            crc(state->crc, filter->out.ok.curr_out, filter->out.ok.avail_out);
            return STREAM_OK;
        }
        break;

    default: 
        assert(!CRASH);
        goto err_label;
    }
    assert("can't get here" && 0);

end_flush_label:
    /* flush zlib */
    switch ((tmp = inflate(&state->zstate, Z_FULL_FLUSH))) {
    case Z_OK:
    case Z_BUF_ERROR:
        /* check if we have some output */
        if (state->zstate.avail_out != state->bufsize) {
            state->zstate.next_out 
              = ((unsigned char *) state) + sizeof(*state);
            filter->out.ok.curr_out = (char *) state->zstate.next_out;
            filter->out.ok.avail_out = state->bufsize - state->zstate.avail_out;
            state->zstate.avail_out = state->bufsize;
            state->state = STATE_END_FLUSH;
            crc(state->crc, filter->out.ok.curr_out, filter->out.ok.avail_out);
            return STREAM_OK;
        } else {
            goto end_check_label;
        }
        break;

    default: 
        assert(!CRASH);
        goto err_label;
    }

end_check_label:
    /* stream has ended, prebuf should now contain:
     *   0   1   2   3   4   5   6   7
     * +---+---+---+---+---+---+---+---+
     * |     CRC32     |     ISIZE     |
     * +---+---+---+---+---+---+---+---+
     * 
     * with CRC and ISIZE both being 4-byte integers, least significant
     * byte first */
    tmp = state->prebuf[0];
    tmp += state->prebuf[1] << 8;
    tmp += state->prebuf[2] << 16;
    tmp += state->prebuf[3] << 24;

    /* tmp is now crc */
    if (tmp != crc_sum(state->crc)) {
        /* checksum failed */
        return STREAM_EINVAL;
    }

    tmp = state->prebuf[4];
    tmp += state->prebuf[5] << 8;
    tmp += state->prebuf[6] << 16;
    tmp += state->prebuf[7] << 24;

    /* tmp is now isize */
    if (tmp != state->zstate.total_out) {
        /* size comparison failed */
        return STREAM_EINVAL;
    }

/* this section commented out becuase it failed (for no apparent reason) on a
 * machine with zlib 1.1.4, and doesn't really add anything (crc checks data
 * consistency) */
#if 0
    /* provide calculated adler32 checksum to zlib and finish.  Note that the 
     * adler32 checksum is provided MOST significant byte first, which is 
     * inconsistent with the rest of the zlib stuff. */
    state->prebuf[3] = state->zstate.adler & 0xff;
    state->prebuf[2] = (state->zstate.adler >> 8) & 0xff;
    state->prebuf[1] = (state->zstate.adler >> 16) & 0xff;
    state->prebuf[0] = (state->zstate.adler >> 24) & 0xff;
    assert(!state->zstate.avail_in);
    state->zstate.next_in = state->prebuf;
    state->zstate.avail_in = 4;
    switch ((tmp = inflate(&state->zstate, Z_FINISH))) {
    case Z_STREAM_END:
        /* stream has ended, zlib is happy */
        goto end_label;

    default: 
        assert(!CRASH);
        goto err_label;
    }
    assert("can't get here" && 0);
#endif

    goto end_label;

end_label:
    filter->out.delet.after = 0;
    state->state = STATE_ERR;          /* shouldn't re-enter this function, 
                                        * because we're about to... */
    return STREAM_DELETE;              /* ...delete this module */

err_label:
    assert(!CRASH);
    state->state = STATE_ERR;
    return STREAM_EINVAL;
}

static enum stream_ret gunzipfilter_delete(struct stream_filter *filter) {
    struct gunzipfilter *state = (void *) filter;

    assert(filter->idfn(filter) == gunzipfilter_id(filter));

    inflateEnd(&state->zstate);
    crc_delete(state->crc);

    free(filter);
    return STREAM_OK;
}

struct gunzipfilter *gunzipfilter_new(unsigned int bufsize) {
    struct gunzipfilter *state;
    unsigned char *header = (unsigned char *) "\x78\x01";
    int ret;

    if ((state = malloc(sizeof(*state) + bufsize)) 
      && (state->crc = crc_new())) {
        state->state = STATE_START;
        state->bufsize = bufsize + !bufsize;
        state->filter.filter = gunzipfilter_filter;
        state->filter.idfn = gunzipfilter_id;
        state->filter.deletefn = gunzipfilter_delete;
        state->zstate.zalloc = Z_NULL;
        state->zstate.zfree = Z_NULL;
        state->zstate.opaque = NULL;
        state->zstate.next_out = ((unsigned char *) state) + sizeof(*state);
        state->zstate.avail_out = bufsize;
        /* provide a fake header to fool zlib into thinking that its
         * decompressing zlib format data */
        state->zstate.next_in = header;
        state->zstate.avail_in = str_len((char *) header);
        if (((ret = inflateInit(&state->zstate)) != Z_OK) 
          /* zlib must read the header now */
          || ((state->zstate.avail_in)
            && (((ret = inflate(&state->zstate, Z_NO_FLUSH)) != Z_OK)
              || state->zstate.avail_in))) {

            crc_delete(state->crc);
            free(state);
            state = NULL;
        }
    } else if (state) {
        free(state);
        state = NULL;
    }

    return state;
}

