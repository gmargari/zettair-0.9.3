/* _uri.h contains the implementation details of uri.c 
 *
 * written nml 2003-06-2
 *
 */

#ifndef PRIVATE_URI_H
#define PRIVATE_URI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "uri.h"

/* same as the uriparse_new constructor, except that it doesn't malloc
 * the returned parser or uri. */
struct uriparse *uriparse_new_inplace(struct uriparse *parser, 
  struct uri *uri);

/* use with the uriparse_new_inplace constructor */
struct uri *uriparse_delete_inplace(struct uriparse *parser);

/* free all resources used by a uri */
void uri_delete_inplace(struct uri *uri);

/* copy uri into given space, returning a pointer to space on
 * success, and NULL on failure */
struct uri *uri_copy_inplace(const struct uri *uri, struct uri *space);

/* note: strings require lengths because all except host and scheme can contain
 * encoded characters (which can encode the '\0' character).  using short int
 * (to save space - this is a large structure) limits us to URLs of 65534
 * characters or less, but this doesn't seem likely to cause a problem) */
typedef unsigned short int uri_ushort;
struct uri {
    int port;                    /* port number (or -1 for no port) */
    uri_ushort *split_path;      /* path string split by slashes */
    char *buf;                   /* buffer where all strings are held */
    uri_ushort scheme;           /* scheme string */
    uri_ushort scheme_len;       /* length of scheme string */
    uri_ushort userinfo;         /* userinfo string */
    uri_ushort userinfo_len;     /* length of userinfo string */
    uri_ushort host;             /* host string */
    uri_ushort host_len;         /* length of host string */
    uri_ushort reg;              /* non-server authority */
    uri_ushort reg_len;          /* length of non-server authority string */
    uri_ushort path;             /* path string */
    uri_ushort path_len;         /* length of path string */
    uri_ushort split_len;        /* number of split path elements */
    uri_ushort query;            /* query string */
    uri_ushort query_len;        /* length of query string */
    uri_ushort frag;             /* fragment string */
    uri_ushort frag_len;         /* length of fragment string */
    uri_ushort buflen;           /* how many bytes the buf has */
};

struct uriparse {
    struct uri *uri;             /* uri we're parsing into */
    int parse_state;             /* state in our parser */
    int next_state;              /* next state for meta-states that can 
                                  * transition to multiple next states */
    unsigned int state_count;    /* number of matches for the current state */
    int err;                     /* last error as errno code or 0 if none */
    unsigned int bytes;          /* how many bytes of uri there were */
    unsigned int bufsize;        /* how many bytes the uri buf can take */
    unsigned int split_size;     /* how many elements in the path array */
    int flags;                   /* flags indicating some hacky but necessary 
                                  * stuff */
};

#ifdef __cplusplus
}
#endif

#endif

