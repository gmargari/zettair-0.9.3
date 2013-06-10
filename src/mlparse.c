/* mlparse.c implements the parser declared in mlparse.h
 *
 * If you want to figure out how this works, you're probably best off
 * compiling the program at the bottom of this file and running it over some
 * data (need to #define MLPARSE_TEST).
 *
 * there are two main components to understanding the code,
 * understanding the buffering and understanding the state machine
 * (if you don't know what a state machine is, go find out).  
 *
 * The whole business of parsing SGML/XML/HTML is complicated by the fact that
 * almost nobody writes it correctly.  As such, we can't just accept structures
 * like open tags and open comments as correct, we need to verify that they will
 * actually close.  This requires us to examine characters further ahead in the
 * stream than we are currently parsing.  Since our interface doesn't guarantee
 * that they are available in advance, we have to be prepared to buffer a 
 * certain number of characters internally.  This complicates things a lot,
 * because we now sometimes have two sources of characters (internal and 
 * external buffers) and sometimes have two seperate positions (current parsing
 * position and current look-ahead position) in the character stream that we 
 * have to maintain.  Throughout the code i have referred to this look-ahead
 * process as 'peek'ing.  This process can only be performed forward until we
 * run out of buffer space, so state->lookahead limits the amount of characters
 * that can be passed over this way.  In the peek'ing states, the number of
 * characters peek'd over is recorded in the state->count variable, which MUST
 * record all characters passed over, so we know that they'll fit in the buffer.
 * (note that i actually break this slightly for the eref peek, but we only 
 * allow very short erefs).  The buffer is implemented by the state->buf 
 * (current  buffer), state->tmpbuf (spare buffer), state->pos 
 * (position in current buffer) and state->end (end of data in current buffer)
 * variables within the state structure.  state->pos and state->end dictate how
 * much data is in the buffer, and must both be placed at the start of the 
 * buffer if no buffered data is available (simplifies the buffering process).
 * state->tmpbuf is used for miscellaneous purposes, including copying buffered
 * data into it to make it contiguous.  This occurs if we don't have enough
 * space in the current buffer for text that we want to add to it.  At the end
 * of each peek'ing process we want to ensure that the text we have looked over
 * is either all in the internal buffer, or all in the external buffer (and
 * hence contiguous).  This means that in states where we know that we've
 * already parsed the data we are looking at, we don't have to worry about
 * testing for the end of the stream until we hit the end delimiter.  Note that
 * although buffering is necessary for the correct operation of the parser, the
 * assumption is that large buffers are available and that buffering will be
 * fairly rare.  Thus our optimisation efforts are directed toward the case
 * where buffering is extremely rare, ensuring only that buffering works when
 * required.  Non-stripping operation is likewise supported but not considered
 * important for performance purposes.
 *
 * Another set of design decisions had to be made about what constitutes markup,
 * especially in the cases where it doesn't quite conform to the standards.
 * Particular situations that cropped up were what to accept in a tag name, 
 * (we accept the XML/SGML name character set) where to allow whitespace in
 * structural elements (tags: anywhere but straight after '<', comments: not in
 * '<!--' or '-->', cdata: not in '<![' or ']]>', entity references: not 
 * allowed).  I beleive that valid SGML tags aren't allowed whitespace 
 * immediately after the start tag, although its a little hard to tell from
 * casual inspection of the productions.
 *
 * The state machine is implemented as a set of states (declared in enum
 * mlparse_states) and a set of flags (in enum mlparse_flags).  The flags are
 * supposed to be orthogonal to the states, so that pretty much any set of flags
 * can occur in any state (this isn't true of FLAG_COMMENT and FLAG_CDATA, but
 * close enough).  The JUMP macro implements a mapping between states and flags
 * and the bits of code that implement operations for that state.  In this way,
 * different, specialised bits of code can handle the same state when different
 * flags are in effect, as happens to the word parsing states when FLAG_COMMENT
 * or FLAG_CDATA is on.  This could also be used to optimise away some of the
 * overhead of buffering/stripping (both of which weren't in the previous
 * version of this parser) by specialising the most used states so that they
 * don't have to use conditionals to determine whether stripping/buffering are
 * on.  However, states that are connected by direct goto statements (pretty
 * much anything not seperated by peek states, which must use JUMP statements to
 * exit given the uncertainty of where they came from) have to be consistent in
 * their assumptions of what flags are enabled.  One technique that i have used
 * to reduce the bulk of the code is to use macro's and indirect blocks of code
 * that appear as 'transitional' states (marked with trans_).  The macros are
 * fairly self-evident (and commented), but in some cases the inline replacement
 * of macros was bulking up the code a lot.  In these situations i expanded the
 * macros in only a few places in the code (typically in a trans_ block of
 * code), using local variables in the trans union to pass parameters, and then
 * used a stub macro to marshall parameters and jump to the indirect macro
 * location.
 *
 * A major design decision in the code was to allow 'floating' states, that act
 * sort of like functions in that you can jump to them from anywhere in the code
 * and they return through (an extremely) limited stack mechanism.  The stack is
 * the state->next_state variable, which specifies how the 'floating' states are
 * to exit.  The 'floating' states are those that detect/process entity
 * references/tags/other entities, since they can occur in a number of contexts,
 * and keeping track of exactly what the context is without this technique is
 * extremely difficult.  However, since only one next_state variable is
 * available, only one 'floating' state can be active at a time.  This caused
 * some minor issues with the end sentence detection, which i initially tried to
 * implement as a 'floating' state, but couldn't as that raised scenarios when i
 * wanted two floating states active at one time (like 'word.&nbsp;', where the
 * transition from end sentence state to entity reference state causes us to
 * kill the next_state value that the end sentence state relies upon.
 *
 * The code could be improved in many ways:
 *   - specialise some states (especially the toplevel word detection states)
 *     for stripping and non-buffering for performance
 *   - allow multi-character entity references (not necessary at the moment,
 *     since we only allow HTML and numeric erefs, which only reference a single
 *     character).  This could be implemented by loading st->count with the
 *     number of escaped characters and counting down in escape states.  This
 *     would probably require that esc becomes a flag as discussed below.
 *   - do a better job of detection of entity references (using a trie,
 *     probably) and numeric character decoding (anything other than strtol).
 *     Same thing applies to recognition of marked sections.  This will become
 *     much more important if you want to recognise custom entity references.
 *   - most of the CASE macros should be moved to a more central bit of code,
 *     since they are useful for more than just here.  It would also be an
 *     improvement if the characters were turned into numbers so that it will
 *     work identically on non-ASCII machines.
 *   - further character analysis (maybe?).  This would complicate things, but
 *     better output might be obtained by breaking words across characters like
 *     '/' (which seems to unequivocally represent an alternative in english
 *     text) and by allowing (double) quotes between sentence ending punctuation
 *     and whitespace (which i tried but removed to reduce complexity).
 *   - escape (almost certainly) and end sentence (maybe) should be flags.  They
 *     aren't because of lack of foresight by me and the fact that turning end
 *     sentence into a flag could contention over the next_state variable as
 *     discussed above.
 *   - declarations could be handled properly, which would assist in comment
 *     resolution stuff.  It isn't particularly hard either, just a bit more
 *     work.  However, theres a bit of a conflict between SGML and XML here, in
 *     that XML expects comments to be a single entity.  The SGML comment is
 *     more general, and solves some problems with whitespace in comment
 *     start/end.  Somewhat harder along the same lines is to allow DTD 
 *     declarations using the <!DTD [ ]> syntax, since it involves detecting 
 *     tags within tags.
 *   - mark EOF as a flag, so that user only has to call EOF once.
 *     Unfortunately this may require a lot more in the way of states to handle
 *     properly.
 *   - sort out what should/shouldn't be filtered from parameter names (+
 *     implicit values)
 *   - self-ending tags (i.e. <p/>) are truncated for the end tag return if the
 *     name goes over wordlen chars
 *   - explicit end-tag notification (simple to do, just not done because i
 *     didn't want to make it less efficient)
 *
 * A few more random notes about the code:
 *   - length has to be assigned before using RETURN().  I've assigned it
 *     immediately before use so you can use grep to validate this (a frequent
 *     source of bugs not to assign length before RETURN()).
 *
 * FIXME: include (at least conceptual) state diagrams
 *
 * written nml 2004-04-10
 *
 */

/* XXX: should decouple buffer size and lookahead so fill_buffer can scale up */

/* test: */
/* XXX: vary buffer size and compare outputs */

#include "firstinclude.h" 

#include "mlparse.h"

#include "ascii.h" 
#include "def.h" 
#include "str.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* the maximum number of characters that we recognise entity references over */
#define MAXENTITYREF 8

/* flags that store additional state about the parsing */
enum mlparse_flags {
    FLAG_NONE = 0,                  /* no flags */
    FLAG_STRIP = 1,                 /* flag indicating that junk can be 
                                     * stripped out while parsing.  Note that
                                     * strip is never set in the flags, its
                                     * passed by the user at every invocation,
                                     * so it can change between each call. */
    FLAG_BUFFER = 2,                /* indicates that bytes have been stored in 
                                     * the internal buffer */
    FLAG_COMMENT = 4,               /* indicates that we're in a comment */
    FLAG_CDATA = 8,                 /* indicates that we're in a cdata sec. */

    /* entry to allow us to mask in/out flags easily */
    FLAG_ALL = FLAG_STRIP | FLAG_BUFFER | FLAG_COMMENT | FLAG_CDATA,
    FLAG_BITS = 4                   /* number of flag bits (MUST keep this up 
                                     * to date) */
};

/* states that we can be in when we enter the parser */
enum mlparse_states {
    STATE_ERR = 0,                    /* error state */

    /* states based on words */
    STATE_TOPLEVEL = 1,               /* not in any HTML entities or words */
    STATE_WORD = 2,                   /* in a word */
    STATE_WORD_ENDS = 3,              /* in a word, after sentence-ending
                                       * punctuation */
    STATE_PUNC = 6,                   /* in a word, after punctuation */
    STATE_PUNC_ENDS = 7,              /* in a word, after sentence ending 
                                       * punctuation, after punctuation*/
    STATE_ACRONYM = 8,                /* in an acronym, after capital letter */
    STATE_ACRONYM_LETTER = 9,         /* in an acronym, after a period */
    STATE_SPACE = 10,                 /* in whitespace */

    /* states based on tags */
    STATE_TAG_PEEK_FIRST = 20,        /* looking ahead to validate a tag, 
                                       * expecting first letter of tag name (or
                                       * '!' or '?' or '/' depending on what it
                                       * is) */
    STATE_TAG_PEEK_NAME = 21,         /* looking ahead to validate a tag, 
                                       * parsing the tag name after first 
                                       * char */
    STATE_TAG_PEEK_NAME_CONT = 22,    /* looking ahead to validate a tag,
                                       * parsing tag name after its exceeded
                                       * wordlen characters */
    STATE_TAG_PEEK = 23,              /* looking ahead to validate a tag, 
                                       * after the tag name */
    STATE_TAG_ENTRY = 24,             /* in a tag from start once 
                                       * validated */
    STATE_TAG_NAME_CONT = 25,         /* in a tag name after 
                                       * continuation */
    STATE_TAG_NAME_SELFEND = 26,      /* in a tag that may self-end during 
                                       * name */ 
    STATE_TAG = 27,                   /* in a tag after name */
    STATE_TAG_SELFEND = 28,           /* in a tag that may self-end */

    /* states based on parameters/values */
    STATE_PARAM = 30,                 /* in a parameter name */
    STATE_PARAM_EQ = 31,              /* after parameter name */
    STATE_PVAL_FIRST = 32,            /* at the first char of a pval */
    STATE_PVAL = 33,                  /* in a whitespace-delimited pval */
    STATE_PVAL_PUNC = 35,             /* in a whitespace-delimited pval, after 
                                       * puncation */
    STATE_PVAL_SELFEND = 36,          /* in a whitespace-delimited pval, after 
                                       * a / may self-end the tag */
    STATE_PVAL_QUOT = 37,             /* in a quote-delimited pval */
    STATE_PVAL_QUOT_WORD = 38,        /* in a word in a quote-delimited pval */
    STATE_PVAL_QUOT_PUNC = 40,        /* in a word in a quote-delimited pval, 
                                       * after punctuation */
    STATE_PVAL_QUOT_SPACE = 41,       /* in whitespaec in a quote-delimited 
                                       * pval */
    STATE_PVAL_DQUOT = 42,            /*                                     */
    STATE_PVAL_DQUOT_WORD = 43,       /*  these states are equivalent to     */
    STATE_PVAL_DQUOT_PUNC = 45,       /*  double-quote delimited pvals       */
    STATE_PVAL_DQUOT_SPACE = 46,      /*                                     */

    /* states based on entity references */
    STATE_EREF_PEEK_FIRST = 50,       /* looking ahead to validate an eref, on 
                                       * first character after '&' */
    STATE_EREF_NUM_PEEK_FIRST = 52,   /* looking ahead to validate a numeric 
                                       * eref, on first char after '#' */
    STATE_EREF_NUM_PEEK = 53,         /* looking ahead to validate a numeric 
                                       * eref */
    STATE_EREF_HEX_PEEK = 54,         /* looking ahead to validate a 
                                       * hexadecimal numeric eref */
    STATE_EREF_PEEK = 55,             /* looking ahead to validate a 
                                       * non-numeric eref */

    /* states based on comment or cdata sections */

    STATE_DECL_PEEK = 70,                 /* looking ahead to validate a 
                                           * declaration, after '!' char */
    STATE_COMMENT_PEEK_FIRST = 71,        /* looking ahead to validate a 
                                           * comment, after first '-' char */
    STATE_COMMENT_PEEK = 72,              /* looking ahead to validate a 
                                           * comment */

    /* these two states just validate --> ending on comment (one for each char
     * except the first) */
    STATE_COMMENT_PEEK_END_FIRST = 73,
    STATE_COMMENT_PEEK_END = 74,

    /* these three states just look out for <!-- within comment (one for each
     * char except the first) */
    STATE_COMMENT_PEEK_BADEND_FIRST = 75,
    STATE_COMMENT_PEEK_BADEND_SECOND = 76,   
    STATE_COMMENT_PEEK_BADEND = 77,

    STATE_COMMENT_ENTRY = 78,             /* parsing a validated comment */
    STATE_MARKSEC_PEEK_LABEL_FIRST = 79,  /* looking ahead to validate a 
                                           * marked section, after first '[' */
    STATE_MARKSEC_PEEK_LABEL = 80,        /* looking ahead to validate a marked 
                                           * section, during its label 
                                           * (i.e. 'CDATA') */
    STATE_MARKSEC_PEEK_LABEL_LAST = 81,   /* looking ahead to validate a marked 
                                           * section, after end of label */
    STATE_MARKSEC_PEEK = 82,              /* looking ahead to validate a marked
                                           * section */
    STATE_MARKSEC_PEEK_END_FIRST = 83,    /* looking ahead to validate a marked 
                                           * section, after first ']' end 
                                           * char */
    STATE_MARKSEC_PEEK_END = 84,          /* looking ahead to validate a marked
                                           * section, after second ']' end 
                                           * char */
    STATE_CDATA_ENTRY = 85,               /* parsing a validated cdata marked 
                                           * section */

    /* these two states just look for --> within a cdata/comment section */
    STATE_CCDATA_END_COMMENT_FIRST = 86,
    STATE_CCDATA_END_COMMENT = 87,

    /* these two states just look for ]]> within a cdata/comment section */
    STATE_CCDATA_END_CDATA_FIRST = 88,
    STATE_CCDATA_END_CDATA = 89,

    STATE_CCDATA_RECOVERY = 90,           /* recover from false cdata/comment 
                                           * end detection */

    /* escape states, which correspond to the normal states except that the next
     * character is escaped */
    STATE_TOPLEVEL_ESC = 100,
    STATE_WORD_ESC = 101,
    STATE_WORD_ENDS_ESC = 102,
    STATE_PUNC_ESC = 105,
    STATE_PUNC_ENDS_ESC = 106,
    STATE_SPACE_ESC = 107,
    STATE_ACRONYM_ESC = 108,
    STATE_ACRONYM_LETTER_ESC = 109,
    STATE_PVAL_QUOT_ESC = 110,
    STATE_PVAL_QUOT_WORD_ESC = 111,
    STATE_PVAL_QUOT_PUNC_ESC = 113,
    STATE_PVAL_QUOT_SPACE_ESC = 114,
    STATE_PVAL_DQUOT_ESC = 115,
    STATE_PVAL_DQUOT_WORD_ESC = 116,
    STATE_PVAL_DQUOT_PUNC_ESC = 118,
    STATE_PVAL_DQUOT_SPACE_ESC = 119,

    STATE_EOF = 120                       /* end of file state */
};

struct mlparse_state {
    enum mlparse_states state;            /* current state of parser */
    enum mlparse_flags flags;             /* additional state flags */
    unsigned int len;                     /* length of entity in state */
    enum mlparse_states next_state;       /* next state to go to (where 
                                           * applicable) */
    unsigned int count;                   /* chars we've processed in current 
                                           * state */
    unsigned int wordlen;                 /* maximum length of a word */
    unsigned int lookahead;               /* maximum length to look ahead for 
                                           * end tags */
    unsigned int buflen;                  /* length of the internal buffer */
    const char *pos;                      /* current position in buffer */
    const char *end;                      /* first position past end of 
                                           * buffer */
    char *buf;                            /* buffer for lookahead */
    char *tagbuf;                         /* buffer to store tag names in */
    unsigned int tagbuflen;               /* length of stuff in tagbuf */
    char *tmpbuf;                         /* temporary buffer (size of buf) */
    char erefbuf[MAXENTITYREF + 1];       /* entity reference buffer */
    unsigned int errline;                 /* line error occurred on (debugging 
                                           * tool) */
};

int mlparse_new(struct mlparse *space, unsigned int wordlen, 
  unsigned int lookahead) {

    if (wordlen <= 1) {
        return 0;
    }

    /* XXX: note slight overallocation of buf for fudge factor purposes:
     * sometimes the maths are a bit funny, so we allow a few extra characters
     * on the end just in case */

    if ((space->state = malloc(sizeof(*space->state))) 
      && (space->state->buflen = lookahead + 2 * MAXENTITYREF)
      && (space->state->buf = malloc(space->state->buflen + 1)) 
      && (space->state->tmpbuf = malloc(space->state->buflen + 1)) 
      && (space->state->tagbuf = malloc(wordlen + 1))) {
        space->state->wordlen = wordlen;
        space->state->lookahead = lookahead;
        space->state->tagbuf[0] = '\0';
        space->state->erefbuf[0] = '\0';
        space->state->buf[space->state->buflen] = '\0';
        mlparse_reinit(space);
    } else if (space->state) {
        if (space->state->buf) {
            if (space->state->tmpbuf) {
                free(space->state->tmpbuf);
            }
            free(space->state->buf);
        }
        free(space->state);
        return 0;
    }

    return 1;
}

void mlparse_reinit(struct mlparse *p) {
    /* reset variables */
    p->state->pos = p->state->end = p->state->buf;
    p->state->state = STATE_TOPLEVEL;             /* start in toplevel state */
    p->state->flags = FLAG_NONE;
    p->state->errline = 0;
    return;
}

unsigned long int mlparse_buffered(struct mlparse *parser) {
    if (parser->state->flags & FLAG_BUFFER) {
        return parser->state->end - parser->state->pos;
    } else {
        return 0;
    }
}

const char *mlparse_buffer(struct mlparse *parser) {
    return parser->state->pos;
}

void mlparse_delete(struct mlparse *parser) {
    free(parser->state->tagbuf);
    free(parser->state->buf);
    free(parser->state->tmpbuf);
    free(parser->state);
}

unsigned int mlparse_err_line(struct mlparse *parser) {
    return parser->state->errline;
}

/* macro to combine state values with flag values by shifting state values */
#define COMB(stateval) ((stateval) << (FLAG_BITS))

/* stub macro to jump to state state_ (modified by flags_).  unbuf_ is boolean,
 * indicating whether to allow jump to trans_unbuf_label */
#define JUMP(state_, flags_, unbuf_)                                          \
    trans.jump.state = (state_);                                              \
    trans.jump.flags = (flags_);                                              \
    if (unbuf_) {                                                             \
        goto trans_jump_unbuf_label;                                          \
    } else if (1) {                                                           \
        goto trans_jump_label;                                                \
    } else 

/* macro to manage jump table, which maps the state/flag variables to chunks of
 * code that are executed when they are encountered.  However, this has to be
 * consistent with the direct gotos between states for proper operation.  
 * (please don't change this stuff unless you *really* know what you're 
 * doing) */
#define ACTUAL_JUMP(state_, flags, unbuf_)                                    \
    if (1) {                                                                  \
        switch (COMB(state_) | (flags) | strip) {                             \
        case FLAG_STRIP | COMB(STATE_SPACE):                                  \
        case COMB(STATE_TOPLEVEL):                                            \
        case FLAG_STRIP | COMB(STATE_TOPLEVEL): goto toplevel_label;          \
                                                                              \
        case COMB(STATE_WORD_ENDS):                                           \
        case FLAG_STRIP | COMB(STATE_WORD_ENDS): goto word_ends_label;        \
                                                                              \
        case COMB(STATE_WORD):                                                \
        case FLAG_STRIP | COMB(STATE_WORD): goto word_label;                  \
                                                                              \
        case COMB(STATE_PUNC_ENDS):                                           \
        case FLAG_STRIP | COMB(STATE_PUNC_ENDS): goto punc_ends_label;        \
                                                                              \
        case COMB(STATE_PUNC):                                                \
        case FLAG_STRIP | COMB(STATE_PUNC): goto punc_label;                  \
                                                                              \
        case COMB(STATE_ACRONYM):                                             \
        case FLAG_STRIP | COMB(STATE_ACRONYM): goto acronym_label;            \
                                                                              \
        case COMB(STATE_ACRONYM_LETTER):                                      \
        case FLAG_STRIP | COMB(STATE_ACRONYM_LETTER):                         \
            goto acronym_letter_label;                                        \
        case COMB(STATE_SPACE): goto space_label;                             \
                                                                              \
        case COMB(STATE_TAG_PEEK_FIRST):                                      \
        case FLAG_STRIP | COMB(STATE_TAG_PEEK_FIRST):                         \
            goto tag_peek_first_label;                                        \
                                                                              \
        case COMB(STATE_TAG_PEEK_NAME):                                       \
        case FLAG_STRIP | COMB(STATE_TAG_PEEK_NAME):                          \
            goto tag_peek_name_label;                                         \
                                                                              \
        case COMB(STATE_TAG_PEEK_NAME_CONT):                                  \
        case FLAG_STRIP | COMB(STATE_TAG_PEEK_NAME_CONT):                     \
            goto tag_peek_name_cont_label;                                    \
                                                                              \
        case COMB(STATE_TAG_PEEK):                                            \
        case FLAG_STRIP | COMB(STATE_TAG_PEEK):                               \
            goto tag_peek_label;                                              \
                                                                              \
        case COMB(STATE_TAG_ENTRY):                                           \
        case FLAG_STRIP | COMB(STATE_TAG_ENTRY):                              \
            goto tag_entry_label;                                             \
                                                                              \
        case COMB(STATE_TAG_NAME_CONT):                                       \
        case FLAG_STRIP | COMB(STATE_TAG_NAME_CONT):                          \
            goto tag_name_cont_label;                                         \
                                                                              \
        case COMB(STATE_TAG_NAME_SELFEND):                                    \
        case FLAG_STRIP | COMB(STATE_TAG_NAME_SELFEND):                       \
            goto tag_name_selfend_label;                                      \
                                                                              \
        case COMB(STATE_TAG):                                                 \
        case FLAG_STRIP | COMB(STATE_TAG):                                    \
            goto tag_label;                                                   \
                                                                              \
        case COMB(STATE_TAG_SELFEND):                                         \
        case FLAG_STRIP | COMB(STATE_TAG_SELFEND):                            \
            goto tag_selfend_label;                                           \
                                                                              \
        case COMB(STATE_PARAM):                                               \
        case FLAG_STRIP | COMB(STATE_PARAM):                                  \
            goto param_label;                                                 \
                                                                              \
        case COMB(STATE_PARAM_EQ):                                            \
        case FLAG_STRIP | COMB(STATE_PARAM_EQ):                               \
            goto param_eq_label;                                              \
                                                                              \
        case COMB(STATE_PVAL_FIRST):                                          \
        case FLAG_STRIP | COMB(STATE_PVAL_FIRST):                             \
            goto pval_first_label;                                            \
                                                                              \
        case COMB(STATE_PVAL):                                                \
        case FLAG_STRIP | COMB(STATE_PVAL):                                   \
            goto pval_label;                                                  \
                                                                              \
        case COMB(STATE_PVAL_PUNC):                                           \
        case FLAG_STRIP | COMB(STATE_PVAL_PUNC):                              \
            goto pval_punc_label;                                             \
                                                                              \
        case COMB(STATE_PVAL_SELFEND):                                        \
        case FLAG_STRIP | COMB(STATE_PVAL_SELFEND):                           \
            goto pval_selfend_label;                                          \
                                                                              \
        case COMB(STATE_PVAL_QUOT):                                           \
        case FLAG_STRIP | COMB(STATE_PVAL_QUOT):                              \
        case FLAG_STRIP | COMB(STATE_PVAL_QUOT_SPACE):                        \
            goto pval_quot_label;                                             \
                                                                              \
        case COMB(STATE_PVAL_QUOT_WORD):                                      \
        case FLAG_STRIP | COMB(STATE_PVAL_QUOT_WORD):                         \
            goto pval_quot_word_label;                                        \
                                                                              \
        case COMB(STATE_PVAL_QUOT_PUNC):                                      \
        case FLAG_STRIP | COMB(STATE_PVAL_QUOT_PUNC):                         \
            goto pval_quot_punc_label;                                        \
                                                                              \
        case COMB(STATE_PVAL_QUOT_SPACE):                                     \
            goto pval_quot_space_label;                                       \
                                                                              \
        case COMB(STATE_PVAL_DQUOT):                                          \
        case FLAG_STRIP | COMB(STATE_PVAL_DQUOT):                             \
            goto pval_dquot_label;                                            \
                                                                              \
        case COMB(STATE_PVAL_DQUOT_WORD):                                     \
        case FLAG_STRIP | COMB(STATE_PVAL_DQUOT_WORD):                        \
            goto pval_dquot_word_label;                                       \
                                                                              \
        case COMB(STATE_PVAL_DQUOT_PUNC):                                     \
        case FLAG_STRIP | COMB(STATE_PVAL_DQUOT_PUNC):                        \
            goto pval_dquot_punc_label;                                       \
                                                                              \
        case COMB(STATE_PVAL_DQUOT_SPACE):                                    \
        case FLAG_STRIP | COMB(STATE_PVAL_DQUOT_SPACE):                       \
            goto pval_dquot_space_label;                                      \
                                                                              \
        case COMB(STATE_EREF_PEEK_FIRST):                                     \
        case FLAG_STRIP | COMB(STATE_EREF_PEEK_FIRST):                        \
            goto eref_peek_first_label;                                       \
                                                                              \
        case COMB(STATE_EREF_NUM_PEEK_FIRST):                                 \
        case FLAG_STRIP | COMB(STATE_EREF_NUM_PEEK_FIRST):                    \
            goto eref_num_peek_first_label;                                   \
                                                                              \
        case COMB(STATE_EREF_NUM_PEEK):                                       \
        case FLAG_STRIP | COMB(STATE_EREF_NUM_PEEK):                          \
            goto eref_num_peek_label;                                         \
                                                                              \
        case COMB(STATE_EREF_HEX_PEEK):                                       \
        case FLAG_STRIP | COMB(STATE_EREF_HEX_PEEK):                          \
            goto eref_hex_peek_label;                                         \
                                                                              \
        case COMB(STATE_EREF_PEEK):                                           \
        case FLAG_STRIP | COMB(STATE_EREF_PEEK):                              \
            goto eref_peek_label;                                             \
                                                                              \
        case FLAG_STRIP | COMB(STATE_SPACE_ESC):                              \
        case COMB(STATE_TOPLEVEL_ESC):                                        \
        case FLAG_STRIP | COMB(STATE_TOPLEVEL_ESC): goto toplevel_esc_label;  \
                                                                              \
        case COMB(STATE_WORD_ESC):                                            \
        case FLAG_STRIP | COMB(STATE_WORD_ESC): goto word_esc_label;          \
                                                                              \
        case COMB(STATE_WORD_ENDS_ESC):                                       \
        case FLAG_STRIP | COMB(STATE_WORD_ENDS_ESC):                          \
            goto word_ends_esc_label;                                         \
                                                                              \
        case COMB(STATE_PUNC_ESC):                                            \
        case FLAG_STRIP | COMB(STATE_PUNC_ESC): goto punc_esc_label;          \
                                                                              \
        case COMB(STATE_PUNC_ENDS_ESC):                                       \
        case FLAG_STRIP | COMB(STATE_PUNC_ENDS_ESC):                          \
            goto punc_ends_esc_label;                                         \
                                                                              \
        case COMB(STATE_SPACE_ESC): goto space_esc_label;                     \
                                                                              \
        case COMB(STATE_ACRONYM_ESC): goto acronym_esc_label;                 \
        case FLAG_STRIP | COMB(STATE_ACRONYM_ESC): goto acronym_esc_label;    \
                                                                              \
        case COMB(STATE_ACRONYM_LETTER_ESC):                                  \
        case FLAG_STRIP | COMB(STATE_ACRONYM_LETTER_ESC):                     \
            goto acronym_letter_esc_label;                                    \
                                                                              \
        case COMB(STATE_PVAL_QUOT_ESC):                                       \
        case FLAG_STRIP | COMB(STATE_PVAL_QUOT_ESC):                          \
        case FLAG_STRIP | COMB(STATE_PVAL_QUOT_SPACE_ESC):                    \
            goto pval_quot_esc_label;                                         \
                                                                              \
        case COMB(STATE_PVAL_QUOT_WORD_ESC):                                  \
        case FLAG_STRIP | COMB(STATE_PVAL_QUOT_WORD_ESC):                     \
            goto pval_quot_word_esc_label;                                    \
                                                                              \
        case COMB(STATE_PVAL_QUOT_PUNC_ESC):                                  \
        case FLAG_STRIP | COMB(STATE_PVAL_QUOT_PUNC_ESC):                     \
            goto pval_quot_punc_esc_label;                                    \
                                                                              \
        case COMB(STATE_PVAL_QUOT_SPACE_ESC):                                 \
            goto pval_quot_space_esc_label;                                   \
                                                                              \
        case COMB(STATE_PVAL_DQUOT_ESC):                                      \
        case FLAG_STRIP | COMB(STATE_PVAL_DQUOT_ESC):                         \
        case FLAG_STRIP | COMB(STATE_PVAL_DQUOT_SPACE_ESC):                   \
            goto pval_dquot_esc_label;                                        \
                                                                              \
        case COMB(STATE_PVAL_DQUOT_WORD_ESC):                                 \
        case FLAG_STRIP | COMB(STATE_PVAL_DQUOT_WORD_ESC):                    \
            goto pval_dquot_word_esc_label;                                   \
                                                                              \
        case COMB(STATE_PVAL_DQUOT_PUNC_ESC):                                 \
        case FLAG_STRIP | COMB(STATE_PVAL_DQUOT_PUNC_ESC):                    \
            goto pval_dquot_punc_esc_label;                                   \
                                                                              \
        case COMB(STATE_PVAL_DQUOT_SPACE_ESC):                                \
            goto pval_dquot_space_esc_label;                                  \
                                                                              \
        case COMB(STATE_DECL_PEEK):                                           \
        case FLAG_STRIP | COMB(STATE_DECL_PEEK):                              \
            goto decl_peek_label;                                             \
                                                                              \
        case COMB(STATE_COMMENT_PEEK_FIRST):                                  \
        case FLAG_STRIP | COMB(STATE_COMMENT_PEEK_FIRST):                     \
            goto comment_peek_first_label;                                    \
                                                                              \
        case COMB(STATE_COMMENT_PEEK):                                        \
        case FLAG_STRIP | COMB(STATE_COMMENT_PEEK):                           \
            goto comment_peek_label;                                          \
                                                                              \
        case COMB(STATE_COMMENT_PEEK_END_FIRST):                              \
        case FLAG_STRIP | COMB(STATE_COMMENT_PEEK_END_FIRST):                 \
            goto comment_peek_end_first_label;                                \
                                                                              \
        case COMB(STATE_COMMENT_PEEK_END):                                    \
        case FLAG_STRIP | COMB(STATE_COMMENT_PEEK_END):                       \
            goto comment_peek_end_label;                                      \
                                                                              \
        case COMB(STATE_COMMENT_PEEK_BADEND_FIRST):                           \
        case FLAG_STRIP | COMB(STATE_COMMENT_PEEK_BADEND_FIRST):              \
            goto comment_peek_badend_first_label;                             \
                                                                              \
        case COMB(STATE_COMMENT_PEEK_BADEND_SECOND):                          \
        case FLAG_STRIP | COMB(STATE_COMMENT_PEEK_BADEND_SECOND):             \
            goto comment_peek_badend_second_label;                            \
                                                                              \
        case COMB(STATE_COMMENT_PEEK_BADEND):                                 \
        case FLAG_STRIP | COMB(STATE_COMMENT_PEEK_BADEND):                    \
            goto comment_peek_badend_label;                                   \
                                                                              \
        case FLAG_COMMENT | COMB(STATE_COMMENT_ENTRY):                        \
        case FLAG_COMMENT | FLAG_STRIP | COMB(STATE_COMMENT_ENTRY):           \
            goto comment_entry_label;                                         \
                                                                              \
        case FLAG_COMMENT | COMB(STATE_TOPLEVEL):                             \
        case FLAG_CDATA | COMB(STATE_TOPLEVEL):                               \
        case FLAG_STRIP | FLAG_COMMENT | COMB(STATE_TOPLEVEL):                \
        case FLAG_STRIP | FLAG_CDATA | COMB(STATE_TOPLEVEL):                  \
            goto ccdata_toplevel_label;                                       \
                                                                              \
        case FLAG_COMMENT | COMB(STATE_WORD):                                 \
        case FLAG_CDATA | COMB(STATE_WORD):                                   \
        case FLAG_STRIP | FLAG_COMMENT | COMB(STATE_WORD):                    \
        case FLAG_STRIP | FLAG_CDATA | COMB(STATE_WORD):                      \
            goto ccdata_word_label;                                           \
                                                                              \
        case FLAG_COMMENT | COMB(STATE_PUNC):                                 \
        case FLAG_CDATA | COMB(STATE_PUNC):                                   \
        case FLAG_STRIP | FLAG_COMMENT | COMB(STATE_PUNC):                    \
        case FLAG_STRIP | FLAG_CDATA | COMB(STATE_PUNC):                      \
            goto ccdata_punc_label;                                           \
                                                                              \
        case FLAG_COMMENT | COMB(STATE_SPACE):                                \
        case FLAG_CDATA | COMB(STATE_SPACE):                                  \
        case FLAG_STRIP | FLAG_COMMENT | COMB(STATE_SPACE):                   \
        case FLAG_STRIP | FLAG_CDATA | COMB(STATE_SPACE):                     \
            goto ccdata_space_label;                                          \
                                                                              \
        case FLAG_COMMENT | COMB(STATE_CCDATA_END_COMMENT_FIRST):             \
        case FLAG_CDATA | COMB(STATE_CCDATA_END_COMMENT_FIRST):               \
        case FLAG_STRIP | FLAG_COMMENT | COMB(STATE_CCDATA_END_COMMENT_FIRST):\
        case FLAG_STRIP | FLAG_CDATA | COMB(STATE_CCDATA_END_COMMENT_FIRST):  \
            goto ccdata_end_comment_first_label;                              \
                                                                              \
        case FLAG_COMMENT | COMB(STATE_CCDATA_END_COMMENT):                   \
        case FLAG_CDATA | COMB(STATE_CCDATA_END_COMMENT):                     \
        case FLAG_STRIP | FLAG_COMMENT | COMB(STATE_CCDATA_END_COMMENT):      \
        case FLAG_STRIP | FLAG_CDATA | COMB(STATE_CCDATA_END_COMMENT):        \
            goto ccdata_end_comment_label;                                    \
                                                                              \
        case FLAG_COMMENT | COMB(STATE_CCDATA_END_CDATA_FIRST):               \
        case FLAG_CDATA | COMB(STATE_CCDATA_END_CDATA_FIRST):                 \
        case FLAG_STRIP | FLAG_COMMENT | COMB(STATE_CCDATA_END_CDATA_FIRST):  \
        case FLAG_STRIP | FLAG_CDATA | COMB(STATE_CCDATA_END_CDATA_FIRST):    \
            goto ccdata_end_cdata_first_label;                                \
                                                                              \
        case FLAG_COMMENT | COMB(STATE_CCDATA_END_CDATA):                     \
        case FLAG_CDATA | COMB(STATE_CCDATA_END_CDATA):                       \
        case FLAG_STRIP | FLAG_COMMENT | COMB(STATE_CCDATA_END_CDATA):        \
        case FLAG_STRIP | FLAG_CDATA | COMB(STATE_CCDATA_END_CDATA):          \
            goto ccdata_end_cdata_label;                                      \
                                                                              \
        case FLAG_COMMENT | COMB(STATE_CCDATA_RECOVERY):                      \
        case FLAG_CDATA | COMB(STATE_CCDATA_RECOVERY):                        \
        case FLAG_STRIP | FLAG_COMMENT | COMB(STATE_CCDATA_RECOVERY):         \
        case FLAG_STRIP | FLAG_CDATA | COMB(STATE_CCDATA_RECOVERY):           \
            goto ccdata_recovery_label;                                       \
                                                                              \
        case COMB(STATE_CDATA_ENTRY):                                         \
        case FLAG_STRIP | COMB(STATE_CDATA_ENTRY):                            \
        case FLAG_CDATA | COMB(STATE_CDATA_ENTRY):                            \
        case FLAG_CDATA | FLAG_STRIP | COMB(STATE_CDATA_ENTRY):               \
            goto cdata_entry_label;                                           \
                                                                              \
        case COMB(STATE_MARKSEC_PEEK_LABEL_FIRST):                            \
        case FLAG_STRIP | COMB(STATE_MARKSEC_PEEK_LABEL_FIRST):               \
            goto marksec_peek_label_first_label;                              \
                                                                              \
        case COMB(STATE_MARKSEC_PEEK_LABEL):                                  \
        case FLAG_STRIP | COMB(STATE_MARKSEC_PEEK_LABEL):                     \
            goto marksec_peek_label_label;                                    \
                                                                              \
        case COMB(STATE_MARKSEC_PEEK_LABEL_LAST):                             \
        case FLAG_STRIP | COMB(STATE_MARKSEC_PEEK_LABEL_LAST):                \
            goto marksec_peek_label_last_label;                               \
                                                                              \
        case COMB(STATE_MARKSEC_PEEK):                                        \
        case FLAG_STRIP | COMB(STATE_MARKSEC_PEEK):                           \
            goto marksec_peek_label;                                          \
                                                                              \
        case COMB(STATE_MARKSEC_PEEK_END_FIRST):                              \
        case FLAG_STRIP | COMB(STATE_MARKSEC_PEEK_END_FIRST):                 \
            goto marksec_peek_end_first_label;                                \
                                                                              \
        case COMB(STATE_MARKSEC_PEEK_END):                                    \
        case FLAG_STRIP | COMB(STATE_MARKSEC_PEEK_END):                       \
            goto marksec_peek_end_label;                                      \
                                                                              \
        case COMB(STATE_EOF):                                                 \
        case FLAG_STRIP | COMB(STATE_EOF):                                    \
        case FLAG_COMMENT | COMB(STATE_EOF):                                  \
        case FLAG_CDATA | COMB(STATE_EOF):                                    \
        case FLAG_STRIP | FLAG_COMMENT | COMB(STATE_EOF):                     \
        case FLAG_STRIP | FLAG_CDATA | COMB(STATE_EOF):                       \
        case FLAG_STRIP | FLAG_COMMENT | FLAG_CDATA | COMB(STATE_EOF):        \
            goto eof_label;                                                   \
                                                                              \
        default:                                                              \
            /* have to save state because trans_unbuffer_label will jump to   \
             * st->state after altering pos, end */                           \
            if (unbuf_) {                                                     \
                goto trans_unbuffer_label;                                    \
            } else {                                                          \
                st->state = (state_);                                         \
                st->errline = UINT_MAX;                                       \
                goto err_label;                                               \
            }                                                                 \
        }                                                                     \
    } else /* dangling else to allow trailing semi-colon */

/* macro to detect (unexpected) errors in the parsing process, leaving enough
 * info around to figure out how they occurred (hopefully) */
#define CANT_GET_HERE()                                                       \
    assert(0);                                                                \
    st->errline = __LINE__;                                                   \
    goto err_label

/* catch sentence ending punctuation (no ending colon and no first case so we 
 * can use case syntax) */
#define CASE_ENDS                                                             \
         '.': case '?': case '!'

/* convert a character case, given that its the opposite case, if CASE_FOLDING
 * has been defined */
#ifdef CASE_FOLDING
/* they want case folding */
#define TOLOWER(c) ASCII_TOLOWER(c)
#define TOUPPER(c) ASCII_TOUPPER(c)
#else
/* no case folding */
#define TOLOWER(c) c
#define TOUPPER(c) c
#endif

/* fix things up for a return where we're requesting more input */
#define RETURN_INPUT(state_)                                                  \
    trans.return_input.state = (state_);                                      \
    goto trans_return_input_label

/* fix things up for return, assuming that we're working from the buffer */
#define RETURN_BUFFER(retval, state_)                                         \
    assert(st->flags & FLAG_BUFFER);                                          \
    st->pos = pos;                                                            \
    st->state = (state_);                                                     \
    return (retval);

/* fix things up for return, assuming that we're not working from the buffer */
#define RETURN_NOBUFFER(retval, state_)                                       \
    assert(!(st->flags & FLAG_BUFFER));                                       \
    parser->next_in = pos;                                                    \
    parser->avail_in = end - pos;                                             \
    st->state = (state_);                                                     \
    return (retval);

/* fix things up for return (by altering buffer positions to reflect what we've
 * parsed this call) */
#define RETURN(retval, state_)                                                \
    if (1) {                                                                  \
        if (st->flags & FLAG_BUFFER) {                                        \
            RETURN_BUFFER(retval, state_);                                    \
        } else {                                                              \
            RETURN_NOBUFFER(retval, state_);                                  \
        }                                                                     \
    } else

/* macro to handle boilerplate transitions to tag parsing */
#define CASE_TAG(next_state_)                                                 \
         '<':                                                                 \
        tmppos = pos + 1;                                                     \
        tmpend = end;                                                         \
        st->count = 1;                                                        \
        st->next_state = (next_state_);                                       \
        goto tag_peek_first_label

/* macro to handle boilerplate transitions to entity reference parsing */
#define CASE_EREF(next_state_)                                                \
         '&':                                                                 \
        tmppos = pos + 1;                                                     \
        tmpend = end;                                                         \
        st->count = 1;                                                        \
        st->next_state = (next_state_);                                       \
        goto eref_peek_first_label

/* push a character onto the current word, returning MLPARSE_CONT | retval 
 * and maintaining state state_ if there is no room left */
#define PUSH(char_, retval, state_)                                           \
    if (st->len < st->wordlen) {                                              \
        word[st->len++] = (char_);                                            \
    } else if (1) {                                                           \
        *length = st->len;                                                    \
        st->len = 0;                                                          \
        RETURN(MLPARSE_CONT | (retval), (state_));                            \
    } else

/* a macro to place characters examined by a peek state (as indicated by tmppos)
 * into the buffer */
#define BUFFER()                                                              \
    if (1) {                                                                  \
        const char *tmpstart;                                                 \
                                                                              \
        assert((tmppos >= parser->next_in)                                    \
          && (tmppos <= parser->next_in + parser->avail_in));                 \
                                                                              \
        /* figure out where to start copying, else we're in danger of         \
         * trying to copy stuff from the buffer into the buffer */            \
        if (st->flags & FLAG_BUFFER) {                                        \
            assert((pos >= st->pos) && (pos <= st->end));                     \
            tmpstart = parser->next_in;                                       \
        } else {                                                              \
            assert(1 && (pos >= parser->next_in)                              \
              && (pos <= parser->next_in + parser->avail_in));                \
            tmpstart = pos;                                                   \
        }                                                                     \
                                                                              \
        if (tmppos - tmpstart) {                                              \
            /* need to copy stuff into buffer */                              \
            assert(st->end >= st->pos);                                       \
            assert((st->end > st->pos)                                        \
              || ((st->buf == st->end) && (st->buf == st->pos)));             \
                                                                              \
            if ((tmppos - tmpstart) < (st->buf + st->buflen - st->end)) {     \
                /* easy case, just copy chunk in to end of buffer */          \
                memcpy((char *) st->end, tmpstart, (tmppos - tmpstart));      \
                pos = st->pos;                                                \
                end = st->end += (tmppos - tmpstart);                         \
            } else {                                                          \
                unsigned int tmp = end - pos;                                 \
                char *tmpbuf = st->buf;                                       \
                                                                              \
                /* must already be in the buffer */                           \
                assert((unsigned int) ((tmppos - tmpstart) + (end - pos))     \
                  < st->buflen);                                              \
                assert((pos >= st->pos) && (pos <= st->end));                 \
                                                                              \
                /* copy existing into temporary buffer */                     \
                memcpy(st->tmpbuf, pos, tmp);                                 \
                                                                              \
                /* copy new stuff in */                                       \
                memcpy(st->tmpbuf + tmp, tmpstart, tmppos - tmpstart);        \
                tmp += tmppos - tmpstart;                                     \
                assert(tmp < st->buflen);                                     \
                                                                              \
                /* swap buffers over */                                       \
                pos = st->pos = st->buf = st->tmpbuf;                         \
                end = st->end = st->pos + tmp;                                \
                st->tmpbuf = tmpbuf;                                          \
            }                                                                 \
            st->flags |= FLAG_BUFFER;                                         \
            parser->next_in += (tmppos - tmpstart);                           \
            parser->avail_in -= (tmppos - tmpstart);                          \
        }                                                                     \
    } else

/* macro to return a stored value depending on state in state->next_state.  
 * It can return a value (setting next_state to next_state_ param) or merely 
 * continue if no value needs to be returned.  This is the end of each of the 
 * peek states, where they have to return buffered values if appropriate or 
 * continue on into the states associated with the entity just detected.  Note
 * that it alters the buffer to ensure contiguity either way. */
#define RETURN_IF_STORED(next_state_)                                         \
    if (1) {                                                                  \
        enum mlparse_ret RETURN_STORED_ret;                                   \
                                                                              \
        /* note that this only has to handle 'toplevel' states, since we      \
         * can't enter an entity while parsing another entity */              \
        switch (st->next_state) {                                             \
        case STATE_WORD:                                                      \
        case STATE_WORD_ESC:                                                  \
        case STATE_WORD_ENDS:                                                 \
        case STATE_WORD_ENDS_ESC:                                             \
        case STATE_PUNC:                                                      \
        case STATE_PUNC_ESC:                                                  \
        case STATE_PUNC_ENDS:                                                 \
        case STATE_PUNC_ENDS_ESC:                                             \
        case STATE_ACRONYM:                                                   \
        case STATE_ACRONYM_ESC:                                               \
        case STATE_ACRONYM_LETTER:                                            \
        case STATE_ACRONYM_LETTER_ESC:                                        \
            *length = st->len;                                                \
            RETURN_STORED_ret = MLPARSE_WORD;                                 \
            break;                                                            \
                                                                              \
        case STATE_SPACE:                                                     \
        case STATE_SPACE_ESC:                                                 \
            *length = st->len;                                                \
            RETURN_STORED_ret = MLPARSE_WHITESPACE;                           \
            break;                                                            \
                                                                              \
        default:                                                              \
            RETURN_STORED_ret = MLPARSE_INPUT;                                \
            break;                                                            \
        }                                                                     \
                                                                              \
        /* ensure everything is either wholly buffered or not buffered at all \
         * to ensure contiguity as we reparse over the peek'd section.  Also  \
         * save buffer position as we might return below. */                  \
        if ((st->flags & FLAG_BUFFER) && (tmppos >= parser->next_in)          \
          && (tmppos <= parser->next_in + parser->avail_in)) {                \
            /* need to buffer */                                              \
            assert((pos >= st->pos)                                           \
              && (pos <= st->end));                                           \
                                                                              \
            st->pos = pos;                                                    \
            BUFFER();                                                         \
        } else if (!(st->flags & FLAG_BUFFER)) {                              \
            /* nothing in buffer, no need to alter it */                      \
            assert((pos >= parser->next_in)                                   \
              && (pos <= parser->next_in + parser->avail_in));                \
            assert((tmppos >= parser->next_in)                                \
              && (tmppos <= parser->next_in + parser->avail_in));             \
                                                                              \
            parser->next_in = pos;                                            \
            parser->avail_in = end - pos;                                     \
        } else {                                                              \
            st->pos = pos;                                                    \
        }                                                                     \
                                                                              \
        st->len = 0;                                                          \
        if (RETURN_STORED_ret != MLPARSE_INPUT) {                             \
            st->next_state = STATE_ERR;                                       \
            st->state = (next_state_);                                        \
            return (RETURN_STORED_ret);                                       \
        }                                                                     \
    } else 

/* stub macro to jump to end buffering code, which allows peek states to manage
 * the buffer/get text from buffer/finish looking ahead after a certain number
 * of characters (limit_) */
#define ENDBUF(limit_, state_)                                                \
    trans.endbuf.state = (state_);                                            \
    trans.endbuf.limit = (limit_);                                            \
    goto trans_endbuf_label;

int mlparse_parse(struct mlparse *parser, char *word, unsigned int *length, 
  int strip) {
    struct mlparse_state *st = parser->state;
    const char *pos = parser->next_in,
               *end = parser->next_in + parser->avail_in,
               *tmppos = pos,
               *tmpend = end;

    /* used to pass information to transitional states */
    union {
        struct {
            enum mlparse_states state;
        } return_input;

        struct {
            enum mlparse_states state;
            enum mlparse_flags flags;
        } jump;

        struct {
            enum mlparse_states state;
            unsigned int limit;
        } endbuf;
    } trans;

    strip = !!strip * FLAG_STRIP;

    /* some of the code relies on strip being 1 for stripping (i.e. *length =
     * st->len - strip, which removes last character under stripping) */
    assert(((strip == 1) || (strip == 0)) && (FLAG_STRIP == 1));

    /* fallthrough to jump to initial state (or to buffer state if we need to 
     * first) */
    JUMP(st->state, st->flags, 1); 

trans_jump_unbuf_label:
    ACTUAL_JUMP(trans.jump.state, trans.jump.flags, 1);

trans_jump_label:
    ACTUAL_JUMP(trans.jump.state, trans.jump.flags, 0);

trans_endbuf_label:
    if (st->count < trans.endbuf.limit) {
        /* detect whether we're currently working in the buffer (except that the
         * flag is unreliable because we've come from a peek state) */
        if ((tmpend == st->end) && (st->end != st->pos)) {
            /* working from existing buffer, go to external input and try
             * again (have to update buffer pos as we may have consumed some) */
            st->pos = pos;
            tmppos = parser->next_in;
            tmpend = parser->next_in + parser->avail_in;
            JUMP(trans.endbuf.state, st->flags & ~FLAG_BUFFER, 0);
        } else {
            /* save buffer position if necessary */
            if (st->flags & FLAG_BUFFER) {
                assert((pos >= st->pos) && (pos <= st->end));
                st->pos = pos;
            } else {
                assert((pos >= parser->next_in)
                  && (pos <= parser->next_in + parser->avail_in));
                parser->next_in = pos;
                parser->avail_in = end - pos;
            }

            BUFFER();
            st->state = trans.endbuf.state;
            return MLPARSE_INPUT;
        }
    } else {
        /* couldn't find an endtag in time, its not a tag */
        JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
    }
    CANT_GET_HERE();

trans_return_input_label:
    if (st->flags & FLAG_BUFFER) {
            /* requesting more input, but more is available so point pos and
             * end to the right stuff and JUMP back in there... */
            if (end != st->end) {
                pos = st->pos = st->buf;
                end = st->end;
            } else {
                /* finished buffer */
                st->pos = st->end = st->buf;
                st->buf[0] = '\0';
                pos = parser->next_in;
                end = parser->next_in + parser->avail_in;
                st->flags &= ~FLAG_BUFFER;  /* no more buffering for now */
            }
            JUMP(trans.return_input.state, st->flags, 0);
    } else {
        parser->next_in = pos;
        parser->avail_in = end - pos;
        st->state = trans.return_input.state;
        return MLPARSE_INPUT;
    }
    CANT_GET_HERE();

/* the parser hasn't entered any structures */
toplevel_label:
    while (pos < end) {
        switch (*pos) {
        /* uppercase start to the word, could be an acronym */
        case ASCII_CASE_UPPER:
            if (strip) {
                word[0] = TOLOWER(*pos++);
            } else {
                word[0] = *pos++;
            }
            st->len = 1;
            goto acronym_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            word[0] = *pos++;
            st->len = 1;
            goto word_label;

        case ASCII_CASE_SPACE:
            if (!strip) {
                word[0] = *pos++;
                st->len = 1;
                goto space_label;
            } else {
                pos++;
            }
            break;

        case ASCII_CASE_EXTENDED:
        default:
            if (strip) {
                /* ignore junk as input */
                pos++;
            } else {
                word[0] = *pos++;
                st->len = 1;
                goto word_label;
            }
            break;

        case CASE_TAG(STATE_TOPLEVEL_ESC);
        case CASE_EREF(STATE_TOPLEVEL_ESC);
        }
    }
    RETURN_INPUT(STATE_TOPLEVEL);

/* toplevel state, except that the next character is escaped */
toplevel_esc_label:
    assert(pos < end);     /* escaped chars are always immediately available */
    switch (*pos) {
    case '<':
    case '&':
        if (strip) {
            /* ignore special characters */
            pos++;
        } else {
            word[0] = *pos++;
            st->len = 1;
            goto word_label;
        }
        /* fallthrough */
    default:
        /* let toplevel handle others */
        goto toplevel_label;
        break;
    }
    CANT_GET_HERE();

/* the parser is in whitespace */
space_label:
    assert(!strip);
    while (pos < end) {
        switch (*pos) {
        case ASCII_CASE_SPACE:
            PUSH(*pos++, MLPARSE_WHITESPACE, STATE_SPACE);
            break;

        default:
            /* anything else breaks the whitespace up */
            *length = st->len;
            RETURN(MLPARSE_WHITESPACE, STATE_TOPLEVEL);
            break;

        case CASE_TAG(STATE_SPACE_ESC);
        case CASE_EREF(STATE_SPACE_ESC);
        }
    }
    RETURN_INPUT(STATE_SPACE);
 
/* space state, except that the next character is escaped */
space_esc_label:
    assert(!strip);
    assert(pos < end);     /* escaped chars are always immediately available */
    switch (*pos) {
    case '<':
    case '&':
        /* break on special characters */
        *length = st->len;
        RETURN(MLPARSE_WHITESPACE, STATE_TOPLEVEL);

    default:
        /* let space handle others */
        goto space_label;
        break;
    }
    CANT_GET_HERE();

/* the parser is reading a normal word */
word_label:
    while (pos < end) {
        switch (*pos) {
        case CASE_ENDS:
            /* could be the end of a sentence */
            if (strip) {
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_WORD);
            }
            st->next_state = STATE_WORD;
            goto word_ends_label;

        case ASCII_CASE_UPPER:
            /* push character onto word */
            if (strip) {
                PUSH(TOLOWER(*pos++), MLPARSE_WORD, STATE_WORD);
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_WORD);
            }
            break;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            PUSH(*pos++, MLPARSE_WORD, STATE_WORD);
            break;

        case '-':
        default:
        case '\'':
            /* break if we get two consecutive punctuation marks */
            if (strip) {
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_WORD);
            }
            goto punc_label;

        case ASCII_CASE_SPACE:
            /* word ends */
            *length = st->len;
            RETURN(MLPARSE_WORD, STATE_TOPLEVEL);

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            if (strip) {
                /* ignore junk in words */
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_WORD);
            }
            break;
            
        case CASE_TAG(STATE_WORD_ESC);
        case CASE_EREF(STATE_WORD_ESC);
        }
    }
    RETURN_INPUT(STATE_WORD);

/* word state, except that the next character is escaped */
word_esc_label:
    assert(pos < end);     /* escaped chars are always immediately available */
    switch (*pos) {
    case '<':
    case '&':
        if (strip) {
            /* ignore special characters */
            pos++;
        } else {
            PUSH(*pos++, MLPARSE_WORD, STATE_WORD_ESC);
        }
        /* fallthrough */
    default:
        /* let word handle others */
        goto word_label;
        break;
    }
    CANT_GET_HERE();

/* have parsed an end-of-sentence character, now we see what comes next to
 * determine whether it really is the end of the sentence */
word_ends_label:
    while (pos < end) {
        switch (*pos) {
        case ASCII_CASE_SPACE:
            /* it was the end of a sentence */
            *length = st->len;
            RETURN(MLPARSE_END | MLPARSE_WORD, STATE_TOPLEVEL);

        case CASE_ENDS:
            /* have to take care of these since otherwise we'll just end up back
             * here */
            if (strip) {
                /* ignore special characters */
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_WORD_ENDS);
            }
            *length = st->len;
            RETURN(MLPARSE_WORD, STATE_TOPLEVEL);

        case CASE_TAG(STATE_WORD_ENDS_ESC);
        case CASE_EREF(STATE_WORD_ENDS_ESC);

        default:
            /* wasn't the end of a sentence */
            goto word_label;
        }
    }
    RETURN_INPUT(STATE_WORD_ENDS);

/* ends state, except that the next character is escaped */
word_ends_esc_label:
    assert(pos < end);     /* escaped chars are always immediately available */
    switch (*pos) {
    case '<':
    case '&':
        /* special characters indicate no end of sentence */
        if (strip) {
            pos++;
        } else {
            PUSH(*pos++, MLPARSE_WORD, STATE_WORD_ENDS_ESC);
        }
        *length = st->len;
        RETURN(MLPARSE_WORD, STATE_TOPLEVEL);

    default:
        /* let word_ends handle others */
        goto word_ends_label;
        break;
    }
    CANT_GET_HERE();

punc_label:
    while (pos < end) {
        switch (*pos) {
        case CASE_ENDS:
            /* could be the end of a sentence */
            st->next_state = STATE_PUNC;
            if (strip) {
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_PUNC);
            }
            goto punc_ends_label;

        case ASCII_CASE_UPPER:
            if (strip) {
                PUSH(TOLOWER(*pos++), MLPARSE_WORD, STATE_PUNC);
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_PUNC);
            }
            goto word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            PUSH(*pos++, MLPARSE_WORD, STATE_PUNC);
            goto word_label;

        default:
            /* break across two successive punctuation marks */
            if (strip) {
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_PUNC);
            }
            *length = st->len;
            RETURN(MLPARSE_WORD, STATE_TOPLEVEL);

        case ASCII_CASE_SPACE:
            /* word ends */
            *length = st->len;
            RETURN(MLPARSE_WORD, STATE_TOPLEVEL);

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            if (strip) {
                /* ignore junk in words */
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_PUNC);
            }
            break;

        case CASE_TAG(STATE_PUNC_ESC);
        case CASE_EREF(STATE_PUNC_ESC);
        }
    }
    RETURN_INPUT(STATE_PUNC);

/* punc state, except that the next character is escaped */
punc_esc_label:
    assert(pos < end);     /* escaped chars are always immediately available */
    switch (*pos) {
    case '<':
    case '&':
        /* break across two successive punctuation marks */
        *length = st->len;
        RETURN(MLPARSE_WORD, STATE_TOPLEVEL);

    default:
        /* let punc handle others */
        goto punc_label;
        break;
    }
    CANT_GET_HERE();

/* have parsed an end-of-sentence character, now we see what comes next to
 * determine whether it really is the end of the sentence */
punc_ends_label:
    while (pos < end) {
        switch (*pos) {
        case ASCII_CASE_SPACE:
            /* it was the end of a sentence */
            *length = st->len;
            RETURN(MLPARSE_END | MLPARSE_WORD, STATE_TOPLEVEL);

        case CASE_ENDS:
            /* have to take care of these since otherwise we'll just end up back
             * here */
            *length = st->len;
            RETURN(MLPARSE_WORD, STATE_TOPLEVEL);

        case CASE_TAG(STATE_PUNC_ENDS_ESC);
        case CASE_EREF(STATE_PUNC_ENDS_ESC);

        default:
            /* wasn't the end of a sentence */
            goto punc_label;
        }
    }
    RETURN_INPUT(STATE_PUNC_ENDS);

/* ends state, except that the next character is escaped */
punc_ends_esc_label:
    assert(pos < end);     /* escaped chars are always immediately available */
    switch (*pos) {
    case '<':
    case '&':
        /* need to break here because of two consecutive punctuation marks */
        /* special characters indicate no end of sentence */
        if (strip) {
            pos++;
        } else {
            PUSH(*pos++, MLPARSE_WORD, STATE_PUNC_ENDS_ESC);
        }
        *length = st->len;
        RETURN(MLPARSE_WORD, STATE_TOPLEVEL);

    default:
        /* let punc_ends handle others */
        goto punc_ends_label;
        break;
    }
    CANT_GET_HERE();

/* parsing what may be an acronym, expecting '.' next */
acronym_label:
    while (pos < end) {
        switch (*pos) {
        case '.':
            /* the acronym continues */
            if (strip) {
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_ACRONYM);
            }
            goto acronym_letter_label;

        case ASCII_CASE_SPACE:
            /* end of the word */
            *length = st->len;
            RETURN(MLPARSE_WORD, STATE_TOPLEVEL);

        case ASCII_CASE_UPPER:
            /* push character onto word */
            if (strip) {
                PUSH(TOLOWER(*pos++), MLPARSE_WORD, STATE_ACRONYM);
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_ACRONYM);
            }
            goto word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            PUSH(*pos++, MLPARSE_WORD, STATE_ACRONYM);
            goto word_label;

        case ASCII_CASE_EXTENDED:
        default:
            /* anything else, let the word state deal with it */
            goto word_label;

        case CASE_TAG(STATE_ACRONYM_ESC);
        case CASE_EREF(STATE_ACRONYM_ESC);
        }
    }
    RETURN_INPUT(STATE_ACRONYM);

/* acronym state, except that the next character is escaped */
acronym_esc_label:
    assert(pos < end);     /* escaped chars are always immediately available */
    switch (*pos) {
    case '<':
    case '&':
        /* ignore special characters */
        if (strip) {
            pos++;
        } else {
            PUSH(*pos++, MLPARSE_WORD, STATE_ACRONYM_ESC);
        }
        /* fallthrough */
    default:
        /* let acronym handle others */
        goto acronym_label;
        break;
    }
    CANT_GET_HERE();

/* parsing what may be an acronym, expecting a capital letter next */
acronym_letter_label:
    while (pos < end) {
        switch (*pos) {
        case ASCII_CASE_SPACE:
            /* end of the word, note no end of sentence */
            /* XXX: maybe check and make sure acronym is len > 1, else might be
             * end of sentence */
            *length = st->len;
            RETURN(MLPARSE_WORD, STATE_TOPLEVEL);

        case ASCII_CASE_UPPER:
            /* the acronym continues */
            if (strip) {
                PUSH(TOLOWER(*pos++), MLPARSE_WORD, STATE_ACRONYM_LETTER);  
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_ACRONYM_LETTER);  
            }
            /* note that the acronym continues even if we have to return a 
             * chunk of it */
            goto acronym_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* acronym ends, push character onto word */
            PUSH(*pos++, MLPARSE_WORD, STATE_ACRONYM_LETTER);  
            goto word_label;

        case ASCII_CASE_EXTENDED:
        default:
            /* anything else, let the punc state deal with it */
            goto punc_label;

        case CASE_TAG(STATE_ACRONYM_LETTER_ESC);
        case CASE_EREF(STATE_ACRONYM_LETTER_ESC);
        }
    }
    RETURN_INPUT(STATE_ACRONYM_LETTER);

/* acronym_letter state, except that the next character is escaped */
acronym_letter_esc_label:
    assert(pos < end);     /* escaped chars are always immediately available */
    switch (*pos) {
    case '<':
    case '&':
        if (strip) {
            /* ignore special characters */
            pos++;
        } else {

            PUSH(*pos++, MLPARSE_WORD, STATE_ACRONYM_LETTER_ESC);
        }
        /* fallthrough */
    default:
        /* let acronym_letter handle others */
        goto acronym_letter_label;
        break;
    }
    CANT_GET_HERE();

/* have just recieved an open tag, need to establish whether its really a valid 
 * tag */
tag_peek_first_label:
    while (tmppos < tmpend) {
        switch (*tmppos) {
        default:
        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_SPACE:
            /* can't have space or other junk directly after tag open, 
             * its not a tag */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);

        case '!':
            /* its probably a declaration */
            st->count++;
            st->tagbuf[0] = *tmppos++;
            st->tagbuflen = 1;
            goto decl_peek_label;

        case '<':
        case '>':
            /* let tag_peek_label handle these */
            st->tagbuflen = 0;
            goto tag_peek_label;

        case ASCII_CASE_UPPER:
            st->tagbuf[0] = ASCII_TOLOWER(*tmppos);
            st->tagbuflen = 1;
            tmppos++;
            st->count++;
            goto tag_peek_name_label;

        /* xml allows these characters in tags */
        case '.': case '-': case '_': case ':':
        case '/': case '?': 
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* pretty much anything else qualifies, loosely speaking */
            st->tagbuf[0] = *tmppos;
            st->tagbuflen = 1;
            tmppos++;
            st->count++;
            goto tag_peek_name_label;
        }
    }
    ENDBUF(st->lookahead, STATE_TAG_PEEK_FIRST);

tag_peek_name_label:
    while (tmppos < tmpend) {
        switch (*tmppos) {
        default:
        case ASCII_CASE_EXTENDED:
            /* don't accept junk in tag names */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);

        case '<':
        case '>':
            /* let tag_peek_label handle these */
            goto tag_peek_label;

        case '/':
            /* allow / so we recognise self-ending tags, but don't count as part
             * of the name */
            tmppos++;
            break;

        case ASCII_CASE_SPACE:
            /* end of tag name */
            tmppos++;
            st->count++;
            goto tag_peek_label;

        case ASCII_CASE_UPPER:
            /* pretty much anything else qualifies, loosely speaking */
            if (st->tagbuflen < st->wordlen) {
                st->tagbuf[st->tagbuflen++] = ASCII_TOLOWER(*tmppos);
                tmppos++;
                st->count++;
            } else {
                tmppos++;
                st->count++;
                goto tag_peek_name_cont_label;
            }
            break;

        /* xml allows these characters in tags */
        case '.': case '-': case '_': case ':':
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* pretty much anything else qualifies, loosely speaking */
            if (st->tagbuflen < st->wordlen) {
                st->tagbuf[st->tagbuflen++] = *tmppos;
                tmppos++;
                st->count++;
            } else {
                tmppos++;
                st->count++;
                goto tag_peek_name_cont_label;
            }
            break;
        }
    }
    ENDBUF(st->lookahead, STATE_TAG_PEEK_NAME);

tag_peek_name_cont_label:
    while (tmppos < tmpend) {
        switch (*tmppos) {
        default:
        case ASCII_CASE_EXTENDED:
            /* don't accept junk in tag names */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);

        case '<':
        case '>':
            /* let tag_peek_label handle these */
            goto tag_peek_label;

        case ASCII_CASE_SPACE:
            /* end of tag name */
            tmppos++;
            st->count++;
            goto tag_peek_label;

        case ASCII_CASE_UPPER:
        /* xml allows these characters in tags */
        case '.': case '-': case '_': case ':':
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* pretty much anything else qualifies, loosely speaking */
            tmppos++;
            st->count++;
            break;
        }
    }
    ENDBUF(st->lookahead, STATE_TAG_PEEK_NAME_CONT);

/* have recieved an open tag, need to establish whether its really a valid 
 * tag */
tag_peek_label:
    while ((tmppos < tmpend) && (st->count < st->lookahead)) {
        switch (*tmppos) {
        case '<':
            /* can't have another tag open character, its not a tag */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);

        case '>':
            /* found end tag, need to return entity previously being parsed */
            tmppos++;
            RETURN_IF_STORED(STATE_TAG_ENTRY);
            /* otherwise (nothing stored) */
            goto tag_entry_label;

        default:
            /* ignore everything else */
            st->count++;
            tmppos++;
            break;
        }
    }
    ENDBUF(st->lookahead, STATE_TAG_PEEK);

/* parsing an open tag */
tag_entry_label:
    /* skip tag that we've already parsed */
    memcpy(word, st->tagbuf, st->tagbuflen);
    pos += st->tagbuflen + 1; /* + 1 for the open tag */

    /* need to decide where to go next */
    while (1) {
        switch (*pos) {
        case '>':
        case ASCII_CASE_SPACE:
            *length = st->tagbuflen;
            RETURN(MLPARSE_TAG, STATE_TAG);

        case '/':
            st->next_state = STATE_TAG;
            pos++;
            *length = st->tagbuflen;
            RETURN(MLPARSE_TAG, STATE_TAG_SELFEND);

        default:
            st->len = 0;
            *length = st->tagbuflen;
            RETURN(MLPARSE_TAG, STATE_TAG_NAME_CONT);
        }
    }

/* parsing a tag name after having to break it into pieces */
tag_name_cont_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case ASCII_CASE_SPACE:
            /* end of tag */
            *length = st->len;
            RETURN(MLPARSE_TAG, STATE_TAG);

        case '>':
            /* end of tag */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_TAG, STATE_TOPLEVEL);

        case ASCII_CASE_UPPER:
            /* add character to tag name */
            PUSH(ASCII_TOLOWER(*pos++), MLPARSE_TAG, STATE_TAG_NAME_CONT);
            break;

            /* fallthrough */
        /* xml allows these characters in tags */
        case '.': case '-': case '_': case ':':
            /* fallthrough */
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* add character to tag name */
            PUSH(*pos++, MLPARSE_TAG, STATE_TAG_NAME_CONT);
            break;

        case '/':
            /* may be self-ending */
            st->next_state = STATE_TAG_NAME_CONT;
            pos++;
            goto tag_name_selfend_label;

        case '<':
            CANT_GET_HERE();

        default:
        case ASCII_CASE_EXTENDED:
            /* ignore anything else */
            pos++;
            break;
        }
    }

/* parsing what may be a self-ending tag with only a name */
tag_name_selfend_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case ASCII_CASE_SPACE:
        case '>':
            /* name ended, return it and let self-end state handle the rest */
            *length = st->len;
            RETURN(MLPARSE_TAG, STATE_TAG_SELFEND);

        default:
            /* didn't self-end, go back to where we came from */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
        }
    }

/* parsing a tag after the name */
tag_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* end of tag */
            pos++;
            goto toplevel_label;

        case '/':
            /* could be a self-ending tag */
            pos++;
            goto tag_selfend_label;

        case ASCII_CASE_UPPER:
            /* add character to tag name */
            word[0] = ASCII_TOLOWER(*pos++);
            st->len = 1;
            goto param_label;

        /* xml allows these characters in names */
        case '.': case '-': case '_': case ':':
            /* fallthrough */
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* add character to tag name */
            word[0] = *pos++;
            st->len = 1;
            goto param_label;

        case '\'':
            /* start of a single-quote-delimited anonymous parameter value */
            pos++;
            goto pval_quot_label;

        case '"':
            /* start of a double-quote-delimited anonymous parameter value */
            pos++;
            goto pval_dquot_label;

        case ASCII_CASE_EXTENDED:
        default:
            /* ignore everything else */
            pos++;
            break;
        }
    }

/* parsing a tag that may be self-ending */
tag_selfend_label:
    while (1) {
        unsigned int tmp;

        assert(pos < end);
        switch (*pos) {
        case '>':
            /* self-ended, copy tag name from buffer into word */
            pos++;
            assert(st->tagbuflen <= st->wordlen);
            word[0] = '/';

            if (st->tagbuflen == st->wordlen) {
                /* need to exclude last character */
                tmp = st->wordlen;
            } else {
                tmp = st->tagbuflen + 1;
            }
            memcpy(&word[1], st->tagbuf, tmp);
            *length = tmp;
            RETURN(MLPARSE_TAG, STATE_TOPLEVEL);
            break;

        case ASCII_CASE_SPACE:
            /* ignore */
            pos++;
            break;

        default:
            /* not self-ending, let tag handle it */
            goto tag_label;
        }
    }

/* parsing a parameter name */
param_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* tag and parameter ended */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAM, STATE_TOPLEVEL);

        case '=':
            /* start of a parameter value */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAM, STATE_PVAL_FIRST);

        case ASCII_CASE_SPACE:
            /* possibly the start of a parameter value */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAM, STATE_PARAM_EQ);

        case ASCII_CASE_UPPER:
            /* add character to tag name */
            PUSH(ASCII_TOLOWER(*pos++), MLPARSE_PARAM, STATE_PARAM);
            break;

        /* xml allows these characters in names */
        case '.': case '-': case '_': case ':':
            /* fallthrough */
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* add character to tag name */
            PUSH(*pos++, MLPARSE_PARAM, STATE_PARAM);
            break;

        case ASCII_CASE_EXTENDED:
        default:
            /* ignore junk */
            pos++;
            break;
        }
    }

/* have parsed a parameter name, waiting to see if it has a value */
param_eq_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* tag ends */
            pos++;
            goto toplevel_label;

        case '=':
            pos++;
            goto pval_first_label;

        case '\'':
            /* start of a single-quote-delimited anonymous parameter value */
            pos++;
            goto pval_quot_label;

        case '"':
            /* start of a double-quote-delimited anonymous parameter value */
            pos++;
            goto pval_dquot_label;

        case ASCII_CASE_UPPER:
            /* start of a different parameter */
            word[0] = ASCII_TOLOWER(*pos++);
            st->len = 1;
            goto param_label;

        /* xml allows these characters in names */
        case '.': case '-': case '_': case ':':
            /* fallthrough */
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* start of a different parameter */
            word[0] = *pos++;
            st->len = 1;
            goto param_label;

        case ASCII_CASE_EXTENDED:
        default:
        case ASCII_CASE_SPACE:
            /* ignore */
            pos++;
            break;
        }
    }

/* parsing the first character of a parameter value */
pval_first_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* tag and parameter (empty) ended */
            pos++;
            *length = 0;
            RETURN(MLPARSE_PARAMVAL, STATE_TOPLEVEL);

        case '\'':
            /* start of a single-quote-delimited parameter value */
            pos++;
            goto pval_quot_label;

        case '"':
            /* start of a double-quote-delimited parameter value */
            pos++;
            goto pval_dquot_label;

        case ASCII_CASE_UPPER:
            /* start of a whitespace delimited parameter value */
            if (strip) {
                word[0] = TOLOWER(*pos++);
            } else {
                word[0] = *pos++;
            }
            st->len = 1;
            goto pval_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* start of a whitespace delimited parameter value */
            word[0] = *pos++;
            st->len = 1;
            goto pval_label;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_SPACE:
        default:
            /* ignore */
            pos++;
            break;
        }
    }

/* parsing a whitespace delimited parameter value */
pval_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* tag and parameter end */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TOPLEVEL);

        case '-':
        case '\'':
        default:
            /* break after two successive punctuation marks */
            if (strip) {
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL);
            }
            goto pval_punc_label;

        case ASCII_CASE_UPPER:
            /* push character onto pval */
            if (strip) {
                PUSH(TOLOWER(*pos++), MLPARSE_PARAMVAL, STATE_PVAL);
            } else {
                PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL);
            }
            break;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto pval */
            PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL);
            break;

        case ASCII_CASE_SPACE:
            /* end of the pval */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TAG);
 
        case '/':
            /* could be the end of a self-ending tag */
            pos++;
            st->next_state = STATE_PVAL;
            goto pval_selfend_label;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:   
            /* ignore junk */
            pos++;
            break;
        }
    }

/* parsing a whitespace delimited parameter value after in-word punctuation */
pval_punc_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* tag and parameter value end */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TOPLEVEL);

        default:
        case '-':
        case '\'':
            /* end of the pval */
            if (strip) {
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_PUNC);
            }
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_PVAL);

        case ASCII_CASE_UPPER:
            /* push character onto word */
            if (strip) {
                PUSH(TOLOWER(*pos++), MLPARSE_PARAMVAL, STATE_PVAL_PUNC);
            } else {
                PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_PUNC);
            }
            goto pval_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_PUNC);
            goto pval_label;

        case ASCII_CASE_SPACE:
            /* end of the pval */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TAG);

        case '/':
            /* could be the end of a self-ending tag */
            pos++;
            st->next_state = STATE_PVAL_PUNC;
            goto pval_selfend_label;
 
        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore junk */
            pos++;
            break;
        }
    }

/* parsing a whitespace-delimited parameter value after we received a / */
pval_selfend_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* pval ended, tag self-ended */
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TAG_SELFEND);

        default:
            /* not self-ending, go back to where we came from */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
        }
    }

/* parsing a single-quote-delimited parameter value */
pval_quot_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* end of tag */
            pos++;
            goto toplevel_label;

        case '\'':
            /* end single quote (apostrophe) ends pval */
            pos++;
            goto tag_label;

        case ASCII_CASE_UPPER:
            /* push character onto pval */
            if (strip) {
                word[0] = TOLOWER(*pos++);
            } else {
                word[0] = *pos++;
            }
            st->len = 1;
            goto pval_quot_word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto pval */
            word[0] = *pos++;
            st->len = 1;
            goto pval_quot_word_label;

        case ASCII_CASE_SPACE:
            if (!strip) {
                word[0] = *pos++;
                st->len = 1;
                goto pval_quot_space_label;
            } else {
                pos++;
            }
            break;

        default:
            if (!strip) {
                word[0] = *pos++;
                st->len = 1;
                goto pval_quot_word_label;
            } else {
                pos++;
            }
            break;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore */
            pos++;
            break;

        case CASE_EREF(STATE_PVAL_QUOT_ESC);
        }
    }

/* same as pval_quote, but next character is escaped */
pval_quot_esc_label:
    while (1) {

        /* catch non-contiguity that entity references may have introduced (by
         * forcing the use of the internal buffer when the rest of the 
         * contiguous data is in the provided buffer */
        assert(pos < end);
        tmppos = pos++;
        if (pos == end) {
            assert(pos == st->end);
            assert(st->flags & FLAG_BUFFER);
            st->flags &= ~FLAG_BUFFER;
            st->end = st->pos = st->buf;
            pos = parser->next_in;
            end = parser->next_in + parser->avail_in;
        }

        switch (*tmppos) {
        case '&':
        case '>':
        case '\'':
            if (strip) {
                /* ignore */
            } else {
                PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_ESC);
            }
            break;

        case ASCII_CASE_UPPER:
            /* push character onto pval */
            if (strip) {
                word[0] = TOLOWER(*tmppos);
            } else {
                word[0] = *tmppos;
            }
            st->len = 1;
            goto pval_quot_word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto pval */
            word[0] = *tmppos;
            st->len = 1;
            goto pval_quot_word_label;

        case ASCII_CASE_SPACE:
            if (!strip) {
                word[0] = *tmppos;
                st->len = 1;
                goto pval_quot_space_label;
            }
            break;

        default:
            if (!strip) {
                word[0] = *tmppos;
                st->len = 1;
                goto pval_quot_word_label;
            }
            break;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore */
            break;
        }

        /* now return to normal processing */
        goto pval_quot_label;
    }

/* in a word in a single-quote-delimited parameter value */
pval_quot_word_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* end of tag */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TOPLEVEL);

        case '\'':
            /* end single quote (apostrophe) ends pval */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TAG);

        case '-':
        default:
            /* break after two successive punctuation characters in a word */
            if (strip) {
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_WORD);
            }
            goto pval_quot_punc_label;

        case ASCII_CASE_UPPER:
            /* push character onto pval */
            if (strip) {
                PUSH(TOLOWER(*pos++), MLPARSE_PARAMVAL, STATE_PVAL_QUOT_WORD);
            } else {
                PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_WORD);
            }
            goto pval_quot_word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto pval */
            PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_WORD);
            goto pval_quot_word_label;

        case ASCII_CASE_SPACE:
            /* end of pval */
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_PVAL_QUOT);

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore */
            pos++;
            break;

        case CASE_EREF(STATE_PVAL_QUOT_WORD_ESC);
        }
    }

/* same as pval_quot_word, but next character is escaped */
pval_quot_word_esc_label:
    while (1) {

        /* catch non-contiguity that entity references may have introduced (by
         * forcing the use of the internal buffer when the rest of the 
         * contiguous data is in the provided buffer */
        assert(pos < end);
        tmppos = pos++;
        if (pos == end) {
            assert(pos == st->end);
            assert(st->flags & FLAG_BUFFER);
            st->flags &= ~FLAG_BUFFER;
            st->end = st->pos = st->buf;
            pos = parser->next_in;
            end = parser->next_in + parser->avail_in;
        }

        switch (*tmppos) {
        case '&':
        case '>':
        case '\'':
            if (strip) {
                /* ignore */
            } else {
                PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_WORD_ESC);
            }
            break;

        default:
        case '-':
            /* break after two successive punctuation characters in a word */
            if (strip) {
                /* ignore */
            } else {
                PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_WORD_ESC);
            }
            goto pval_quot_punc_label;

        case ASCII_CASE_UPPER:
            /* push character onto pval */
            if (strip) {
                PUSH(TOLOWER(*tmppos), MLPARSE_PARAMVAL, 
                  STATE_PVAL_QUOT_WORD_ESC);
            } else {
                PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_WORD_ESC);
            }
            break;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto pval */
            PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_WORD_ESC);
            break;

        case ASCII_CASE_SPACE:
            /* end of pval */
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_PVAL_QUOT);

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore */
            break;
        }

        /* now return to normal processing */
        goto pval_quot_word_label;
    }
    CANT_GET_HERE();

/* in a word after punctuation in a single-quote-delimited parameter value */
pval_quot_punc_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* end of tag */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TOPLEVEL);

        case '\'':
            /* end single quote (apostrophe) ends pval */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TAG);

        default:
        case '-':
            /* end of pval */
            if (strip) {
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_PUNC);
            }
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_PVAL_QUOT);

        case ASCII_CASE_UPPER:
            /* push character onto pval */
            if (strip) {
                PUSH(TOLOWER(*pos++), MLPARSE_PARAMVAL, STATE_PVAL_QUOT_PUNC);
            } else {
                PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_PUNC);
            }
            goto pval_quot_word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto pval */
            PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_PUNC);
            goto pval_quot_word_label;

        case ASCII_CASE_SPACE:
            /* end of pval */
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_PVAL_QUOT);

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore */
            pos++;
            break;

        case CASE_EREF(STATE_PVAL_QUOT_PUNC_ESC);
        }
    }

/* same as pval_quot_punc, but next character is escaped */
pval_quot_punc_esc_label:
    while (1) {

        /* catch non-contiguity that entity references may have introduced (by
         * forcing the use of the internal buffer when the rest of the 
         * contiguous data is in the provided buffer */
        assert(pos < end);
        tmppos = pos++;
        if (pos == end) {
            assert(pos == st->end);
            assert(st->flags & FLAG_BUFFER);
            st->flags &= ~FLAG_BUFFER;
            st->end = st->pos = st->buf;
            pos = parser->next_in;
            end = parser->next_in + parser->avail_in;
        }

        switch (*tmppos) {
        case '&':
        case '>':
        case '\'':
        default:
        case '-':
            /* end of pval */
            if (strip) {
                /* ignore */
            } else {
                PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_PUNC_ESC);
            }
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_PVAL_QUOT);

        case ASCII_CASE_UPPER:
            /* push character onto pval */
            if (strip) {
                PUSH(TOLOWER(*tmppos), MLPARSE_PARAMVAL, 
                  STATE_PVAL_QUOT_PUNC_ESC);
            } else {
                PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_PUNC_ESC);
            }
            goto pval_quot_word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto pval */
            PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_PUNC_ESC);
            goto pval_quot_word_label;

        case ASCII_CASE_SPACE:
            /* end of pval */
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_PVAL_QUOT);

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore */
            break;
        }

        /* now return to normal processing */
        goto pval_quot_punc_label;
    }
    CANT_GET_HERE();

pval_quot_space_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* end of tag */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TOPLEVEL);

        case '\'':
            /* end single quote (apostrophe) ends pval */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TAG);

        default:
            /* end of space */
            *length = st->len;
            RETURN(MLPARSE_WHITESPACE, STATE_PVAL_QUOT);

        case ASCII_CASE_SPACE:
            /* space continues */
            PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_SPACE);
            break;

        case ASCII_CASE_CONTROL:
            /* ignore */
            pos++;
            break;

        case CASE_EREF(STATE_PVAL_QUOT_SPACE_ESC);
        }
    }

pval_quot_space_esc_label:
    while (1) {

        /* catch non-contiguity that entity references may have introduced (by
         * forcing the use of the internal buffer when the rest of the 
         * contiguous data is in the provided buffer */
        assert(pos < end);
        tmppos = pos++;
        if (pos == end) {
            assert(pos == st->end);
            assert(st->flags & FLAG_BUFFER);
            st->flags &= ~FLAG_BUFFER;
            st->end = st->pos = st->buf;
            pos = parser->next_in;
            end = parser->next_in + parser->avail_in;
        }

        switch (*tmppos) {
        case '&':
        case '>':
        case '\'':
        default:
            /* end of space */
            pos--;  /* dodgy, but we need to preserve the character */
            *length = st->len;
            RETURN(MLPARSE_WHITESPACE, STATE_PVAL_QUOT_ESC);

        case ASCII_CASE_SPACE:
            /* space continues */
            PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_QUOT_SPACE_ESC);
            break;

        case ASCII_CASE_CONTROL:
            /* ignore */
            break;
        }

        /* now return to normal processing */
        goto pval_quot_space_label;
    }
    CANT_GET_HERE();

/* parsing a double-quote-delimited parameter value */
pval_dquot_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* end of tag */
            pos++;
            goto toplevel_label;

        case '"':
            /* end double quote ends pval */
            pos++;
            goto tag_label;

        case ASCII_CASE_UPPER:
            /* push character onto pval */
            if (strip) {
                word[0] = TOLOWER(*pos++);
            } else {
                word[0] = *pos++;
            }
            st->len = 1;
            goto pval_dquot_word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto pval */
            word[0] = *pos++;
            st->len = 1;
            goto pval_dquot_word_label;

        case ASCII_CASE_SPACE:
            if (strip) {
                pos++;
            } else {
                word[0] = *pos++;
                st->len = 1;
                goto pval_dquot_space_label;
            }
            break;

        default:
            if (strip) {
                pos++;
            } else {
                word[0] = *pos++;
                st->len = 1;
                goto pval_dquot_word_label;
            }
            break;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore */
            pos++;
            break;

        case CASE_EREF(STATE_PVAL_DQUOT_ESC);
        }
    }

/* same as pval_dquot, but next character is escaped */
pval_dquot_esc_label:
    while (1) {

        /* catch non-contiguity that entity references may have introduced (by
         * forcing the use of the internal buffer when the rest of the 
         * contiguous data is in the provided buffer */
        assert(pos < end);
        tmppos = pos++;
        if (pos == end) {
            assert(pos == st->end);
            assert(st->flags & FLAG_BUFFER);
            st->flags &= ~FLAG_BUFFER;
            st->end = st->pos = st->buf;
            pos = parser->next_in;
            end = parser->next_in + parser->avail_in;
        }

        switch (*tmppos) {
        case '&':
        case '>':
        case '"':
        default:
            if (strip) {
                /* ignore */
            } else {
                word[0] = *tmppos;
                st->len = 1;
                goto pval_dquot_word_label;
            }
            break;

        case ASCII_CASE_UPPER:
            /* push character onto pval */
            if (strip) {
                word[0] = TOLOWER(*tmppos);
            } else {
                word[0] = *tmppos;
            }
            st->len = 1;
            goto pval_dquot_word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto pval */
            word[0] = *tmppos;
            st->len = 1;
            goto pval_dquot_word_label;

        case ASCII_CASE_SPACE:
            if (strip) {
                /* ignore */
            } else {
                word[0] = *tmppos;
                st->len = 1;
                goto pval_dquot_space_label;
            }
            break;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore */
            break;
        }

        /* now resume normal processing */
        goto pval_dquot_label;
    }

/* in a word in a double-quote-delimited parameter value */
pval_dquot_word_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* end of tag */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TOPLEVEL);

        case '"':
            /* end double quote ends pval */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TAG);

        case '-':
        default:
            /* two successive punctuation characters end a word */
            if (strip) {
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_WORD);
            }
            goto pval_dquot_punc_label;

        case ASCII_CASE_UPPER:
            /* push character onto pval */
            if (strip) {
                PUSH(TOLOWER(*pos++), MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_WORD);
            } else {
                PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_WORD);
            }
            goto pval_dquot_word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto pval */
            PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_WORD);
            goto pval_dquot_word_label;

        case ASCII_CASE_SPACE:
            /* end of pval */
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_PVAL_DQUOT);

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore */
            pos++;
            break;

        case CASE_EREF(STATE_PVAL_DQUOT_WORD_ESC);
        }
    }

/* same as pval_dquot_word, but next character is escaped */
pval_dquot_word_esc_label:
    while (1) {

        /* catch non-contiguity that entity references may have introduced (by
         * forcing the use of the internal buffer when the rest of the 
         * contiguous data is in the provided buffer */
        assert(pos < end);
        tmppos = pos++;
        if (pos == end) {
            assert(pos == st->end);
            assert(st->flags & FLAG_BUFFER);
            st->flags &= ~FLAG_BUFFER;
            st->end = st->pos = st->buf;
            pos = parser->next_in;
            end = parser->next_in + parser->avail_in;
        }

        switch (*tmppos) {
        case '&':
        case '>':
        case '"':
        case '-':
        default:
            if (strip) {
                /* ignore */
            } else {
                PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_WORD_ESC);
            }
            goto pval_dquot_punc_label;

        case ASCII_CASE_UPPER:
            /* push character onto pval */
            if (strip) {
                PUSH(TOLOWER(*tmppos), MLPARSE_PARAMVAL, 
                  STATE_PVAL_DQUOT_WORD_ESC);
            } else {
                PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_WORD_ESC);
            }
            goto pval_dquot_word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto pval */
            PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_WORD_ESC);
            goto pval_dquot_word_label;

        case ASCII_CASE_SPACE:
            /* end of pval */
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_PVAL_DQUOT);

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore */
            break;
        }

        /* now resume normal processing */
        goto pval_dquot_word_label;
    }

/* in a word after punctuation in a double-quote-delimited parameter value */
pval_dquot_punc_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* end of tag */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TOPLEVEL);

        case '"':
            /* end double quote ends pval */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TAG);

        case '-':
        default:
            /* two successive punctuation marks end the word */
            if (strip) {
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_PUNC);
            }
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_PVAL_DQUOT);

        case ASCII_CASE_SPACE:
            /* end of word */
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_PVAL_DQUOT);

        case ASCII_CASE_UPPER:
            /* push character onto pval */
            if (strip) {
                PUSH(TOLOWER(*pos++), MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_PUNC);
            } else {
                PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_PUNC);
            }
            goto pval_dquot_word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto pval */
            PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_PUNC);
            goto pval_dquot_word_label;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore */
            pos++;
            break;

        case CASE_EREF(STATE_PVAL_DQUOT_PUNC_ESC);
        }
    }

/* same as pval_dquot_punc, but next character is escaped */
pval_dquot_punc_esc_label:
    while (1) {

        /* catch non-contiguity that entity references may have introduced (by
         * forcing the use of the internal buffer when the rest of the 
         * contiguous data is in the provided buffer */
        assert(pos < end);
        tmppos = pos++;
        if (pos == end) {
            assert(pos == st->end);
            assert(st->flags & FLAG_BUFFER);
            st->flags &= ~FLAG_BUFFER;
            st->end = st->pos = st->buf;
            pos = parser->next_in;
            end = parser->next_in + parser->avail_in;
        }

        switch (*tmppos) {
        case '&':
        case '>':
        case '"':
        case '-':
        default:
            if (strip) {
                /* ignore */
            } else {
                PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_PUNC_ESC);
            }
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_PVAL_DQUOT);

        case ASCII_CASE_SPACE:
            /* end of word */
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_PVAL_DQUOT);

        case ASCII_CASE_UPPER:
            /* push character onto pval */
            if (strip) {
                PUSH(TOLOWER(*tmppos), MLPARSE_PARAMVAL, 
                  STATE_PVAL_DQUOT_PUNC_ESC);
            } else {
                PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_PUNC_ESC);
            }
            goto pval_dquot_word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto pval */
            PUSH(*tmppos, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_PUNC_ESC);
            goto pval_dquot_word_label;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore */
            break;
        }

        /* now resume normal processing */
        goto pval_dquot_punc_label;
    }

pval_dquot_space_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            /* end of tag */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TOPLEVEL);

        case '\'':
            /* end single quote (apostrophe) ends pval */
            pos++;
            *length = st->len;
            RETURN(MLPARSE_PARAMVAL, STATE_TAG);

        default:
            /* end of space */
            *length = st->len;
            RETURN(MLPARSE_WHITESPACE, STATE_PVAL_DQUOT);

        case ASCII_CASE_SPACE:
            /* space continues */
            PUSH(*pos++, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_SPACE);
            break;

        case ASCII_CASE_CONTROL:
            /* ignore */
            pos++;
            break;

        case CASE_EREF(STATE_PVAL_DQUOT_SPACE_ESC);
        }
    }

pval_dquot_space_esc_label:
    while (1) {

        /* catch non-contiguity that entity references may have introduced (by
         * forcing the use of the internal buffer when the rest of the 
         * contiguous data is in the provided buffer */
        assert(pos < end);
        tmppos = pos++;
        if (pos == end) {
            assert(pos == st->end);
            assert(st->flags & FLAG_BUFFER);
            st->flags &= ~FLAG_BUFFER;
            st->end = st->pos = st->buf;
            pos = parser->next_in;
            end = parser->next_in + parser->avail_in;
        }

        switch (*tmppos) {
        case '&':
        case '>':
        case '\'':
        default:
            /* end of space */
            pos--;  /* dodgy, but we need to preserve the character */
            *length = st->len;
            RETURN(MLPARSE_WHITESPACE, STATE_PVAL_DQUOT_ESC);

        case ASCII_CASE_SPACE:
            /* space continues */
            PUSH(*tmppos++, MLPARSE_PARAMVAL, STATE_PVAL_DQUOT_SPACE_ESC);
            break;

        case ASCII_CASE_CONTROL:
            /* ignore */
            break;
        }

        /* now resume normal processing */
        goto pval_dquot_space_label;
    }

/* parse the first character of an entity reference */
eref_peek_first_label:
    while (tmppos < tmpend) {
        switch (*tmppos) {
        /* xml allows these characters in names */
        case ASCII_CASE_UPPER:
        case '.': case '-': case '_': case ':':
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            st->erefbuf[0] = *tmppos++;
            st->count = 1;
            goto eref_peek_label;

        case '#':
            /* allowed, as it denotes a numeric character reference */
            tmppos++;
            goto eref_num_peek_first_label;

        default:
        case ASCII_CASE_EXTENDED:
            /* shouldn't get anything else */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
        }
    }
    ENDBUF(MAXENTITYREF, STATE_EREF_PEEK_FIRST);

/* parse the first character of an entity reference */
eref_num_peek_first_label:
    while (tmppos < tmpend) {
        switch (*tmppos) {
        /* xml allows these characters in names */
        case ASCII_CASE_DIGIT:
            st->erefbuf[0] = *tmppos++;
            st->count = 1;
            goto eref_num_peek_label;

        case 'X':
        case 'x':
            /* indicates start of hexadecimal numeric character reference */
            tmppos++;
            st->count = 0;
            goto eref_hex_peek_label;

        default:
            /* shouldn't get anything else */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
        }
    }
    ENDBUF(MAXENTITYREF, STATE_EREF_NUM_PEEK_FIRST);

/* parse a numeric entity reference */
eref_num_peek_label:
    while ((tmppos < tmpend) && (st->count < MAXENTITYREF)) {
        char *numend;
        long int num;

        switch (*tmppos) {
        /* xml allows these characters in names */
        case ASCII_CASE_DIGIT:
            st->erefbuf[st->count++] = *tmppos++;
            break;

        case ';':
            /* XXX: note that we don't recognise references to the extended
             * character set at the moment, this requires i18n of the whole
             * parser */
            tmppos++;
            st->erefbuf[st->count] = '\0';
            if (((num = strtol(st->erefbuf, &numend, 10)) >= 0) 
              && (*numend == '\0') && (num < UCHAR_MAX)) {
                /* fiddle the buffers to replace text */
                if ((st->flags & FLAG_BUFFER) 
                  /* handle case where part of eref is in the buffer */
                  && ((tmppos >= st->pos) && (tmppos <= st->end))) {
                    /* working in the buffer */
                    pos += st->count + 1;  /* + 1 for opening '#' */
                } else {
                    /* nothing should be buffered after this, put reference char
                     * in buffer alone, saving next_in position */
                    parser->next_in = tmppos;
                    parser->avail_in = tmpend - parser->next_in;
                    pos = st->pos = st->buf;
                    end = st->end = st->buf + 1;
                    st->flags |= FLAG_BUFFER;
                }

                /* XXX: semi-dodgy cast to allow changing of the buffer */
                numend = (char *) pos;
                *numend = (char) num;

                JUMP(st->next_state, (st->flags & ~FLAG_BUFFER), 0);
            }
            /* fallthrough for failed conversions */

        default:
            /* shouldn't get anything else */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
        }
    }
    ENDBUF(MAXENTITYREF, STATE_EREF_NUM_PEEK);

/* parse a hexadecimal numeric entity reference */
eref_hex_peek_label:
    while ((tmppos < tmpend) && (st->count < MAXENTITYREF)) {
        long int num;
        char *numend;

        switch (*tmppos) {
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
            st->erefbuf[st->count++] = ASCII_TOLOWER(*tmppos++);
            break;

        case ASCII_CASE_DIGIT:
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
            st->erefbuf[st->count++] = *tmppos++;
            break;

        case ';':
            /* XXX: note that we don't recognise references to the extended
             * character set at the moment, this requires i18n of the whole
             * parser */
            tmppos++;
            st->erefbuf[st->count] = '\0';
            if (((num = strtol(st->erefbuf, &numend, 16)) >= 0) 
              && (*numend == '\0') && (num < UCHAR_MAX)) {
                /* fiddle the buffers to replace text */
                if ((st->flags & FLAG_BUFFER) 
                  /* handle case where part of eref is in the buffer */
                  && ((tmppos >= st->pos) && (tmppos <= st->end))) {
                    /* working in the buffer */
                    pos += st->count + 2;  /* + 1 for opening '#x' */
                } else {
                    /* nothing should be buffered after this, put reference char
                     * in buffer alone, saving next_in position */
                    parser->next_in = tmppos;
                    parser->avail_in = tmpend - parser->next_in;
                    pos = st->pos = st->buf;
                    end = st->end = st->buf + 1;
                    st->flags |= FLAG_BUFFER;
                }

                /* XXX: semi-dodgy cast to allow changing of the buffer */
                numend = (char *) pos;
                *numend = (char) num;

                JUMP(st->next_state, (st->flags & ~FLAG_BUFFER), 0);
            }
            /* fallthrough for failed conversions */

        default:
            /* shouldn't get anything else */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
        }
    }
    ENDBUF(MAXENTITYREF, STATE_EREF_HEX_PEEK);

/* have recieved an open entity reference, need to establish whether its 
 * valid */
eref_peek_label:
    while ((tmppos < tmpend) && (st->count < MAXENTITYREF)) {
        int c;

        switch (*tmppos) {
        /* xml allows these characters in names */
        case ASCII_CASE_UPPER:
        case '.': case '-': case '_': case ':':
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            st->erefbuf[st->count++] = *tmppos++;
            break;

        case ';':
            /* match entity reference */
            tmppos++;
            st->erefbuf[st->count] = '\0';
            switch (st->count) {
            case 2:
                /* lt, gt */
                if (!str_cmp("lt", st->erefbuf)) {
                    c = '<';
                } else if (!str_cmp("gt", st->erefbuf)) {
                    c = '>';
                } else {
                    c = -1;
                }
                break;

            case 3:
                /* amp */
                if (!str_cmp("amp", st->erefbuf)) {
                    c = '&';
                } else {
                    c = -1;
                }
                break;

            case 4:
                /* quot, nbsp, apos */
                if (!str_cmp("quot", st->erefbuf)) {
                    c = '"';
                } else if (!str_cmp("apos", st->erefbuf)) {
                    c = '\'';
                } else if (!str_cmp("nbsp", st->erefbuf)) {
                    /* this is incorrect but is semantically better than just 
                     * ignoring it with the rest of the extended char 
                     * references (XXX: at least until we recognise nbsp as a 
                     * whitespace character) */
                    c = ' ';
                } else {
                    c = -1;
                }
                break;

            default:
                c = -1;
                break;
            };

            if (c != -1) {
                char *tmp;

                if ((st->flags & FLAG_BUFFER) 
                  /* handle case where part of eref is in the buffer */
                  && ((tmppos >= st->pos) && (tmppos <= st->end))) {
                    /* working in the buffer */
                    pos += st->count;
                } else {
                    /* nothing should be buffered after this, put reference char
                     * in buffer alone, saving next_in position */
                    parser->next_in = tmppos;
                    parser->avail_in = tmpend - parser->next_in;
                    pos = st->pos = st->buf;
                    end = st->end = st->buf + 1;
                    st->flags |= FLAG_BUFFER;
                }
                
                /* XXX: semi-dodgy cast to allow changing of the buffer */
                tmp = (char *) pos;
                *tmp = (char) c;

                JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
            }
            /* fallthrough for failed conversions */

        case ASCII_CASE_EXTENDED:
        default:
            /* anything else isn't valid */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
        }
    }
    ENDBUF(MAXENTITYREF, STATE_EREF_PEEK);

/* recognising comments and marked sections */
decl_peek_label:
    while (tmppos < tmpend) {
        switch (*tmppos) {
        case '[':
            /* possible marked section */
            tmppos++;
            st->count++;
            goto marksec_peek_label_first_label;

        case '-':
            /* possible comment */
            tmppos++;
            st->count++;
            goto comment_peek_first_label;

        case '>':
            /* <!> (empty) comment */
            goto tag_peek_name_label;

        default:
            goto tag_peek_name_label;
        }
    }
    ENDBUF(st->lookahead, STATE_DECL_PEEK);

/* recognising comments (second hyphen character) */
comment_peek_first_label:
    while (tmppos < tmpend) {
        switch (*tmppos) {
        case '-':
            /* comment continues */
            tmppos++;
            st->count++;
            goto comment_peek_label;

        default:
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
        }
    }
    ENDBUF(st->lookahead, STATE_COMMENT_PEEK_FIRST);

/* recognising comments */
comment_peek_label:
    while ((tmppos < tmpend) && (st->count < st->lookahead)) {
        switch (*tmppos) {
        case '-':
            /* comment starts to end? */
            tmppos++;
            st->count++;
            goto comment_peek_end_first_label;

        case '<':
            /* start of another comment? */
            tmppos++;
            st->count++;
            goto comment_peek_badend_first_label;

        default:
            tmppos++;
            st->count++;
            break;
        }
    }
    ENDBUF(st->lookahead, STATE_COMMENT_PEEK);

/* recognising comments */
comment_peek_end_first_label:
    while ((tmppos < tmpend) && (st->count < st->lookahead)) {
        switch (*tmppos) {
        case '-':
            /* comment continues to end */
            tmppos++;
            st->count++;
            goto comment_peek_end_label;

        case '<':
            /* start of another comment? */
            tmppos++;
            st->count++;
            goto comment_peek_badend_first_label;

        default:
            /* not ending after all */
            tmppos++;
            st->count++;
            goto comment_peek_label;
        }
    }
    ENDBUF(st->lookahead, STATE_COMMENT_PEEK_END_FIRST);

/* recognising comments */
comment_peek_end_label:
    while ((tmppos < tmpend) && (st->count < st->lookahead)) {
        switch (*tmppos) {
        case '-':
            /* comment continues to end */
            tmppos++;
            st->count++;
            break;

        case '<':
            /* start of another comment? */
            tmppos++;
            st->count++;
            goto comment_peek_badend_first_label;

        case '>':
            /* comment ends */
            st->flags |= FLAG_COMMENT;
            
            /* need to return previous entity */
            st->count = 0;
            tmppos++;
            RETURN_IF_STORED(STATE_COMMENT_ENTRY);
            /* otherwise (nothing stored) */
            goto comment_entry_label;

        default:
            /* not ending after all */
            tmppos++;
            st->count++;
            goto comment_peek_label;
        }
    }
    ENDBUF(st->lookahead, STATE_COMMENT_PEEK_END);

comment_peek_badend_first_label:
    while ((tmppos < tmpend) && (st->count < st->lookahead)) {
        switch (*tmppos) {
        case '!':
            /* comment start continues */
            tmppos++;
            st->count++;
            goto comment_peek_badend_second_label;

        default:
            /* not starting another comment after all */
            tmppos++;
            st->count++;
            goto comment_peek_label;
        }
    }
    ENDBUF(st->lookahead, STATE_COMMENT_PEEK_BADEND_FIRST);

comment_peek_badend_second_label:
    while ((tmppos < tmpend) && (st->count < st->lookahead)) {
        switch (*tmppos) {
        case '-':
            /* comment start continues */
            tmppos++;
            st->count++;
            goto comment_peek_badend_label;

        default:
            /* not starting another comment after all */
            tmppos++;
            st->count++;
            goto comment_peek_label;
        }
    }
    ENDBUF(st->lookahead, STATE_COMMENT_PEEK_BADEND_SECOND);

comment_peek_badend_label:
    while ((tmppos < tmpend) && (st->count < st->lookahead)) {
        switch (*tmppos) {
        case '-':
            /* comment start complete, don't accept previous comment start as
             * valid */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);

        default:
            /* not starting another comment after all */
            tmppos++;
            st->count++;
            goto comment_peek_label;
        }
    }
    ENDBUF(st->lookahead, STATE_COMMENT_PEEK_BADEND);

/* need to pass over the <!-- */
comment_entry_label:
    while (1) {
        assert(pos < end);
        switch (*pos++) {
        case '-':
            st->count++;
            /* pass two hyphens and then return a start comment */
            if (st->count >= 2) {
                *length = 0;
                RETURN(MLPARSE_COMMENT, STATE_TOPLEVEL);
            }
            break;
        }
    }

/* parsing inside a comment or cdata section */
ccdata_toplevel_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case ASCII_CASE_UPPER:
            /* push character onto word */
            if (strip) {
                word[0] = TOLOWER(*pos++);
            } else {
                word[0] = *pos++;
            }
            st->len = 1;
            goto ccdata_word_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            word[0] = *pos++;
            st->len = 1;
            goto ccdata_word_label;

        case '-':
            st->len = 0;
            pos++;
            st->next_state = STATE_TOPLEVEL;
            goto ccdata_end_comment_first_label;

        case ']':
            st->len = 0;
            pos++;
            st->next_state = STATE_TOPLEVEL;
            goto ccdata_end_cdata_first_label;

        case ASCII_CASE_SPACE:
            if (strip) {
                pos++;
            } else {
                word[0] = *pos++;
                st->len = 1;
                goto ccdata_space_label;
            }
            break;

        default:
            if (strip) {
                pos++;
            } else {
                word[0] = *pos++;
                st->len = 1;
                goto ccdata_word_label;
            }
            break;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            if (strip) {
                /* ignore junk in words */
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_WORD);
            }
            break;
        }
    }

/* parsing a word in a cdata or comment section */
ccdata_word_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case ASCII_CASE_UPPER:
            /* push character onto word */
            if (strip) {
                PUSH(TOLOWER(*pos++), MLPARSE_WORD, STATE_WORD);
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_WORD);
            }
            break;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            PUSH(*pos++, MLPARSE_WORD, STATE_WORD);
            break;

        case '-':
            /* could be the end of the comment */
            pos++;
            st->next_state = STATE_WORD;
            goto ccdata_end_comment_first_label;

        case ']':
            /* could be the end of the cdata section */
            pos++;
            st->next_state = STATE_WORD;
            goto ccdata_end_cdata_first_label;

        case ASCII_CASE_SPACE:
            /* word ends */
            *length = st->len;
            RETURN(MLPARSE_WORD, STATE_TOPLEVEL);

        case '\'':
        default:
            /* two successive punctuation marks end a word */
            if (strip) {
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_WORD);
            }
            goto ccdata_punc_label;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            if (strip) {
                /* ignore junk in words */
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_WORD);
            }
            break;
        }
    }

ccdata_punc_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case ASCII_CASE_UPPER:
            /* push character onto word */
            if (strip) {
                PUSH(TOLOWER(*pos++), MLPARSE_WORD, STATE_PUNC);
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_PUNC);
            }
            goto ccdata_word_label;

        case '-':
            /* could be the end of the comment */
            pos++;
            st->next_state = STATE_PUNC;
            goto ccdata_end_comment_first_label;

        case ']':
            /* could be the end of the cdata section */
            pos++;
            st->next_state = STATE_PUNC;
            goto ccdata_end_cdata_first_label;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            PUSH(*pos++, MLPARSE_WORD, STATE_PUNC);
            goto ccdata_word_label;

        default:
        case ASCII_CASE_SPACE:
            /* word ends */
            *length = st->len;
            RETURN(MLPARSE_WORD, STATE_TOPLEVEL);

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            if (strip) {
                /* ignore junk in words */
                pos++;
            } else {
                PUSH(*pos++, MLPARSE_WORD, STATE_WORD);
            }
            break;
        }
    }

ccdata_space_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        default:
            /* space ends */
            *length = st->len;
            RETURN(MLPARSE_WHITESPACE, STATE_TOPLEVEL);

        case ASCII_CASE_SPACE:
            /* space continues */
            PUSH(*pos++, MLPARSE_WHITESPACE, STATE_SPACE);
            break;

        case ASCII_CASE_CONTROL:
            /* ignore junk */
            pos++;
            break;
        }
    }

ccdata_end_comment_first_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '-':
            /* continues to end */
            pos++;
            st->count = 2;  /* we've consumed two hyphens */
            goto ccdata_end_comment_label;

        default:
            /* not ending after all */
            st->count = 1;  /* we've consumed one hyphen */
            goto ccdata_recovery_label;
        }
    }

ccdata_end_comment_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            if (st->flags & FLAG_COMMENT) {
                /* comment ends, return stored value */
                tmppos++;
                RETURN_IF_STORED(STATE_CCDATA_END_COMMENT); /* and here after */
                /* otherwise (nothing stored) */
                st->flags &= ~FLAG_COMMENT;
                pos++;
                *length = 0;
                RETURN(MLPARSE_END | MLPARSE_COMMENT, STATE_TOPLEVEL);
            } else {
                /* was a comment ending in a cdata section */
                assert(st->flags & FLAG_CDATA);
                goto ccdata_recovery_label;
            }
            break;

        case '-':
            /* continues to end */
            st->count++;
            pos++;
            break;

        default:
            /* not ending after all */
            goto ccdata_recovery_label;
        }
    }

ccdata_end_cdata_first_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case ']':
            /* continues to end */
            pos++;
            st->count = 2;  /* we've consumed two ']' characters */
            goto ccdata_end_cdata_label;

        default:
            /* not ending after all */
            st->count = 1;  /* we've consumed one ']' character */
            goto ccdata_recovery_label;
        }
    }

ccdata_end_cdata_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '>':
            if (st->flags & FLAG_CDATA) {
                /* cdata ends, return stored value */
                tmppos++;
                RETURN_IF_STORED(STATE_CCDATA_END_CDATA); /* and here after */
                /* otherwise (nothing stored) */
                st->flags &= ~FLAG_CDATA;
                pos++;
                *length = 0;
                RETURN(MLPARSE_END | MLPARSE_CDATA, STATE_TOPLEVEL);
            } else {
                /* was a comment ending in a cdata section */
                assert(st->flags & FLAG_COMMENT);
                goto ccdata_recovery_label;
            }
            break;

        case ']':
            /* continues to end */
            st->count++;
            pos++;
            break;

        default:
            /* not ending after all */
            goto ccdata_recovery_label;
        }
    }

ccdata_recovery_label:
    while (st->count) {
        if (st->flags & FLAG_COMMENT) {
            /* need to push '-' onto the string */
            switch (st->next_state) {
            case STATE_TOPLEVEL:
                if (!strip) {
                    word[0] = '-';
                    st->len = 1;
                    st->next_state = STATE_WORD;
                } else {
                    /* do nothing */
                }
                break;

            case STATE_WORD:
                if (!strip) {
                    PUSH('-', MLPARSE_WORD, STATE_CCDATA_RECOVERY);
                }
                st->next_state = STATE_PUNC;
                break;

            case STATE_PUNC:
                if (!strip) {
                    PUSH('-', MLPARSE_WORD, STATE_CCDATA_RECOVERY);
                }
                st->next_state = STATE_TOPLEVEL;
                *length = st->len;
                RETURN(MLPARSE_WORD, STATE_CCDATA_RECOVERY);

            default:
            case STATE_SPACE:
                CANT_GET_HERE();
            }
        } else {
            assert(st->flags & FLAG_CDATA);
            /* need to push ']' onto the string */
            switch (st->next_state) {
            case STATE_TOPLEVEL:
                if (!strip) {
                    word[0] = ']';
                    st->len = 1;
                    st->next_state = STATE_WORD;
                } else {
                    /* do nothing */
                }
                break;

            case STATE_WORD:
                if (!strip) {
                    PUSH(']', MLPARSE_WORD, STATE_CCDATA_RECOVERY);
                } else {
                    /* do nothing */
                }
                st->next_state = STATE_PUNC;
                break;

            case STATE_PUNC:
                if (!strip) {
                    PUSH(']', MLPARSE_WORD, STATE_CCDATA_RECOVERY);
                }
                st->next_state = STATE_TOPLEVEL;
                *length = st->len;
                RETURN(MLPARSE_WORD, STATE_CCDATA_RECOVERY);

            default:
            case STATE_SPACE:
                CANT_GET_HERE();
            }

        }
        st->count--;
    }
    JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);

marksec_peek_label_first_label:
    while ((tmppos < tmpend) && (st->count < st->lookahead)) {
        switch (*tmppos) {
        case ASCII_CASE_SPACE:
            /* ignore */
            tmppos++;
            st->count++;
            break;

        case ASCII_CASE_UPPER:
            /* start of marked section identifier */
            st->tagbuf[0] = ASCII_TOLOWER(*tmppos++);
            st->tagbuflen = 1;
            st->count++;
            goto marksec_peek_label_label;

        case ASCII_CASE_LOWER:
            /* start of marked section identifier */
            /* (ab)use the entity refence buffer to identify marked sections */
            st->tagbuf[0] = *tmppos++;
            st->tagbuflen = 1;
            st->count++;
            goto marksec_peek_label_label;

        default:
            /* not a marked section */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
        }
    }
    ENDBUF(st->lookahead, STATE_MARKSEC_PEEK_LABEL_FIRST);

marksec_peek_label_label:
    while ((tmppos < tmpend) && (st->count < st->lookahead)) {
        switch (*tmppos) {
        case ASCII_CASE_UPPER:
            /* add to marked section name in entity reference buffer */
            if (st->tagbuflen < st->wordlen) {
                st->tagbuf[st->tagbuflen++] = ASCII_TOLOWER(*tmppos);
            }
            tmppos++;
            st->count++;
            break;

        case ASCII_CASE_LOWER:
            /* add to marked section name in entity reference buffer */
            if (st->tagbuflen < st->wordlen) {
                st->tagbuf[st->tagbuflen++] = *tmppos;
            }
            tmppos++;
            st->count++;
            break;

        case ASCII_CASE_SPACE:
            /* marked section label should now end */
            tmppos++;
            st->count++;
            goto marksec_peek_label_last_label;

        case '[':
            /* marked section has started, but let _last state handle this */
            goto marksec_peek_label_last_label;

        default:
            /* not a marked section */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
        }
    }
    ENDBUF(st->lookahead, STATE_MARKSEC_PEEK_LABEL);

marksec_peek_label_last_label:
    while ((tmppos < tmpend) && (st->count < st->lookahead)) {
        switch (*tmppos) {
        case ASCII_CASE_SPACE:
            /* ignore */
            tmppos++;
            st->count++;
            break;

        case '[':
            /* finished parsing marked section label */
            tmppos++;
            st->count++;
            goto marksec_peek_label;

        default:
            /* not a marked section */
            JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
        }
    }
    ENDBUF(st->lookahead, STATE_MARKSEC_PEEK_LABEL_LAST);

marksec_peek_label:
    while ((tmppos < tmpend) && (st->count < st->lookahead)) {
        switch (*tmppos) {
        case ']':
            /* start of an end of marked section */
            tmppos++;
            st->count++;
            goto marksec_peek_end_first_label;

        default:
            /* ignore */
            tmppos++;
            st->count++;
            break;
        }
    }
    ENDBUF(st->lookahead, STATE_MARKSEC_PEEK);

marksec_peek_end_first_label:
    while ((tmppos < tmpend) && (st->count < st->lookahead)) {
        switch (*tmppos) {
        case ']':
            /* continuation of an end of marked section */
            tmppos++;
            st->count++;
            goto marksec_peek_end_label;

        default:
            tmppos++;
            st->count++;
            goto marksec_peek_label;
        }
    }
    ENDBUF(st->lookahead, STATE_MARKSEC_PEEK_END_FIRST);

marksec_peek_end_label:
    while ((tmppos < tmpend) && (st->count < st->lookahead)) {
        switch (*tmppos) {
        case '>':
            if (!str_ncmp(st->tagbuf, "cdata", 5 /* strlen("cdata") */)) {
                /* end marked section */
                st->flags |= FLAG_CDATA;
            
                /* need to return previous entity */
                st->count = 0;
                tmppos++;
                RETURN_IF_STORED(STATE_CDATA_ENTRY);
                /* otherwise (nothing stored) */
                goto cdata_entry_label;
            } else {
                /* its a marked section, but we don't recognise the type.  If
                 * its SGML other valid (but currently unhandled) types are
                 * ignore, include, rcdata, temp */

                JUMP(st->next_state, st->flags & ~FLAG_BUFFER, 0);
            }
            break;

        case ']':
            /* continuation of an end marked section? */
            tmppos++;
            st->count++;
            break;

        default:
            tmppos++;
            st->count++;
            goto marksec_peek_label;
        }
    }
    ENDBUF(st->lookahead, STATE_MARKSEC_PEEK_END);

/* parsing <![ CDATA [.  Because i'm sick of the whole trie-in-code thing, (and
 * we've just validated the actual tag) just wait for two ['s to go past */
cdata_entry_label:
    while (1) {
        assert(pos < end);
        switch (*pos) {
        case '[':
            pos++;
            st->count++;
            if (st->count >= 2) {
                *length = 0;
                RETURN(MLPARSE_CDATA, STATE_TOPLEVEL);
            }
            break;

        default:
            pos++;
            break;
        }
    }

/* parser has buffered content, set pos and end appropriately and then jump to
 * the correct state */
trans_unbuffer_label:
    pos = st->pos;
    end = st->end;
    JUMP(st->state, st->flags & ~FLAG_BUFFER, 0);

/* the parser has hit EOF */
eof_label:
    switch (st->next_state) {
    case STATE_WORD:
    case STATE_WORD_ENDS:
    case STATE_PUNC:
    case STATE_PUNC_ENDS:
    case STATE_ACRONYM:
    case STATE_ACRONYM_LETTER:
        st->next_state = STATE_EOF;
        *length = st->len;
        RETURN(MLPARSE_WORD, STATE_EOF);

    case STATE_SPACE:
        st->next_state = STATE_EOF;
        *length = st->len;
        RETURN(MLPARSE_WHITESPACE, STATE_EOF);

    case STATE_PARAM:
        st->next_state = STATE_EOF;
        *length = st->len;
        RETURN(MLPARSE_PARAM, STATE_EOF);

    case STATE_PVAL:
    case STATE_PVAL_PUNC:
    case STATE_PVAL_QUOT:
    case STATE_PVAL_QUOT_WORD:
    case STATE_PVAL_QUOT_PUNC:
    case STATE_PVAL_DQUOT:
    case STATE_PVAL_DQUOT_WORD:
    case STATE_PVAL_DQUOT_PUNC:
        st->next_state = STATE_EOF;
        *length = st->len;
        RETURN(MLPARSE_PARAMVAL, STATE_EOF);

    case STATE_PVAL_DQUOT_SPACE:
    case STATE_PVAL_QUOT_SPACE:
        st->next_state = STATE_EOF;
        *length = st->len;
        RETURN(MLPARSE_WHITESPACE, STATE_EOF);

    case STATE_PARAM_EQ:
    case STATE_PVAL_FIRST:
    case STATE_PVAL_SELFEND:
        /* don't do anything */
        st->next_state = STATE_EOF;
        break;

    case STATE_EOF:
        break;

    default:
        goto err_label;
    }

    *length = 0;
    RETURN(MLPARSE_EOF, STATE_EOF);
   
/* the parser has hit an error */
err_label:
    st->flags = FLAG_NONE;
    *length = 0;
    RETURN(MLPARSE_ERR, STATE_ERR);
}

void mlparse_eof(struct mlparse *parser) {
    switch (parser->state->state) {
    default:
        /* arrange to return the word being parsed */
        parser->state->next_state = parser->state->state;
        parser->state->state = STATE_EOF;
        break;

    case STATE_TOPLEVEL:
        /* arrange to return EOF on next call (can validly end in this state) */
        parser->state->next_state = STATE_EOF;
        parser->state->state = STATE_EOF;
        break;

    /* all of the peek states fail to find target on eof */
    case STATE_TAG_PEEK_FIRST:
    case STATE_TAG_PEEK:
    case STATE_TAG_PEEK_NAME:
    case STATE_TAG_PEEK_NAME_CONT:
    case STATE_EREF_PEEK_FIRST:
    case STATE_EREF_PEEK:
    case STATE_EREF_NUM_PEEK_FIRST:
    case STATE_EREF_NUM_PEEK:
    case STATE_EREF_HEX_PEEK:
    case STATE_DECL_PEEK:
    case STATE_COMMENT_PEEK_FIRST:
    case STATE_COMMENT_PEEK:
    case STATE_COMMENT_PEEK_END:
    case STATE_COMMENT_PEEK_END_FIRST:
    case STATE_COMMENT_PEEK_BADEND_FIRST:
    case STATE_COMMENT_PEEK_BADEND_SECOND:
    case STATE_COMMENT_PEEK_BADEND:
    case STATE_MARKSEC_PEEK_LABEL_FIRST:
    case STATE_MARKSEC_PEEK_LABEL:
    case STATE_MARKSEC_PEEK_LABEL_LAST:
    case STATE_MARKSEC_PEEK:
    case STATE_MARKSEC_PEEK_END_FIRST:
    case STATE_MARKSEC_PEEK_END:
    case STATE_CCDATA_RECOVERY: /* FIXME: need to alter current word for 
                                 * proper recovery */
        /* break out of peek state */
        parser->state->state = parser->state->next_state;
        parser->state->next_state = STATE_EOF;
        break;

    case STATE_TOPLEVEL_ESC:
    case STATE_WORD_ESC:
    case STATE_WORD_ENDS_ESC:
    case STATE_PUNC_ESC:
    case STATE_PUNC_ENDS_ESC:
    case STATE_SPACE_ESC:
    case STATE_ACRONYM_ESC:
    case STATE_ACRONYM_LETTER_ESC:
    case STATE_PVAL_QUOT_ESC:
    case STATE_PVAL_QUOT_WORD_ESC:
    case STATE_PVAL_QUOT_PUNC_ESC:
    case STATE_PVAL_QUOT_SPACE_ESC:
    case STATE_PVAL_DQUOT_ESC:
    case STATE_PVAL_DQUOT_WORD_ESC:
    case STATE_PVAL_DQUOT_PUNC_ESC:
    case STATE_PVAL_DQUOT_SPACE_ESC:
    case STATE_COMMENT_ENTRY:
    case STATE_CDATA_ENTRY:
        /* shouldn't happen */
        parser->state->state = STATE_ERR;
        break;

    case STATE_EOF:
        break;
    }
}

/* test code */
#ifdef MLPARSE_TEST
#define WORDLEN 50
#define BUFSIZE 4096
#define LOOKAHEAD 999

/* whether to print stuff out or not (makes debugging much harder if incredible
 * amounts of stuff are going to stdout) */
#define MLPARSE_TEST_OUTPUT

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "stream.h"

void parse(struct mlparse *parser, FILE *output, FILE *input, 
  unsigned int buflen) {
    enum mlparse_ret parse_ret;      /* value returned by the parser */
    char word[WORDLEN + 1];          /* word returned by the parser */
    unsigned int len;                /* length of word */
    char *buf = (char *) parser->next_in;
    unsigned int bytes = 0;
    struct stream *instream;
    struct stream_filter *filter;

    if ((instream = stream_new()) 
      && (filter = (struct stream_filter *) detectfilter_new(BUFSIZ, 0))
      && (stream_filter_push(instream, filter) == STREAM_OK)) {
        /* succeeded */
        instream->next_in = NULL;
        instream->avail_in = 0;
    } else {
        fprintf(stderr, "couldn't initialise stream/detection filter\n");
        return;
    }

    while (((parse_ret = mlparse_parse(parser, word, &len, 1)) != MLPARSE_ERR) 
      && (parse_ret != MLPARSE_EOF)) {
        switch (parse_ret) {
        case MLPARSE_WORD:
        case MLPARSE_END | MLPARSE_WORD:
        case MLPARSE_CONT | MLPARSE_WORD:
#ifdef MLPARSE_TEST_OUTPUT
            word[len] = '\0';
            fprintf(output, "WORD = %s (%u)%s\n", word, len, 
              parse_ret & MLPARSE_CONT ? " (cont)" 
                : ((parse_ret & MLPARSE_END) ? " (endsentence)" : ""));
#endif
            break;

        case MLPARSE_TAG:
        case MLPARSE_CONT | MLPARSE_TAG:
#ifdef MLPARSE_TEST_OUTPUT
            word[len] = '\0';
            fprintf(output, "TAG = %s (%u)%s\n", word, len, 
              parse_ret & MLPARSE_CONT ? " (cont)" : "");
#endif
            break;

        case MLPARSE_END | MLPARSE_TAG:
        case MLPARSE_END | MLPARSE_CONT | MLPARSE_TAG:
#ifdef MLPARSE_TEST_OUTPUT
            word[len] = '\0';
            fprintf(output, "ENDTAG = %s (%u)%s\n", word, len, 
              parse_ret & MLPARSE_CONT ? " (cont)" : "");
#endif
            break;

        case MLPARSE_PARAM:
        case MLPARSE_CONT | MLPARSE_PARAM:
#ifdef MLPARSE_TEST_OUTPUT
            word[len] = '\0';
            fprintf(output, "PARAM = %s (%u)%s\n", word, len, 
              parse_ret & MLPARSE_CONT ? " (cont)" : "");
#endif
            break;

        case MLPARSE_PARAMVAL:
        case MLPARSE_CONT | MLPARSE_PARAMVAL:
#ifdef MLPARSE_TEST_OUTPUT
            word[len] = '\0';
            fprintf(output, "PARAMVAL = %s (%u)%s\n", word, len, 
              parse_ret & MLPARSE_CONT ? " (cont)" : "");
#endif
            break;

        case MLPARSE_COMMENT:
#ifdef MLPARSE_TEST_OUTPUT
            fprintf(output, "START_COMMENT\n");
#endif
            break;

        case MLPARSE_END | MLPARSE_COMMENT:
#ifdef MLPARSE_TEST_OUTPUT
            fprintf(output, "END_COMMENT\n");
#endif
            break;

        case MLPARSE_WHITESPACE:
        case MLPARSE_CONT | MLPARSE_WHITESPACE:
#ifdef MLPARSE_TEST_OUTPUT
            word[len] = '\0';
            fprintf(output, "WHITESPACE = '%s' (%u)%s\n", word, len, 
              parse_ret & MLPARSE_CONT ? " (cont)" : "");
#endif
            break;

        case MLPARSE_CDATA:
#ifdef MLPARSE_TEST_OUTPUT
            fprintf(output, "START_CDATA\n");
#endif
            break;

        case MLPARSE_CDATA | MLPARSE_END:
#ifdef MLPARSE_TEST_OUTPUT
            fprintf(output, "END_CDATA\n");
#endif
            break;

        case MLPARSE_INPUT:
            switch (stream(instream)) {
            case STREAM_OK:
                parser->next_in = instream->curr_out;
                parser->avail_in = instream->avail_out;
                break;

            case STREAM_INPUT:
                if ((len = fread(buf, 1, buflen, input)) && (len != -1)) {
                    instream->next_in = buf;
                    instream->avail_in = len;
                    bytes += len;
                } else if (len) {
                    fprintf(stderr, "read error: %s\n", strerror(errno));
                } else {
                    stream_flush(instream, STREAM_FLUSH_FINISH);
                }
                break;

            case STREAM_END:
                mlparse_eof(parser);
                break;

            default:
                fprintf(stderr, "error streaming\n");
                return;
            }
            break;

        default:
            fprintf(stderr, "unknown retval %d\n", parse_ret);
            return;
        }
    }

    if (parse_ret == MLPARSE_ERR) {
        fprintf(stderr, "parser failed at %lu\n", 
          bytes - mlparse_buffered(parser) - parser->avail_in);
    }

    stream_delete(instream);
    fputc('\n', output);
    return;
}

int main(int argc, char **argv) {
    unsigned int i;
    struct mlparse parser = {NULL, 0, NULL};
    FILE *fp;

    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if ((fp = fopen(argv[i], "rb"))
              && mlparse_new(&parser, WORDLEN, LOOKAHEAD)
              && (parser.next_in = malloc(BUFSIZE))) {
                parse(&parser, stdout, fp, BUFSIZE);
                mlparse_delete(&parser);
                fclose(fp);
            } else if (fp) {
                fprintf(stderr, "failed to open parser on %s: %s\n", argv[i], 
                  strerror(errno));
                fclose(fp);
                return EXIT_FAILURE;
            } else {
                fprintf(stderr, "failed to open %s: %s\n", argv[i], 
                  strerror(errno));
                return EXIT_FAILURE;
            }
        }
    } else {
        if (mlparse_new(&parser, WORDLEN, LOOKAHEAD)
          && (parser.next_in = malloc(BUFSIZE))) {
            parse(&parser, stdout, stdin, BUFSIZE);
            mlparse_delete(&parser);
        } else {
            fprintf(stderr, "failed to open parser on stdin\n");
            return EXIT_FAILURE;
        } 
    }

    return EXIT_SUCCESS;
}
#endif

