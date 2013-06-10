/* mlparse.h declares a parser that parses SGML and XML based markup languages 
 * in a basic way
 *
 * SGML is defined in ISO 8879:1986, although you're probably already familiar
 * with (a subset of) it as the basis of HTML.  There is an informative book by
 * Charles Goldfarb (the 'G' in SGML, sort-of) entitled 'The SGML Handbook'
 * which contains the full text of the standard as well as annotations,
 * commentry and other material.  The BNF productions of SGML could be found
 * (last i checked) by searching for 'sgml productions' on the web.
 *
 * XML is defined in the W3C recommendation, found at 
 * http://www.w3.org/TR/REC-xml/.  It was designed to be compatible with SGML,
 * but far simpler (thank god!)
 *
 * short glossary:
 *   - tag: <name param=pval ... > construct, basic unit of markup, where 'name'
 *     identifies the type of tag that it is
 *   - param: parameter name within a tag (shown above)
 *   - pval: parameter value within a tag (shown above)
 *   - eref: entity reference, indirect reference to some text 
 *     (i.e. &amp; == '&')
 *   - marked section: obscure <![ label [ ... ]]> construct, where label is
 *     CDATA, RCDATA, IGNORE, INCLUDE or TEMP
 *   - declaration, decl: <!declaration ...> construct, like a tag with the 
 *     name starting with '!' (although thats over simplification)
 *
 * The parser attempts to extract entities (tags, comments, marked sections and
 * other data) from SGML-like data.  HTML and numeric entity references are
 * recognised and replaced with the relevant text.  Non-markup data is seperated
 * by tokenising it into words and whitespace, where a word is delimited by
 * whitespace, but contains at most two consecutive punctuation marks (so 'wo
 * rd' == wo, rd, 'wo.rd' == word and 'wo..rd' == wo, rd).  If a word exceeds
 * the wordlen (and hence the buffer space provided) specified, it is returned
 * in as many different chunks as necessary, with a flag set to indicate that
 * this has occurred.  If stripping is requested, the parser will remove all 
 * punctuation from the words, and will 
 * case-fold uppercase letters to lowercase if CASE_FOLD is defined (see def.h).
 * Whitespace is not returned when stipping is enabled, and neither are words 
 * where the entire content has been stripped (note that this means you may get 
 * different word counts for stripping and non-stripping operation).  Control
 * characters are removed from the output stream regardless of whether stripping
 * is enabled, as they don't really make any sense under non-obscure character
 * encodings (which we make no attempt to deal with).  Sentence ending 
 * punctuation ('.', '?' or '!') followed by whitespace is counted as 
 * the end of a sentence and is indicated in the return value (see below).  
 * This is so that phrase matching can avoid matches over sentence/paragraph 
 * boundaries.  An exception to this rule is if the word looks like an acronym 
 * (i.e. U.S.), which the parser recognises by looking for words with capital 
 * letters only with every second letter a period ('.').  These hueristics
 * aren't perfect, but they cover the majority of observed cases.
 *
 * Declarations and marked sections other than CDATA aren't currently
 * recognised, although most declarations will be returned as tags.  Parmeter
 * entity references are also not supported.  There is no reason other than 
 * the time and effort involved why these things couldn't be supported.  Some 
 * hueristics are applied when detecting comments, tags and CDATA sections 
 * (primarily looking ahead in the text for the corresponding end 
 * tag/comment/section).  This is useful when parsing dirty HTML, which 
 * can contain any number of nasty surprises like this.  The parser tries to 
 * err on the side of *not* recognising tags and comments, in which case they 
 * will be returned as words.
 *
 * Parameters and parameter values occurring within tags are treated like words
 * and are returned in whitespace delimited words (with only two consecutive
 * punctuation marks), although subject to parameter delimiters as well.  No end
 * of sentence processing is performed.  Stripping is performed if requested,
 * although some characters are stripped out of parameter names and
 * whitespace delimited values, since they are illegal in SGML/XML.  Entity
 * references are recognised within (double-)quote delimited values.  Bear in
 * mind that SGML allows parameter values to appear like parameter names so long
 * as the context is unambiguous (i.e. <p center> where the full tag is 
 * <p align='center'>) so you may have to be prepared to interpret parameters as
 * parameter values.
 *
 * Comments must be XML comments (<!-- ... --> whereas SGML allows -- to open
 * and close comments anywhere within a declaration 
 * (i.e. <!decl -- SGML, but not XML comment -- >).  Data within comments or
 * CDATA section (<![ CDATA [ cdata section data ]]>) are broken into words the
 * same as parameter values, except that no markup (entity references) is 
 * recognised within them.  SGML CDATA sections are supposed to nest, but the
 * parser doesn't allow this.
 *
 * Known bugs and quirks:
 *   - mlparse_eof may have to be called multiple times
 *   - mlparse_eof can *only* be called when the parser has returned
 *     MLPARSE_INPUT
 *   - CDATA sections don't nest
 *   - TEMP, RCDATA, INCLUDE and IGNORE sections aren't recognised
 *   - SGML declarations aren't handled properly (which also affects comments)
 *   - <!> comments (yes, SGML says that this is an empty comment) is returned
 *     as tag '!'
 *   - removing extended character set to cut down on the produced vocabulary
 *     (proper handling of the extended character set requires recognising
 *     punctuation and so on in it, which is beyond what i'm currently willing
 *     to do)
 *
 * Note: mlparse doesn't handle extremely short word lengths (i.e. 4 characters
 * or less) very well, so i suggest you make word length a reasonable amount.
 *
 * written nml 2003-03-15
 *
 */

#ifndef MLPARSE_H
#define MLPARSE_H

#ifdef __cplusplus
extern "C" {
#endif

/* possible return values from mlparse_parse.  Explanation:
 *
 *    ERR: something bad and unexpected happened.  Contact the developers,
 *    including the text that caused this error and output of mlparse_err_line.
 *
 *    INPUT: more input is required before the parser can proceed.  Always
 *    returned by itself.
 *
 *    EOF: hit the end of the file.
 *
 *    TAG: got an SGML tag.  word now contains the name of
 *    the element.  self ending tags (i.e. <p/>) are detected and correctly 
 *    returned as, in this case, </p>, after the parameters have been processed.
 *
 *    WORD: got a word, which has been copied into the word buffer.  It 
 *    shouldn't be empty but may contain some markup characters if the parser
 *    couldn't make sense of it.  
 *
 *    PARAM: got a parameter in a structural element, which has been
 *    copied into the word buffer.  It should only happen
 *    after a TAG, after which zero or more PARAMs may occur.  May actually be a
 *    paramval, since in SGML you can't tell without understanding the DTD.
 *
 *    PARAMVAL: got a parameter value, which has now been copied into
 *    the word buffer.  It should only occur after a PARAM,
 *    after which zero or more PARAMVALs may occur.  
 *
 *    COMMENT: received a comment start or end.  Comments contain zero or more 
 *    WORDs and WHITESPACE tokens.
 *
 *    CDATA: received a cdata section start or end.   CDATA sections contain
 *    zero or more WORDSs and WHITESPACE tokens.
 *
 *    WHITESPACE: received some whitespace (only returned if not stripping)
 * 
 *    OK: only used in utility functions (not _parse), indicates success
 *
 *    FLAGS:
 * 
 *      END: indicates that the end of an entity was reached.  With COMMENT
 *      or CDATA means end of the comment/cdata section.  With WORD means the
 *      end of the sentence.  Shouldn't be returned with anything else.
 *
 *      CONT: indicates that the entity overflowed the provided buffer, and that
 *      only the first section has been copied into the word buffer.  The rest
 *      of the entity should follow.  Should be returned only with things that
 *      return stuff in the buffer (word, whitespace, param, paramval, tag).
 *
 */
enum mlparse_ret {
    MLPARSE_ERR = 0, 
    MLPARSE_OK = (10 << 2),

    /* flags */
    MLPARSE_CONT = 1,
    MLPARSE_END = 2, 

    /* need input */
    MLPARSE_INPUT = (1 << 2),

    /* entities */
    MLPARSE_TAG = (2 << 2), 
    MLPARSE_WORD = (3 << 2), 
    MLPARSE_PARAM = (4 << 2), 
    MLPARSE_PARAMVAL = (5 << 2), 
    MLPARSE_COMMENT = (6 << 2), 
    MLPARSE_WHITESPACE = (7 << 2),
    MLPARSE_CDATA = (8 << 2),

    MLPARSE_EOF = (9 << 2)
};

struct mlparse {
    const char *next_in;                 /* next input buffer */
    unsigned int avail_in;               /* how much data is in input buffer */
    struct mlparse_state *state;         /* internal parser state */
};

/* create a new parser that breaks entities up into chunks of size wordlen, and
 * looks at most lookahead characters ahead to validate entities.  (the parser
 * will use approximately 2 * wordlen + 2 * lookahead bytes of RAM) */
int mlparse_new(struct mlparse *space, unsigned int wordlen, 
  unsigned int lookahead);

/* clear parser state */
void mlparse_reinit(struct mlparse *parser);

/* return the next entity from the file (see comment about return
 * types above).  Word buffer must be at least size wordlen and is not NUL
 * terminated on return.  length indicates how large the returned entity was.
 * strip is boolean as to whether you would like the next entity returned
 * stripped of some characters and whitespace (see header comment).  strip and 
 * the word buffer must be preserved across calls of mlparse_parse where the 
 * return value is MLPARSE_INPUT, or else you'll get junk out.  If 
 * MLPARSE_INPUT is returned and you've hit the end of the file you are 
 * parsing, call mlparse_eof. */
int mlparse_parse(struct mlparse *parser, char *word, unsigned int *length, 
  int strip);

/* indicate to the parser that end of file has occurred.  Must be called
 * immediately after mlparse_parse has returned MLPARSE_INPUT. */
void mlparse_eof(struct mlparse *parser);

/* destroy the parser */
void mlparse_delete(struct mlparse *parser);

/* return the number of bytes buffered in the parser (so you can tell how many
 * bytes have been parsed at any given point) */
unsigned long int mlparse_buffered(struct mlparse *parser);

/* returns a pointer into the internal buffer of the parser */
const char *mlparse_buffer(struct mlparse *parser);

unsigned int mlparse_err_line(struct mlparse *parser);

#ifdef __cplusplus
}
#endif

#endif

