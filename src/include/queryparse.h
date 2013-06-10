/* queryparse.h declares an interface for a parser that supports a
 * google-like query syntax
 *
 * the supported syntax is:
 *    - words and operators are seperated by whitespace
 *    - words are returned with everything but alphanumeric, '-' and
 *      extended characters removed.  Upper case characters are
 *      converted to lowercase
 *    - +word indicates that the word is to be included regardless of stopping
 *    - -word indicates that results containing the word are to be excluded
 *    - "a phrase" indicates that the contained words are to be treated as a 
 *      phrase
 *    - [mod:word1 word2] indicates that the query is to be modified by
 *      mod, with parameters word1 and word2 (which will be
 *      subsequently returned as words
 *    - nesting of operators is not allowed
 *    - round parentheses () are not allowed (particularly with boolean 
 *      operators) to prevent queries of arbitrary complexity
 *    - hueristic end-of-sentence detection is used while parsing phrases
 *
 * note that this parser must match the word parsing of the other
 * parsers, that are used to parse documents (currently mlparse)
 *
 * written nml 2003-07-08
 *
 */

#ifndef QUERYPARSE_H
#define QUERYPARSE_H

struct queryparse;

enum queryparse_ret {
    QUERYPARSE_ERR = 0,                    /* parser received an error */
    QUERYPARSE_EOF = 1,                    /* parser received end-of-file */
    QUERYPARSE_WORD = 2,                   /* parser received a word */
    QUERYPARSE_WORD_NOSTOP = 3,            /* parser received a word that the 
                                            * querier didn't want stopped */
    QUERYPARSE_WORD_EXCLUDE = 4,           /* parser received a word that the 
                                            * querier wants excluded from 
                                            * results */
    QUERYPARSE_OR = 5,                     /* parser received the OR operator */
    QUERYPARSE_AND = 6,                    /* parser received the AND operator*/
    QUERYPARSE_START_PHRASE = 7,           /* parser received the start of a 
                                            * phrase */
    QUERYPARSE_END_PHRASE = 8,             /* parser received the end of a 
                                            * phrase */
    QUERYPARSE_START_MODIFIER = 9,         /* parser received a query modifier*/
    QUERYPARSE_END_MODIFIER = 10,          /* parser received the end of a query
                                            * modifier */
    QUERYPARSE_END_SENTENCE = 11           /* parser received the end of a 
                                              sentence */
};

/* warnings */
enum queryparse_warn {
    QUERYPARSE_WARN_PARENS_BOOLEAN = (1 << 0),   /* warning: round parens used 
                                                  * in fashion that looks like 
                                                  * a boolean search attempt */
    QUERYPARSE_WARN_PARENS_NESTED = (1 << 1),    /* warning: nested parens */
    QUERYPARSE_WARN_PARENS_UNMATCHED = (1 << 2), /* warning: unmatched parens */
    QUERYPARSE_WARN_QUOTES_UNMATCHED = (1 << 3), /* warning: unmatched quotes */
    QUERYPARSE_WARN_EMPTY_OP = (1 << 4),         /* operator with empty param */
    QUERYPARSE_WARN_NONWORD = (1 << 5)           /* warning: found sequence 
                                                  * that wasn't a word*/
};

/* construct a new queryparser to parse query, which is of length
 * querylen.  words will be limited to maxwordlen bytes */
struct queryparse *queryparse_new(unsigned int maxwordlen, 
  const char *query, unsigned int querylen);

/* retrieve the next entity from parser.  See enum comments for meaning
 * of return code.  In all cases where code needs to return a string,
 * it will be stored in word, NULL terminated and the length stored in
 * len.  These cases are: _WORD, _WORD_NOSTOP, _WORD_EXCLUDE,
 * START_MODIFIER */
enum queryparse_ret queryparse_parse(struct queryparse *parser, 
  char *word, unsigned int *len);

/* destruct a query parser */
void queryparse_delete(struct queryparse *parser);

/* returns the number of bytes processed by the parser thus far */
unsigned int queryparse_bytes(struct queryparse *parser);

/* retuns the last err or 0 */
int queryparse_err(struct queryparse *parser);

/* returns a bitset with OR'd errors or 0 for none.  The warnings are then
 * cleared from the parser. */
unsigned int queryparse_warn(struct queryparse *parser);

#endif

