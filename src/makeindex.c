/* makeindex.c implements methods to create indexes from different types of
 * documents.
 *
 * written nml 2003-03-27
 *
 */

#include "firstinclude.h"

#include "def.h"
#include "makeindex.h"
#include "mlparse.h"
#include "postings.h"
#include "pyramid.h"
#include "psettings.h"
#include "str.h"
#include "timings.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum makeindex_states {
    STATE_ERR = -1,                        /* an unexpected error occurred */
    STATE_START = 0,                       /* start state */
    STATE_ON = 1,                          /* indexing terms */
    STATE_CONT = 2,                        /* ignoring the end of long terms */
    STATE_OFF = 3,                         /* not indexing terms */
    STATE_BINARY_CHECK = 4,                /* checking for binary content */
    STATE_IGNORE = 5,                      /* ignoring content */
    STATE_ID = 6,                          /* reading document id */
    STATE_OFFEND = 7,                      /* not processing anything until we 
                                            * hit the required end tag */
    STATE_ENDDOC = 8,                      /* just finished a document */
    STATE_EOF = 9                          /* finished file */
};

/* structure to hold internal state */
struct makeindex_state {
    enum makeindex_states state;          /* current state */
    struct mlparse mlparser;              /* parser for HTML, TREC, INEX */
    struct psettings *settings;           /* parser settings */
    unsigned int termno;                  /* number of terms in doc thus far */
    char *term;                           /* current term */
    char *endtag;                         /* endtag we're looking for in 
                                           * OFFEND mode */
    char *docno;                          /* current document number */
    unsigned int docno_size;              /* size of docno field */
    unsigned int docno_pos;               /* position in docno field */
    unsigned int maxtermlen;              /* maximum term length */
    int eof;                              /* whether we've hit end of file */
    unsigned int stack;                   /* stack of on/off states we've been 
                                           * through */
    enum mime_types type;                 /* mime type of file */
    enum mime_types doctype;              /* mime type of last document (note 
                                           * that this can be different than 
                                           * file type) */
    struct psettings_type *ptype;         /* pointer to parser settings for 
                                           * type */
};

enum makeindex_ret makeindex_new(struct makeindex *mi, 
  struct psettings *settings, unsigned int termlen, enum mime_types type) {
    enum makeindex_ret ret;

    if (settings && (mi->state = malloc(sizeof(*mi->state))) 
      && (mi->state->term = malloc(termlen + 1))
      && (mi->state->endtag = malloc(termlen + 1))
      && (mi->state->docno = malloc(termlen + 1))
      && mlparse_new(&mi->state->mlparser, termlen, LOOKAHEAD)) {
        mi->state->settings = settings;
        mi->state->maxtermlen = termlen;
        mi->state->docno_size = termlen;
        if ((ret = makeindex_renew(mi, type)) == MAKEINDEX_OK) {
            return MAKEINDEX_OK;
        } else {
            makeindex_delete(mi);
            return ret;
        }
    } else {
        if (mi->state) {
            if (mi->state->term) {
                if (mi->state->endtag) {
                    if (mi->state->docno) {
                        free(mi->state->docno);
                    }
                    free(mi->state->endtag);
                }
                free(mi->state->term);
            }
            free(mi->state);
        }
        return MAKEINDEX_ERR;
    }
}

enum makeindex_ret makeindex_renew(struct makeindex *mi, enum mime_types type) {
    /* get the correct set of tags from the parser settings */
    if (psettings_type_tags(mi->state->settings, type, &mi->state->ptype) 
      != PSETTINGS_OK) {
        if (psettings_type_default(mi->state->settings, type, 
              psettings_default(mi->state->settings)) != PSETTINGS_OK 
          || psettings_type_tags(mi->state->settings, type, &mi->state->ptype) 
            != PSETTINGS_OK) {

            /* failed to get ptype pointer */
            assert("can't get here" && 0);
            return MAKEINDEX_ERR;
        }
    }

    mi->state->type = type;
    mi->state->state = STATE_START;
    mi->state->termno = 0;
    mi->state->eof = 0;
    mi->state->stack = 0;
    mi->state->docno_pos = 0;
    mlparse_reinit(&mi->state->mlparser);
    return MAKEINDEX_OK;
}

void makeindex_delete(struct makeindex *mi) {
    mlparse_delete(&mi->state->mlparser);
    free(mi->state->term);
    free(mi->state->endtag);
    free(mi->state->docno);
    free(mi->state);
    mi->state = NULL;
    return;
}

unsigned int makeindex_buffered(const struct makeindex *mi) {
    return mlparse_buffered(&mi->state->mlparser);
}

void makeindex_eof(struct makeindex *mi) {
    mi->state->eof = 1;
    return;
}

enum mime_types makeindex_type(const struct makeindex *mi) {
    return mi->state->doctype;
}

const char *makeindex_docno(const struct makeindex *mi) {
    return mi->state->docno;
}

int makeindex_append_docno(struct makeindex *mi, const char *docno) {
    unsigned int len = str_len(docno);

    while (mi->state->docno_pos + len + 1 > mi->state->docno_size) {
        void *ptr = realloc(mi->state->docno, mi->state->docno_size * 2);

        if (ptr) {
            mi->state->docno = ptr;
            mi->state->docno_size *= 2;
        } else {
            return MAKEINDEX_ERR;
        }
    }

    memcpy(mi->state->docno + mi->state->docno_pos, docno, len + 1);
    mi->state->docno_pos += len;
    return MAKEINDEX_OK;
}

int makeindex_set_docno(struct makeindex *mi, const char *docno) {
    mi->state->docno[0] = '\0';
    mi->state->docno_pos = 0;
    return makeindex_append_docno(mi, docno);
}

/* badword indicates whether the passed word is probably the first word of a
 * binary file which should be ignored in the context of TREC collections.  This
 * is necessary thanks to some of the crap that got into the TREC collections
 * like wt10g. */
static int badword(const char *word, unsigned int len, const char *buf, 
  unsigned int buflen, enum mime_types *mime) {
    unsigned int i;

    /* i should change badword just to get the state (not buf, buflen), 
     * so we can check parser buffer and then main buffer properly, but this
     * works ok... */

    /* wt10g also has a MacBinary file in it, which appears to be very hard to
     * test for in this framework */

    if (len >= 4) {
        /* test for JPEG (unfortunately the word often breaks before we get to
         * EXIF or JFIF, so just test for magic bytes) */
        if ((word[0] == (char) 0xff) && (word[1] == (char) 0xd8) 
          && (word[2] == (char) 0xff) && (word[3] == (char) 0xe0)) {
            *mime = MIME_TYPE_IMAGE_JPEG;
            return 1;
        }
    }

    if (len >= 6) {
        /* test for GIF */
        if (!str_ncmp(word, "GIF8", 4) && ((word[4] == '9') || (word[4] == '7'))
          && (word[5] == 'a')) {
            *mime = MIME_TYPE_IMAGE_GIF;
            return 1;
        }
    }

    if (len >= 4) {
        /* test for MS word (badly) */
        if ((word[0] == (char) 0xd0) && (word[1] == (char) 0xcf) 
          && (word[2] == (char) 0x11) && (word[3] == (char) 0xe0)) {
            /* XXX: not strictly true, its just an OLE document, but most of 
             * them are MS word docs anyway.  Testing for MS word requires 
             * looking further into the document, so we won't be doing that 
             * here. */
            *mime = MIME_TYPE_APPLICATION_MSWORD;
            return 1;
        }

        if (len == 2) {
            if ((word[0] == (char) 0x31) && (word[1] == (char) 0xbe) 
              && buflen && (buf[0] == '\0')) {
                /* more MS word stuff :o( */
                *mime = MIME_TYPE_APPLICATION_MSWORD;
                return 1;
            } else if ((word[0] == (char) 0xfe) && (word[1] == '7') 
              && buflen && (buf[0] == '\0')) {
                /* office document */
                *mime = MIME_TYPE_APPLICATION_MSWORD;
                return 1;
            }
        }
    }

    /* test for wordperfect files */
    if (len >= 4) {
        if ((word[0] == (char) 0xff) && (word[1] == 'W') && (word[2] == 'P') 
          && (word[3] == 'C')) {
            *mime = MIME_TYPE_APPLICATION_WORDPERFECT5_1;
            return 1;
        }
    }

    /* test for postscript files */
    if (len >= 2) {
        if ((word[0] == '%') && (word[1] == '!')) {
            *mime = MIME_TYPE_APPLICATION_POSTSCRIPT;
            return 1;
        }
    }

    /* test for PDF files */
    if (len >= 5) {
        if ((word[0] == '%') && (word[1] == 'P') && (word[2] == 'D') 
          && (word[3] == 'F') && (word[4] == '-')) {
            *mime = MIME_TYPE_APPLICATION_PDF;
            return 1;
        }
    }

    if (buflen && (*buf == '\0')) {
        /* tar format has 100 byte name at start, '\0' padded.  We'll check for
         * at most 50 - len '\0''s, since we don't know how much has been 
         * stripped from the filename */
        for (i = 0; (len < 50) && (i < 50 - len) && (i < buflen); i++) {
            if (buf[i] != '\0') {
                break;
            }
        }

        assert(i);
        if (((i == 50 - len) || (i == buflen)) && (buf[i - 1] == '\0')) {
            /* looks like a tar archive */
            *mime = MIME_TYPE_APPLICATION_X_TAR;
            return 1;
        }
    }

    return 0;
}

/* macro to return from parsing, updating the parsing position as we do */
#define RETURN(val, state_)                                                   \
    mi->state->state = state_;                                                \
    mi->next_in = mi->state->mlparser.next_in;                                \
    mi->avail_in = mi->state->mlparser.avail_in;                              \
assert(val != MAKEINDEX_ERR);\
    return val

#define CASE_COMMENT()                                                        \
         MLPARSE_COMMENT | MLPARSE_END:                                       \
    case MLPARSE_COMMENT:                                                     \
        if (ret & MLPARSE_END) {                                              \
            /* end of a comment, treat as special tag */                      \
            termlen = str_len("/sgmlcomment");                                \
            assert(mi->state->maxtermlen > termlen);                          \
            str_cpy(mi->state->term, "/sgmlcomment");                         \
        } else if (1) {                                                       \
            /* start of a comment, treat as special tag */                    \
            termlen = str_len("sgmlcomment");                                 \
            assert(mi->state->maxtermlen > termlen);                          \
            str_cpy(mi->state->term, "sgmlcomment");                          \
        } else
 
/* macro to handle reception of tags and pseudo-tags, since it is all very 
 * repetitive. */
#define PROCESS_TAG(state_)                                                   \
    if (1) {                                                                  \
        int currstate = (state_ == STATE_ON || state_ == STATE_START          \
            || state_ == STATE_CONT || (state_ == STATE_BINARY_CHECK)         \
            || (state_ == STATE_ID)) ? 1 : 0;                                 \
                                                                              \
        /* lookup tag to see whether it changes our state (ignoring flow      \
         * attribute) */                                                      \
        assert(termlen <= mi->state->maxtermlen);                             \
        mi->state->term[termlen] = '\0';                                      \
        attr = psettings_type_find(mi->state->settings, mi->state->ptype,     \
            mi->state->term);                                                 \
        if (attr & PSETTINGS_ATTR_INDEX || attr & PSETTINGS_ATTR_TITLE) {     \
            /* continue in on state */                                        \
            mi->state->stack <<= 1;                                           \
            mi->state->stack |= !currstate;                                   \
            goto on_label;                                                    \
        } else if (!(attr & ~PSETTINGS_ATTR_FLOW)) {                          \
            /* don't index */                                                 \
            mi->state->stack <<= 1;                                           \
            mi->state->stack |= !currstate;                                   \
            goto off_label;                                                   \
        } else if (attr & PSETTINGS_ATTR_DOCEND) {                            \
            mi->state->stack = 0;                                             \
            if (postings_update(mi->post, &mi->stats)) {                      \
                RETURN(MAKEINDEX_ENDDOC, STATE_ENDDOC);                       \
            } else {                                                          \
                RETURN(MAKEINDEX_ERR, STATE_ERR);                             \
            }                                                                 \
        } else if (attr & PSETTINGS_ATTR_ID) {                                \
            mi->state->docno_pos = 0;                                         \
            goto id_label;                                                    \
        } else if (attr & PSETTINGS_ATTR_CHECKBIN) {                          \
            /* need to check for a binary document */                         \
            goto binary_check_label;                                          \
        } else if (attr & PSETTINGS_ATTR_POP) {                               \
            if (mi->state->stack & 1) {                                       \
                mi->state->stack >>= 1;                                       \
                goto off_label;                                               \
            } else {                                                          \
                /* note that on is 0 so that if stack underflows then         \
                 * indexing is turned on, not off */                          \
                mi->state->stack >>= 1;                                       \
                goto on_label;                                                \
            }                                                                 \
        } else if (attr & PSETTINGS_ATTR_OFF_END) {                           \
            /* ignore everything until we hit the end tag we're after */      \
            if (mi->state->term[0] != '/') {                                  \
                mi->state->endtag[0] = '/';                                   \
                mi->state->endtag[1] = '\0';                                  \
            } else {                                                          \
                mi->state->endtag[0] = '\0';                                  \
            }                                                                 \
            str_lcat(mi->state->endtag, mi->state->term,                      \
              mi->state->maxtermlen);                                         \
            str_tolower(mi->state->endtag);                                   \
            goto ignore_label;                                                \
        } else {                                                              \
            /* noop, don't do anything */                                     \
            assert(attr & PSETTINGS_ATTR_NOOP);                               \
        }                                                                     \
    } else

/* macro to handle reception of MLPARSE_INPUT in each state.  state_ is the
 * state variable to allow return to this state */
#define CASE_INPUT(state_)                                                    \
         MLPARSE_INPUT:                                                       \
        /* need more input */                                                 \
        if (mi->state->eof) {                                                 \
            mlparse_eof(&mi->state->mlparser);                                \
        } else {                                                              \
            RETURN(MAKEINDEX_INPUT, state_);                                  \
        }                                                                     \
        break

/* macro to handle reception of MLPARSE_EOF in each state.  first specifies
 * whether anything has been read from this document, in which case ENDDOC is
 * returned, else EOF is returned immediately */
#define CASE_EOF(first)                                                       \
         MLPARSE_EOF:                                                         \
        /* we aren't expecting eof until we cause it by calling               \
         * mlparse_eof */                                                     \
        assert(mi->state->eof);                                               \
        if (first) {                                                          \
            RETURN(MAKEINDEX_EOF, STATE_START);                               \
        } else {                                                              \
            if (postings_update(mi->post, &mi->stats)) {                      \
                RETURN(MAKEINDEX_ENDDOC, STATE_EOF);                          \
            } else {                                                          \
                RETURN(MAKEINDEX_ERR, STATE_ERR);                             \
            }                                                                 \
        }                                                                     \
        break;

#define CASE_ERR()                                                            \
         MLPARSE_ERR:                                                         \
        /* unexpected error */                                                \
        assert(0);                                                            \
        RETURN(MAKEINDEX_ERR, STATE_ERR);                                     \
        break

enum makeindex_ret makeindex(struct makeindex *mi) {
    unsigned int termlen,
                 tmp;
    enum mlparse_ret ret;
    enum psettings_attr attr;
    enum mime_types mime;

    mi->state->mlparser.next_in = mi->next_in;
    mi->state->mlparser.avail_in = mi->avail_in;

    /* jump to the correct state */
    switch (mi->state->state) {
    case STATE_START: goto start_label;
    case STATE_ID: goto id_label;
    case STATE_ON: goto on_label;
    case STATE_CONT: goto cont_label;
    case STATE_OFF: goto off_label;
    case STATE_EOF: goto eof_label;
    case STATE_BINARY_CHECK: goto binary_check_label;
    case STATE_IGNORE: goto ignore_label;
    case STATE_ENDDOC: goto enddoc_label;
    default: RETURN(MAKEINDEX_ERR, STATE_ERR);
    };

/* start state is just the on state, except that we haven't gotten anything in
 * this document yet, and so we to call postings_adddoc if we get anything */
start_label:
    /* reset termno, as we're about to start a new document */
    mi->state->termno = 0;
    mi->state->doctype = mi->state->type;
    while (1) {
        ret = mlparse_parse(&mi->state->mlparser, mi->state->term, &termlen, 1);
        switch (ret) {
        default:
            /* indicate to the postings that the document has started */
            postings_adddoc(mi->post, mi->docs++);
            /* complete hack, transfer to on state and complete processing */
            goto on_middle;

        case CASE_INPUT(STATE_START);
        case CASE_ERR();
        case CASE_EOF(1);  /* note 1 indicates that this is the start state */
        }
    }

/* on state, add received words to the index and look for tags/comments that
 * alter our state */
on_label:
    while (1) {
        ret = mlparse_parse(&mi->state->mlparser, mi->state->term, &termlen, 1);
        assert(ret != MLPARSE_WHITESPACE);

/* complete hack, but we'd like to jump to this point from binary_check_label
 * and first_label to avoid duplicating code */
on_middle:

        switch (ret) {
        case MLPARSE_WORD:
        case MLPARSE_END | MLPARSE_WORD:
            /* got a regular word, add it to the postings */
            assert(termlen <= mi->state->maxtermlen);
            mi->state->term[termlen] = '\0';
            if (postings_addword(mi->post, mi->state->term, 
              mi->state->termno++)) {
                if (ret & MLPARSE_END) {
                    /* increment termno so phrases don't match */
                    mi->state->termno++;
                }
            } else {
                RETURN(MAKEINDEX_ERR, STATE_ERR);
            }
            break;

        case MLPARSE_CONT | MLPARSE_WORD:
            /* got a long word, don't add it to the postings */
            assert(termlen <= mi->state->maxtermlen);
            /* increment termno so phrases don't match over long word */
            mi->state->termno++;
            goto cont_label;

        /* things we ignore */
        case MLPARSE_PARAM:
        case MLPARSE_CONT | MLPARSE_PARAM:
        case MLPARSE_END | MLPARSE_PARAM:
        case MLPARSE_CONT | MLPARSE_PARAMVAL:
        case MLPARSE_END | MLPARSE_PARAMVAL:
        case MLPARSE_PARAMVAL:
        case MLPARSE_CDATA:
        case MLPARSE_END | MLPARSE_CDATA:
            break;

        case CASE_COMMENT();  /* fallthrough to tag processing */
        case MLPARSE_CONT | MLPARSE_TAG: case MLPARSE_TAG: 
            PROCESS_TAG(STATE_ON);
            break;

        case CASE_INPUT(STATE_ON);
        case CASE_EOF(0);
        default:
        case CASE_ERR();
        }
    }

/* cont state, in the middle of a long word, ignore everything until it ends */
cont_label:
    while (1) {
        ret = mlparse_parse(&mi->state->mlparser, mi->state->term, &termlen, 1);

        /* ignore everything except tags, errors and end comments */
        switch (ret) {
        case MLPARSE_CONT | MLPARSE_WORD:
            /* ignore */
            break;

        case MLPARSE_END | MLPARSE_WORD:
        case MLPARSE_WORD:
            /* got a sensible sized word, ignoring ends */
            goto on_label;
            break;

        case CASE_INPUT(STATE_CONT);
        case CASE_EOF(0);

        default:
            /* error, shouldn't get anything but a word here fallthrough */
        case CASE_ERR();
        }
    }

/* off state, ignoring all words, just waiting for a tag/end comment that may
 * alter our state */
off_label:
    while (1) {
        ret = mlparse_parse(&mi->state->mlparser, mi->state->term, &termlen, 1);

        /* ignore everything except tags, errors and end comments */
        switch (ret) {
        default:
            /* ignore */
            break;

        case MLPARSE_CONT | MLPARSE_WHITESPACE:
        case MLPARSE_WHITESPACE:
            /* not expecting whitespace, fallthrough to error */
        case CASE_ERR();

        case CASE_COMMENT();  /* fallthrough to tag processing */
        case MLPARSE_CONT | MLPARSE_TAG: case MLPARSE_TAG: 
            PROCESS_TAG(STATE_OFF);
            break;

        case CASE_EOF(0);
        case CASE_INPUT(STATE_OFF);
        }
    }

/* parsing the docno of this document */
id_label:
    while (1) {
        /* note NO STRIPPING so that we get the full, unaltered docno */
        ret = mlparse_parse(&mi->state->mlparser, mi->state->term, &termlen, 0);

        switch (ret) {
        case MLPARSE_END | MLPARSE_WORD:
        case MLPARSE_WORD:
        case MLPARSE_CONT | MLPARSE_WORD:
            mi->state->term[termlen] = '\0';
            if (makeindex_append_docno(mi, mi->state->term) != MAKEINDEX_OK) {
                RETURN(MAKEINDEX_ERR, STATE_ERR);
            }
            break;

        case MLPARSE_CONT | MLPARSE_WHITESPACE:
        case MLPARSE_WHITESPACE:
            mi->state->term[termlen] = '\0';
            if (mi->state->docno_pos 
              && makeindex_append_docno(mi, mi->state->term) != MAKEINDEX_OK) {
                RETURN(MAKEINDEX_ERR, STATE_ERR);
            }
            break;

        case MLPARSE_PARAM:
        case MLPARSE_CONT | MLPARSE_PARAM:
        case MLPARSE_END | MLPARSE_PARAM:
        case MLPARSE_CONT | MLPARSE_PARAMVAL:
        case MLPARSE_END | MLPARSE_PARAMVAL:
        case MLPARSE_PARAMVAL:
        case MLPARSE_CDATA:
        case MLPARSE_END | MLPARSE_CDATA:
            /* ignore these */
            break;

        case CASE_COMMENT();  /* fallthrough to tag processing */
        case MLPARSE_CONT | MLPARSE_TAG: case MLPARSE_TAG: 
            mi->state->term[termlen] = '\0';
            str_tolower(mi->state->term);
            /* strip whitespace from the end of the docno */
            while (mi->state->docno_pos 
              && isspace(mi->state->docno[mi->state->docno_pos - 1])) {
                mi->state->docno[--mi->state->docno_pos] = '\0';
            }
            PROCESS_TAG(STATE_ID);
            break;

        case CASE_INPUT(STATE_ID);
        case CASE_EOF(0);
        default: /* fallthrough to error */
        case CASE_ERR();
        }
    }

/* just received the end of a document.  Get rid of the previous document number
 * and go to start_label to start the new document.  Note that this has to be a
 * seperate state as non-self-identifying documents never have this happen */
enddoc_label:
    mi->state->docno[0] = '\0';
    goto start_label;

binary_check_label:
    while (1) {
        ret = mlparse_parse(&mi->state->mlparser, mi->state->term, 
          &termlen, 0);

        switch (ret) {
        case MLPARSE_WORD:
        case MLPARSE_WORD | MLPARSE_END:
            mi->state->term[termlen] = '\0';
            if (((tmp = mlparse_buffered(&mi->state->mlparser))
                && badword(mi->state->term, termlen, 
                  mlparse_buffer(&mi->state->mlparser), tmp, &mime)) 
              || (!tmp && badword(mi->state->term, termlen, 
                mi->state->mlparser.next_in, mi->state->mlparser.avail_in, 
                  &mime))) {

                /* looks like binary crap, ignore until we hit the end of the
                 * file */
                mi->state->doctype = mime;
                mi->state->endtag[0] = '\0';
                goto ignore_label;
            } else {
                termlen -= str_strip(mi->state->term);
                mi->state->term[termlen] = '\0';
                str_tolower(mi->state->term);
                if (termlen) {
                    /* total hack: resume normal processing */
                    goto on_middle;
                }
            }
            break;

        case MLPARSE_WORD | MLPARSE_CONT:
        case MLPARSE_WHITESPACE:
        case MLPARSE_WHITESPACE | MLPARSE_CONT:
            /* ignore */
            break;

        default:
            /* total hack: resume normal processing */
            mi->state->term[termlen] = '\0';
            str_tolower(mi->state->term);
            goto on_middle;

        case CASE_INPUT(STATE_BINARY_CHECK);
        }
    }

/* we're ignoring the rest of this document */
ignore_label:
    while (1) {
        ret = mlparse_parse(&mi->state->mlparser, mi->state->term, &termlen, 1);
        switch (ret) {
        /* ignore pretty much everything */
        default: /* ignore */ break;
        case CASE_COMMENT();  /* fallthrough to tag processing */
        case MLPARSE_CONT | MLPARSE_TAG: case MLPARSE_TAG: 
            mi->state->term[termlen] = '\0';
            str_tolower(mi->state->term);
            attr = psettings_type_find(mi->state->settings, mi->state->ptype,
                mi->state->term);
            /* only perform normal actions if it matches the end tag we've been
             * looking for, or if its an end of document tag */
            if ((mi->state->endtag[0] 
                && !str_cmp(mi->state->endtag, mi->state->term))
              || (attr & PSETTINGS_ATTR_DOCEND)) {
                PROCESS_TAG(STATE_IGNORE);
            }
            break;

        case CASE_INPUT(STATE_IGNORE);
        case CASE_EOF(0);
        case CASE_ERR();
        }
    }

eof_label:
    RETURN(MAKEINDEX_EOF, STATE_EOF);
}

