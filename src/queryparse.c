/* queryparse.c implements a parser to parse google-style queries.  See
 * queryparse.h for more detail
 *
 * note that although queryparse currently only supports parsing the
 * whole query in-place, it was written to allow fairly easy conversion
 * to more flexible parsing schemes.
 *
 * written nml 2003-07-08
 *
 */

#include "firstinclude.h"

#include "queryparse.h"

#include "ascii.h"
#include "def.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

/* literals for the operators. note that AND and OR must be
 * uppercase alpha characters.  Other characters cannot be alpha-numeric. */
#define AND_OP "AND"
#define OR_OP "OR"
#define NOSTOP_OP '+'
#define EXCLUDE_OP '-'
#define PHRASE_DELIM '"'
#define MOD_START '['
#define MOD_STRING_END ':'
#define MOD_END ']'

struct queryparse {
    const char *buf;          /* buffer we're parsing from */
    const char *end;          /* end of the buffer */
    unsigned int maxwordlen;  /* maximum word len */
    int state;                /* parsing state we're in */
    unsigned int bytes;       /* number of bytes parsed thus far */
    int err;                  /* last error to occur or 0 */
    unsigned int warn;        /* accumulated warnings */
};

/* parser states */
enum {
    ERR = 0,                  /* error */
    ENDFILE = 1,              /* hit end-of-file (or string in this case) */
    TOPLEVEL = 2,             /* not in any entities */
    INWORD = 3,               /* in a word */
    INWORD_PUNC = 5,          /* in a word, after punctuation */
    INWORD_NOSTOP = 6,        /* in a word that shouldn't be stopped */
    INWORD_NOSTOP_PUNC = 8,   /* in a no-stop word, after punctuation */
    INWORD_EXCLUDE = 9,       /* in word that should be excluded from results*/
    INWORD_EXCLUDE_PUNC = 11, /* in neg-word after punctuation */
    INOR = 12,                /* in an OR operator */
    INAND = 13,               /* in an AND operator */
    INPHRASE = 14,            /* in a " delimited phrase */
    INPHRASE_WORD = 15,       /* in a word in a phrase */
    INPHRASE_WORD_PUNC = 17,  /* in a word in a phrase after punctuation */
    INPHRASE_END = 18,        /* in a phrase that has ended */
    ENDSENTENCE = 19,         /* in the end of a sentence in a phrase */
    ENDSENTENCE_END = 20,     /* in a sentence that has ended */
    INMOD_MOD = 21,           /* parsing the query modifier */
    INMOD = 22,               /* in a query modifier */
    INMOD_WORD = 23,          /* in word in query modifier */
    INMOD_END = 26            /* in a modifier that has ended */
};

struct queryparse *queryparse_new(unsigned int maxwordlen, 
  const char *query, unsigned int querylen) {
    struct queryparse *parser = malloc(sizeof(*parser));

    if (parser) {
        parser->buf = query;
        parser->end = query + querylen;
        parser->bytes = 0;
        parser->state = TOPLEVEL;
        parser->maxwordlen = maxwordlen;
    }

    return parser;
}

#define CASE_END_SENTENCE                                                     \
         '.': case '!': case '?'

/* jump table */
#define JUMP(state)                                                           \
    if (1) {                                                                  \
        switch (state) {                                                      \
        case TOPLEVEL: goto toplevel_label;                                   \
        case INWORD: goto inword_label;                                       \
        case INWORD_PUNC: goto punc_label;                                    \
        case INWORD_NOSTOP: goto inword_nostop_label;                         \
        case INWORD_NOSTOP_PUNC: goto inword_nostop_punc_label;               \
        case INWORD_EXCLUDE: goto inword_exclude_label;                       \
        case INWORD_EXCLUDE_PUNC: goto inword_exclude_punc_label;             \
        case INOR: goto inor_label;                                           \
        case INAND: goto inand_label;                                         \
        case INPHRASE: goto inphrase_label;                                   \
        case INPHRASE_WORD: goto inphrase_word_label;                         \
        case INPHRASE_WORD_PUNC: goto inphrase_word_punc_label;               \
        case INPHRASE_END: goto inphrase_end_label;                           \
        case ENDSENTENCE: goto endsentence_label;                             \
        case ENDSENTENCE_END: goto endsentence_end_label;                     \
        case INMOD_MOD: goto inmod_mod_label;                                 \
        case INMOD: goto inmod_label;                                         \
        case INMOD_WORD: goto inmod_word_label;                               \
        case INMOD_END: goto inmod_end_label;                                 \
        case ENDFILE: goto endfile_label;                                     \
        default: goto err_label;                                              \
        }                                                                     \
    } else /* dangling else to allow function-like syntax */

enum queryparse_ret queryparse_parse(struct queryparse *parser, 
  char *word, unsigned int *len) {
    char c;
    *len = 0;

    JUMP(parser->state);

/* not in any entities yet */
toplevel_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            /* note: AND and OR could be more efficiently detected by special
             * casing their first values, but that would make them harder to
             * change */

            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = ASCII_TOLOWER(c);
            } else {
                word[*len] = '\0';
                parser->state = TOPLEVEL;
                return QUERYPARSE_WORD;
            }

            if (c == AND_OP[0]) {
                /* it could be an AND op */
                goto inand_label;
            } else if (c == OR_OP[0]) {
                /* it could be an OR op */
                goto inor_label;
            } else {
                /* start of a normal word */
                goto inword_label;
            }
            break;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                word[*len] = '\0';
                parser->state = TOPLEVEL;
                return QUERYPARSE_WORD;
            }

            /* start of a normal word */
            goto inword_label;
            break;

        case NOSTOP_OP:
            /* start of a non-stopping word */
            goto inword_nostop_label;
            break;

        case EXCLUDE_OP:
            /* start of an excluded word */
            goto inword_exclude_label;
            break;

        case PHRASE_DELIM:
            parser->state = INPHRASE;
            return QUERYPARSE_START_PHRASE;
            break;

        case MOD_START:
            goto inmod_mod_label;
            break;

        case ASCII_CASE_SPACE:
            /* silently eat whitespace */
            break;

        case '(':
            parser->warn |= QUERYPARSE_WARN_PARENS_BOOLEAN; /*warn,fallthrough*/
        default:
        case ASCII_CASE_EXTENDED:
            /* anything else we don't record in the word, but go to inword 
             * anyway (so we know when a string of junk characters occurred) */
            goto inword_label;
            break;
        }
    }
    /* if we end in toplevel we just EOF */
    goto endfile_label;

/* in a normal word, we may or may not have actually pushed any characters onto
 * the return word */
inword_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            c = ASCII_TOLOWER(c);
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD;
            }
            break;

        case '-':
        default:
            /* break across two punctuation marks in a row */
            goto punc_label;

        case ASCII_CASE_CONTROL:
        case ASCII_CASE_EXTENDED:
            /* ignore junk characters */
            break;

        case ASCII_CASE_SPACE:
            /* word ended */
            if (*len) {
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto toplevel_label;
            }
            break;
        }
    }
    /* if we end in a word, we have to return it */
    if (*len) {
        word[*len] = '\0';
        parser->state = ENDFILE; /* transition to eof */
        return QUERYPARSE_WORD;
    } else {
        /* it was empty, so warn them of that */
        parser->warn |= QUERYPARSE_WARN_NONWORD;
    }
    goto endfile_label;

punc_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            c = ASCII_TOLOWER(c);
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD;
            }
            goto inword_label;

        case ASCII_CASE_PUNCTUATION:
            /* break across two punctuation marks in a row */
            word[*len] = '\0';
            parser->state = TOPLEVEL; /* transition back to toplevel */
            return QUERYPARSE_WORD;

        default:
        case ASCII_CASE_CONTROL:
        case ASCII_CASE_EXTENDED:
            /* ignore junk characters */
            break;

        case ASCII_CASE_SPACE:
            /* word ended */
            if (*len) {
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto toplevel_label;
            }
            break;
        }
    }
    /* if we end in a word, we have to return it */
    if (*len) {
        word[*len] = '\0';
        parser->state = ENDFILE; /* transition to eof */
        return QUERYPARSE_WORD;
    } else {
        /* it was empty, so warn them of that */
        parser->warn |= QUERYPARSE_WARN_NONWORD;
    }
    goto endfile_label;

/* in a word that shouldn't be stopped (pretty much the same as normal word) */
inword_nostop_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            c = ASCII_TOLOWER(c);
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD_NOSTOP;
            }
            break;

        case ASCII_CASE_CONTROL:
        case ASCII_CASE_EXTENDED:
            /* ignore */
            break;

        default:
            goto inword_nostop_punc_label;

        case ASCII_CASE_SPACE:
            /* word ended */
            if (*len) {
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD_NOSTOP;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto toplevel_label;
            }
            break;
        }
    }
    /* if we end in a word, we have to return it */
    if (*len) {
        word[*len] = '\0';
        parser->state = ENDFILE; /* transition to eof */
        return QUERYPARSE_WORD_NOSTOP;
    } else {
        /* it was empty, so warn them of that */
        parser->warn |= QUERYPARSE_WARN_NONWORD;
    }
    goto endfile_label;

/* in a word that shouldn't be stopped (pretty much the same as normal word) */
inword_nostop_punc_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            c = ASCII_TOLOWER(c);
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD_NOSTOP;
            }
            goto inword_nostop_label;

        case ASCII_CASE_CONTROL:
        case ASCII_CASE_EXTENDED:
            /* ignore */
            break;

        case '-':
        default: /* break across punctuation */
        case ASCII_CASE_SPACE:
            /* word ended */
            if (*len) {
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD_NOSTOP;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto toplevel_label;
            }
            break;
        }
    }
    /* if we end in a word, we have to return it */
    if (*len) {
        word[*len] = '\0';
        parser->state = ENDFILE; /* transition to eof */
        return QUERYPARSE_WORD_NOSTOP;
    } else {
        /* it was empty, so warn them of that */
        parser->warn |= QUERYPARSE_WARN_NONWORD;
    }
    goto endfile_label;

/* in a word that should be excluded (pretty much the same as normal word) */
inword_exclude_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            c = ASCII_TOLOWER(c);
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD_EXCLUDE;
            }
            break;

        case ASCII_CASE_CONTROL:
        case ASCII_CASE_EXTENDED:
            /* ignore */
            break;

        default: 
            goto inword_exclude_punc_label;

        case ASCII_CASE_SPACE:
            /* word ended */
            if (*len) {
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD_EXCLUDE;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto toplevel_label;
            }
            break;
        }
    }
    /* if we end in a word, we have to return it */
    if (*len) {
        word[*len] = '\0';
        parser->state = ENDFILE; /* transition to eof */
        return QUERYPARSE_WORD_EXCLUDE;
    } else {
        /* it was empty, so warn them of that */
        parser->warn |= QUERYPARSE_WARN_NONWORD;
    }
    goto endfile_label;

/* in a word that should be excluded (pretty much the same as normal word) */
inword_exclude_punc_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            c = ASCII_TOLOWER(c);
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD_EXCLUDE;
            }
            break;

        case ASCII_CASE_CONTROL:
        case ASCII_CASE_EXTENDED:
            /* ignore */
            break;

        case '-':
        default: /* break across junk characters */
        case ASCII_CASE_SPACE:
            /* word ended */
            if (*len) {
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD_EXCLUDE;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto toplevel_label;
            }
            break;
        }
    }
    /* if we end in a word, we have to return it */
    if (*len) {
        word[*len] = '\0';
        parser->state = ENDFILE; /* transition to eof */
        return QUERYPARSE_WORD_EXCLUDE;
    } else {
        /* it was empty, so warn them of that */
        parser->warn |= QUERYPARSE_WARN_NONWORD;
    }
    goto endfile_label;

/* in what may be an OR operator */
inor_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = ASCII_TOLOWER(c);
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD;
            }

            if (c != OR_OP[*len - 1]) {
                /* it turned out not to be OR, go back to regular word 
                 * parsing */
                goto inword_label;
            }
            break;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD;
            }
            /* it turned out not to be OR, go back to regular word parsing */
            goto inword_label;
            break;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            goto inword_label;
        
        case '-':
        default:
            goto punc_label;

        case ASCII_CASE_SPACE:
            /* word ended */
            if (!OR_OP[*len]) {
                /* whitespace ended OR operator */
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_OR;
            } else if (*len) {
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto toplevel_label;
            }
            break;
        }
    }
    /* if we end in an unfinished OR, return it as word */
    assert(*len);
    word[*len] = '\0';
    parser->state = ENDFILE;
    return QUERYPARSE_WORD;

/* in what may be an AND operator */
inand_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = ASCII_TOLOWER(c);
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD;
            }

            if (c != AND_OP[*len - 1]) {
                /* it turned out not to be AND, go back to regular word 
                 * parsing */
                goto inword_label;
            }
            break;

        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD;
            }
            /* it turned out not to be AND, go back to regular word parsing */
            goto inword_label;
            break;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            goto inword_label;

        case '-':
        default:
            goto punc_label;

        case ASCII_CASE_SPACE:
            /* word ended */
            if (!AND_OP[*len]) {
                /* whitespace ended OR operator */
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_AND;
            } else if (*len) {
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto toplevel_label;
            }
            break;
        }
    }
    /* if we end in an unfinished AND, return it as word */
    assert(*len);
    word[*len] = '\0';
    parser->state = ENDFILE;
    return QUERYPARSE_WORD;

/* inside a phrase */
inphrase_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            c = ASCII_TOLOWER(c);
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = INPHRASE; /* transition back to phrase */
                return QUERYPARSE_WORD;
            }

            /* phrase word started */
            goto inphrase_word_label;
            break;

        case PHRASE_DELIM:
            /* phrase ended */
            goto inphrase_end_label;
            break;

        case ASCII_CASE_SPACE:
            /* silently eat whitespace characters */
            break;

        case ASCII_CASE_CONTROL:
        default:
        case ASCII_CASE_EXTENDED:
            /* phrase word started */
            goto inphrase_word_label;
            break;
        }
    }
    /* if we end in an unfinished phrase, warn them */
    parser->warn |= QUERYPARSE_WARN_QUOTES_UNMATCHED;
    goto endfile_label;
 
/* in a word, inside a phrase */
inphrase_word_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            c = ASCII_TOLOWER(c);
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = INPHRASE; /* transition back to phrase */
                return QUERYPARSE_WORD;
            }
            break;

        case PHRASE_DELIM:
            /* phrase ended */
            if (*len) {
                word[*len] = '\0';
                parser->state = INPHRASE_END;
                return QUERYPARSE_WORD;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto inphrase_end_label;
            }
            break;

        case '-':
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = INPHRASE; /* transition back to phrase */
                return QUERYPARSE_WORD;
            }
            break;

        case ASCII_CASE_EXTENDED:
        case ASCII_CASE_CONTROL:
            /* ignore */
            break;

        default: 
            goto inphrase_word_punc_label;

        case ASCII_CASE_SPACE:
            /* phrase word ended */
            if (*len) {
                word[*len] = '\0';
                parser->state = INPHRASE;
                return QUERYPARSE_WORD;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto toplevel_label;
            }
            break;

        case CASE_END_SENTENCE:
            /* watch out for the end of sentences */
            goto endsentence_label;
            break;
        }
    }
    /* if we end in a word in an unfinished phrase, warn them and return word */
    parser->warn |= QUERYPARSE_WARN_QUOTES_UNMATCHED;
    assert(*len);
    word[*len] = '\0';
    parser->state = ENDFILE;
    return QUERYPARSE_WORD;
 
inphrase_word_punc_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            c = ASCII_TOLOWER(c);
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = INPHRASE; /* transition back to phrase */
                return QUERYPARSE_WORD;
            }

            /* phrase word started */
            goto inphrase_word_label;
            break;

        case PHRASE_DELIM:
            /* phrase ended */
            if (*len) {
                word[*len] = '\0';
                parser->state = INPHRASE_END;
                return QUERYPARSE_WORD;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto inphrase_end_label;
            }
            break;

        case ASCII_CASE_CONTROL:
        case ASCII_CASE_EXTENDED:
            /* ignore */
            break;

        default: /* break across junk characters */
        case ASCII_CASE_SPACE:
            /* phrase word ended */
            if (*len) {
                word[*len] = '\0';
                parser->state = INPHRASE;
                return QUERYPARSE_WORD;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto toplevel_label;
            }
            break;

        case CASE_END_SENTENCE:
            /* watch out for the end of sentences */
            goto endsentence_label;
            break;
        }
    }
    /* if we end in a word in an unfinished phrase, warn them and return word */
    parser->warn |= QUERYPARSE_WARN_QUOTES_UNMATCHED;
    assert(*len);
    word[*len] = '\0';
    parser->state = ENDFILE;
    return QUERYPARSE_WORD;

/* need to return phrase end and then continue */
inphrase_end_label:
    parser->state = TOPLEVEL;
    return QUERYPARSE_END_PHRASE;

/* need to return sentence end and then continue */
endsentence_end_label:
    parser->state = INPHRASE;
    return QUERYPARSE_END_SENTENCE;

/* in what might be the end of a sentence */
endsentence_label:
    while (parser->buf < parser->end) {
        /* don't consume this character (yet) */
        c = *parser->buf;
        switch (c) {
        case ASCII_CASE_SPACE:
            /* it was the end of a sentence */
            parser->buf++;        /* consume the byte */
            parser->bytes++;
            assert(*len);
            word[*len] = '\0';
            parser->state = ENDSENTENCE_END;
            return QUERYPARSE_WORD;

        default:
            /* back to a normal phrase word */
            goto inphrase_word_label;
            break;
        }
    }
    /* finishing in endsentence means we need to return the phrase word */
    assert(*len);
    word[*len] = '\0';
    parser->state = INPHRASE;
    return QUERYPARSE_WORD;

/* parsing the modifier of a modifier */
inmod_mod_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            c = ASCII_TOLOWER(c);
        case '-':
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
        case ASCII_CASE_EXTENDED:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = TOPLEVEL; /* transition back to toplevel */
                return QUERYPARSE_WORD;
            }
            break;

        case MOD_STRING_END:
            /* end of the modifier in the mod */
            word[*len] = '\0';
            parser->state = INMOD;        /* transition to inmod */
            return QUERYPARSE_START_MODIFIER;
            break;

        case MOD_START:
            parser->warn |= QUERYPARSE_WARN_PARENS_NESTED; /* warn,fallthrough*/
        default:
        case ASCII_CASE_SPACE:
            /* it wasn't a mod after all (return word) */
            word[*len] = '\0';
            parser->state = TOPLEVEL;     /* transition back to toplevel */
            return QUERYPARSE_WORD;
            break;
        }
    }
    /* if we end in a mod, return as word */
    parser->warn |= QUERYPARSE_WARN_PARENS_UNMATCHED;
    assert(*len);
    word[*len] = '\0';
    parser->state = ENDFILE;
    return QUERYPARSE_WORD;

/* in the parameters of a modifier */
inmod_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            c = ASCII_TOLOWER(c);
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
        case ASCII_CASE_EXTENDED:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = INMOD; /* transition to this state */
                return QUERYPARSE_WORD;
            }

            /* start parsing word */
            goto inmod_word_label;
            break;

        case MOD_END:
            goto inmod_end_label;
            break;

        case ASCII_CASE_SPACE:
            /* silently eat whitespace */
            break;

        case MOD_START:
            parser->warn |= QUERYPARSE_WARN_PARENS_NESTED; /* warn,fallthrough*/
        default:
            /* anything else starts a word, but isn't recorded */
            goto inmod_word_label;
            break;
        }
    }
    /* if we end in a mod, return as word */
    parser->warn |= QUERYPARSE_WARN_PARENS_UNMATCHED;
    goto endfile_label;

/* in a word, in a modifier */
inmod_word_label:
    while (parser->buf < parser->end) {
        c = *parser->buf++;
        parser->bytes++;
        switch (c) {
        case ASCII_CASE_UPPER:
            c = ASCII_TOLOWER(c);
        case '-':
        case ASCII_CASE_LOWER:
        case ASCII_CASE_DIGIT:
            /* push character onto word */
            if (*len < parser->maxwordlen) {
                word[(*len)++] = c;
            } else {
                /* word finished due to length constraint */
                word[*len] = '\0';
                parser->state = INMOD; /* transition back to inmod */
                return QUERYPARSE_WORD;
            }
            break;

        case MOD_END:
            if (*len) {
                /* return recorded word */
                word[*len] = '\0';
                parser->state = INMOD_END; 
                return QUERYPARSE_WORD;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto inmod_end_label;
            }
            break;

        case '\'':
            /* don't break across apostrophe */
            break;

        case MOD_START:
            parser->warn |= QUERYPARSE_WARN_PARENS_NESTED; /* warn,fallthrough*/
        default: /* break across other crap */
        case ASCII_CASE_SPACE:
            if (*len) {
                /* return recorded word */
                word[*len] = '\0';
                parser->state = INMOD; 
                return QUERYPARSE_WORD;
            } else {
                /* it was empty, so warn them of that */
                parser->warn |= QUERYPARSE_WARN_NONWORD;
                goto inmod_label;
            }
            break;
        }
    }
    /* if we end in a mod, return as word */
    parser->warn |= QUERYPARSE_WARN_PARENS_UNMATCHED;
    assert(*len);
    word[*len] = '\0';
    parser->state = ENDFILE;
    return QUERYPARSE_WORD;

/* modifier has ended */
inmod_end_label:
    parser->state = TOPLEVEL;
    return QUERYPARSE_END_MODIFIER;

endfile_label:
    parser->state = ENDFILE;
    return QUERYPARSE_EOF;

err_label:
    if (!parser->err) {
        parser->err = EINVAL;
    }
    return ERR;
}

void queryparse_delete(struct queryparse *parser) {
    free(parser);
    return;
}

unsigned int queryparse_bytes(struct queryparse *parser) {
    return parser->bytes;
}

int queryparse_err(struct queryparse *parser) {
    return parser->err;
}

unsigned int queryparse_warn(struct queryparse *parser) {
    unsigned int tmp = parser->warn;
    parser->warn = 0;
    return tmp;
}

#ifdef QUERYPARSE_TEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "str.h"

const char *warn_mesg(unsigned int *warn) {
    if (*warn & QUERYPARSE_WARN_PARENS_BOOLEAN) {
        *warn ^= QUERYPARSE_WARN_PARENS_BOOLEAN;
        return "where you attempting a boolean query?";
    } else if (*warn & QUERYPARSE_WARN_PARENS_NESTED) {
        *warn ^= QUERYPARSE_WARN_PARENS_NESTED;
        return "attempt to nest square parentheses";
    } else if (*warn & QUERYPARSE_WARN_PARENS_UNMATCHED) {
        *warn ^= QUERYPARSE_WARN_PARENS_UNMATCHED;
        return "square parentheses aren't matched";
    } else if (*warn & QUERYPARSE_WARN_QUOTES_UNMATCHED) {
        *warn ^= QUERYPARSE_WARN_QUOTES_UNMATCHED;
        return "quotes aren't matched";
    } else if (*warn & QUERYPARSE_WARN_EMPTY_OP) {
        *warn ^= QUERYPARSE_WARN_EMPTY_OP;
        return "empty operation";
    } else if (*warn & QUERYPARSE_WARN_NONWORD) {
        *warn ^= QUERYPARSE_WARN_NONWORD;
        return "a string of junk was encountered";
    } else if (*warn) {
        *warn = 0;
        return "unexpected warning";
    } else {
        return NULL;
    }
}

int main() {
    char buf[BUFSIZ + 1];
    struct queryparse *qp;
    int ret;
    char word[100];
    unsigned int len,
                 warn;

    while (fgets(buf, BUFSIZ, stdin)) {
        str_rtrim(buf);
        printf("query: %s\n", buf);

        if (!(qp = queryparse_new(50, buf, str_len(buf)))) {
            fprintf(stderr, "couldn't initialise parser\n");
            return EXIT_FAILURE;
        }

        do {
            ret = queryparse_parse(qp, word, &len);
            warn = queryparse_warn(qp);

            switch (ret) {
            case QUERYPARSE_ERR:
                fprintf(stderr, "queryparse got error %d %s\n", 
                  queryparse_err(qp), strerror(queryparse_err(qp)));
                ret = QUERYPARSE_EOF;
                break;

            case QUERYPARSE_WORD:
                assert(len == str_len(word));
                printf("WORD: %s\n", word);
                break;

            case QUERYPARSE_WORD_NOSTOP:
                assert(len == str_len(word));
                printf("WORD_NS: %s\n", word);
                break;

            case QUERYPARSE_WORD_EXCLUDE:
                assert(len == str_len(word));
                printf("WORD_EX: %s\n", word);
                break;

            case QUERYPARSE_OR:
                printf("OR\n");
                break;

            case QUERYPARSE_AND:
                printf("AND\n");
                break;

            case QUERYPARSE_START_PHRASE:
                printf("PHRASE \"\n");
                break;

            case QUERYPARSE_END_PHRASE:
                printf("\" PHRASE\n");
                break;

            case QUERYPARSE_START_MODIFIER:
                assert(len == str_len(word));
                printf("MOD %s [\n", word);
                break;

            case QUERYPARSE_END_MODIFIER:
                printf("] MOD \n");
                break;

            case QUERYPARSE_END_SENTENCE:
                printf("ENDSENT\n");
                break;

            case QUERYPARSE_EOF:
                break;

            default:
                fprintf(stderr, "unexpected output!\n");
                ret = QUERYPARSE_EOF;
                break;
            }

            while (warn) {
                printf("%s\n", warn_mesg(&warn));
            }
        } while ((ret != QUERYPARSE_EOF));

        queryparse_delete(qp);
    }

    return EXIT_SUCCESS;
}
#endif

