/* uri.h defines a set of methods for parsing and working with Uniform
 * Resource Identifiers (as defined in RFC 3896).
 *
 * Before attempting to work with this interface, you probably need to know a
 * little about URI's.  A URI is a string that points to a resource.  It can
 * have a number of components, being the scheme, authority, path, query and
 * fragment.  For example:
 *
 *    http://nml:xxx@blah.com:80/stuff/morestuff?queryparams#fragment
 *    ^--^   ^-----------------^^--------------^ ^---------^ ^------^
 *      |            |             |                  |         |
 *     scheme    authority       path               query    fragment
 *
 * Note that the path contains the first / after the authority, but the query
 * and fragment don't contain ? and # respectively.  The situation is
 * complicated by a number of things.  Firstly, URI's don't have to contain all
 * of these components.  Not one single component is actually required in a 
 * URI, so you can't assume that you're going to get them.
 * Secondly, the authority can be further broken down into
 * either an (optional) userinfo field (nml:xxx in the example), hostname or
 * IPv4 address or IPv6 address (surrounded by square brackets) and (optional)
 * port number (preceded by a colon).  
 *
 * Thanks to the large number of people who use incorrect URI's in real life,
 * the parser makes a number of deviations from the RFC's.  
 *   - square brackets are allowed in the path (this is a misinterpretation of
 *     RFC 2732) (including opaque path components)
 *   - backslashes are accepted and treated like forward slashes in all cases
 *     (thanks windows users!)
 *   - pipes (|) are accepted in path components (including opaque ones),
 *     queries and fragments (apparently we have netscape to thank for this)
 *
 * all of these 'features' can be disabled by defining URI_STRICT when 
 * compiling uri.c
 *
 * written nml 2003-06-02
 *
 */

#ifndef URI_H
#define URI_H

enum uri_ret {
    URI_OK = 0,               /* operation succeeded */

    URI_ERR = -1,             /* unexpected error */
    URI_PARSE_ERR = -2,       /* parsing error */
    URI_CHAR_ERR = -3,        /* illegal character in input (an important 
                               * subset of parsing error) */
    URI_MEM_ERR = -4          /* can't allocate memory */
};

/* enumeration of the different parts of a URI, see top comment for detail */
enum uri_part {
    URI_PART_SCHEME = (1 << 0),
    URI_PART_USERINFO = (1 << 1),
    URI_PART_HOST = (1 << 2),
    URI_PART_PORT = (1 << 3),
    URI_PART_PATH = (1 << 4),
    URI_PART_QUERY = (1 << 5),
    URI_PART_FRAGMENT = (1 << 6)
};

/* each component, each seperator */
struct uri_parsed {
    unsigned int scheme_len;     /* length of scheme component */
    unsigned int userinfo_len;   /* length of userinfo component */
    unsigned int host_len;       /* length of host component */
    unsigned int port_len;       /* length of port component */
    unsigned int path_len;       /* length of path component */
    unsigned int query_len;      /* length of query component */
    unsigned int frag_len;       /* length of fragment component */

    int seps;                    /* track which seperators have occurred 
                                  * (note that just because a component has 
                                  * zero length, doesn't mean that it's 
                                  * seperator hasn't occurred) */
    int flags;                   /* track some properties of the uri */
};

/* parse the given uri, of length uri_len, and fill in the given parse 
 * structure on successful return */
enum uri_ret uri_parse(const char *uri, unsigned int uri_len, 
  struct uri_parsed *parse);

/* normalise a URI, overwriting the existing data.  uri_len should be set to 
 * the length of the uri on input,
 * and will be set to the (never longer) new length of the uri upon successful
 * return.  The parse structure will also be updated to be appropriate for the
 * new uri.  Normalisation consists of:
 *
 *   - lowercasing the hostname and scheme
 *   - removing superfluous separators 
 *   - removing superfluous encoded characters
 *   - changing '\' to '/' (slightly non-standard)
 *   - removing standard ports
 *   - removing leading zeros from ports
 *   - modifying a path of '/' to the empty path (slightly non-standard)
 *   - removing '.' and '..' segments from the path
 *   - translating repeated slashes into single slashes (i.e. '//' -> '/')
 *
 */
enum uri_ret uri_normalise(char *uri, struct uri_parsed *parse);
#define uri_normalize uri_normalise

/* normalise a path (remove '.' and '..' segments).  path_len should be set to
 * the length of the uri when passed in, and will be set to the correct (never
 * longer) length upon successful return) */
enum uri_ret uri_path_normalise(char *path, unsigned int *path_len);
#define uri_path_normalize uri_path_normalise

/* extract the selected part of the given uri (with the parse structure filled
 * in using uri_parse).  Result will be written into dst, up to a limit of
 * dst_max_len chars, with the untruncated length of the total result written
 * into dst_len on successful return. */
enum uri_ret uri_part_decode(const char *uri, const struct uri_parsed *parse, 
  enum uri_part part, 
  char *dst, unsigned int dst_max_len, unsigned int *dst_len);

/* return the length of the URI */
unsigned int uri_length(const struct uri_parsed *parse);

/* whether uri is relative (true) or absolute (false) */
int uri_relative(const struct uri_parsed *parse);

/* whether uri is hierarchical */
int uri_hierarchical(const struct uri_parsed *parse);

/* append 'end' uri to 'base' uri, altering the base uri in the process.  
 * base character array is expected to have sufficient space to hold 
 * the resultant uri (in other words, it should be of size
 * uri_append_length(...) or greater).  On success, base will be the 
 * well-formed resultant uri, on error it probably contains rubbish. */
enum uri_ret uri_append(char *base, struct uri_parsed *base_parse,
  const char *end, const struct uri_parsed *end_parse);

/* method to indicate the potential length of a combined uri created by
 * appending */
unsigned int uri_append_length(char *base, struct uri_parsed *base_parse,
  const char *end, const struct uri_parsed *end_parse);

/* append path 'end' to path 'base', altering base in the process.
 * base character array is expected to have sufficient space to hold 
 * the resultant path (in other words, it should be of size
 * uri_path_append_length(...) or greater).  base will then be the 
 * well-formed resultant path, no errors are possible. */
void uri_path_append(char *base, unsigned int *base_len, 
  const char *end, unsigned int end_len, int forward, int back);
unsigned int uri_path_append_length(char *base, unsigned int base_len, 
  const char *end, unsigned int end_len, int forward, int back);

#endif

