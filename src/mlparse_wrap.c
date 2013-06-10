/* mlparse_wrap.c implements a wrapper around mlparse to handle IO
 *
 * written nml 2004-05-13
 *
 */

#include "firstinclude.h"

#include "mlparse_wrap.h"

#include <assert.h>
#include <stdlib.h>

struct mlparse_wrap {
    void *src;                     /* pointer to source of data */
    void *buf;                     /* allocated buffer */
    unsigned int bufsize;          /* size of allocated buffer */
    struct mlparse parser;         /* wrapped parser */
    unsigned int bytes;            /* number of bytes read from src */
    unsigned int limit;            /* number of bytes to read if limited */
    int limited;                   /* whether we're limiting the number of 
                                    * bytes we read */
    int fd;                        /* space to store an fd value */
    unsigned int (*fillbuf)        /* method to fill the buffer from the src */
      (void *src, void *buf, unsigned int len);
};

static unsigned int stdio_fillbuf(void *src, void *buf, unsigned int len) {
    return fread(buf, 1, len, src);
}

static unsigned int fd_fillbuf(void *src, void *buf, unsigned int len) {
    int *fd = src;
    int rlen;

    if ((rlen = read(*fd, buf, len)) > 0) {
        return rlen;
    } else {
        return 0;
    }
}

void mlparse_wrap_reinit_fd(struct mlparse_wrap *wrapper, int fd, 
  unsigned int limit) {
    wrapper->bytes = 0;
    wrapper->limited = limit;
    wrapper->limit = limit;
    wrapper->src = &wrapper->fd;
    wrapper->fd = fd;
    wrapper->fillbuf = fd_fillbuf;
    wrapper->parser.avail_in = 0;
    wrapper->parser.next_in = NULL;
    mlparse_reinit(&wrapper->parser);
}

void mlparse_wrap_reinit_file(struct mlparse_wrap *wrapper, FILE *fp, 
  unsigned int limit) {
    wrapper->bytes = 0;
    wrapper->limited = limit;
    wrapper->limit = limit;
    wrapper->src = fp;
    wrapper->fillbuf = stdio_fillbuf;
    wrapper->parser.avail_in = 0;
    wrapper->parser.next_in = NULL;
    mlparse_reinit(&wrapper->parser);
}

struct mlparse_wrap *mlparse_wrap_new_fd(unsigned int wordlen, 
  unsigned int lookahead, int fd, unsigned int bufsize, unsigned int limit) {
    struct mlparse_wrap *wrapper = malloc(sizeof(*wrapper));

    if (wrapper && (wrapper->buf = malloc(bufsize))
      && mlparse_new(&wrapper->parser, wordlen, lookahead)) {
        wrapper->bufsize = bufsize;
        mlparse_wrap_reinit_fd(wrapper, fd, limit);
    } else {
        if (wrapper) {
            if (wrapper->buf) {
                free(wrapper->buf);
            }
            free(wrapper);
        }
        wrapper = NULL;
    }

    return wrapper;
}

struct mlparse_wrap *mlparse_wrap_new_file(unsigned int wordlen, 
  unsigned int lookahead, FILE *fp, unsigned int bufsize, unsigned int limit) {
    struct mlparse_wrap *wrapper = malloc(sizeof(*wrapper));

    if (wrapper && (wrapper->buf = malloc(bufsize))
      && mlparse_new(&wrapper->parser, wordlen, lookahead)) {
        wrapper->bufsize = bufsize;
        mlparse_wrap_reinit_file(wrapper, fp, limit);
    } else {
        if (wrapper) {
            if (wrapper->buf) {
                free(wrapper->buf);
            }
            free(wrapper);
        }
        wrapper = NULL;
    }

    return wrapper;
}

enum mlparse_ret mlparse_wrap_parse(struct mlparse_wrap *wrapper, 
  char *buf, unsigned int *len, int strip) {
    enum mlparse_ret ret;
    unsigned int readlen;

    while ((ret 
      = mlparse_parse(&wrapper->parser, buf, len, strip)) == MLPARSE_INPUT) {
        assert(!wrapper->parser.avail_in);

        /* figure out how much to read (implement limit) */
        readlen = wrapper->bufsize;
        if (wrapper->limited 
          && (wrapper->bufsize + wrapper->bytes > wrapper->limit)) {
            readlen = wrapper->limit - wrapper->bytes;
        }

        if ((readlen 
          = wrapper->fillbuf(wrapper->src, wrapper->buf, readlen))) {
            wrapper->parser.avail_in = readlen;
            wrapper->parser.next_in = wrapper->buf;
            wrapper->bytes += readlen;
        } else {
            mlparse_eof(&wrapper->parser);
        }
    }

    buf[*len] = '\0';
    return ret;
}

unsigned int mlparse_wrap_bytes(struct mlparse_wrap *wrapper) {
    assert(wrapper->bytes >= wrapper->parser.avail_in 
      + mlparse_buffered(&wrapper->parser));

    return wrapper->bytes - wrapper->parser.avail_in 
      - mlparse_buffered(&wrapper->parser);
}

void mlparse_wrap_delete(struct mlparse_wrap *wrapper) {
    mlparse_delete(&wrapper->parser);
    free(wrapper->buf);
    free(wrapper);
}

#ifdef MLPARSE_WRAP_TEST
#undef MLPARSE_TEST_OUTPUT
#define WORDLEN 50
#define LOOKAHEAD 999

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>

void parse(struct mlparse_wrap *parser, FILE *output) {
    enum mlparse_ret parse_ret;      /* value returned by the parser */
    char word[WORDLEN + 1];          /* word returned by the parser */
    unsigned int len;                /* length of word */
    int print = 0;

#ifdef MLPARSE_TEST_OUTPUT
    print = 1;
#endif

    while (((parse_ret = mlparse_wrap_parse(parser, word, &len, 1)) 
        != MLPARSE_ERR) 
      && (parse_ret != MLPARSE_EOF)) {
        switch (parse_ret) {
        case MLPARSE_WORD:
        case MLPARSE_END | MLPARSE_WORD:
        case MLPARSE_CONT | MLPARSE_WORD:
            word[len] = '\0';
            if (print) {
                fprintf(output, "WORD = %s (%u)%s\n", word, len, 
                  parse_ret & MLPARSE_CONT ? " (cont)" 
                    : ((parse_ret & MLPARSE_END) ? " (endsentence)" : ""));
            }
            break;

        case MLPARSE_TAG:
        case MLPARSE_CONT | MLPARSE_TAG:
            word[len] = '\0';
            if (print) {
                fprintf(output, "TAG = %s (%u)%s\n", word, len, 
                  parse_ret & MLPARSE_CONT ? " (cont)" : "");
            }
            break;

        case MLPARSE_END | MLPARSE_TAG:
        case MLPARSE_END | MLPARSE_CONT | MLPARSE_TAG:
            word[len] = '\0';
            if (print) {
                fprintf(output, "ENDTAG = %s (%u)%s\n", word, len, 
                  parse_ret & MLPARSE_CONT ? " (cont)" : "");
            }
            break;

        case MLPARSE_PARAM:
        case MLPARSE_CONT | MLPARSE_PARAM:
            word[len] = '\0';
            if (print) {
                fprintf(output, "PARAM = %s (%u)%s\n", word, len, 
                  parse_ret & MLPARSE_CONT ? " (cont)" : "");
            }
            break;

        case MLPARSE_PARAMVAL:
        case MLPARSE_CONT | MLPARSE_PARAMVAL:
            word[len] = '\0';
            if (print) {
                fprintf(output, "PARAMVAL = %s (%u)%s\n", word, len, 
                  parse_ret & MLPARSE_CONT ? " (cont)" : "");
            }
            break;

        case MLPARSE_COMMENT:
            if (print) {
                fprintf(output, "START_COMMENT\n");
            }
            break;

        case MLPARSE_END | MLPARSE_COMMENT:
            if (print) {
                fprintf(output, "END_COMMENT\n");
            }
            break;

        case MLPARSE_WHITESPACE:
        case MLPARSE_CONT | MLPARSE_WHITESPACE:
            word[len] = '\0';
            if (print) {
                fprintf(output, "WHITESPACE = '%s' (%u)%s\n", word, len, 
                  parse_ret & MLPARSE_CONT ? " (cont)" : "");
            }
            break;

        case MLPARSE_CDATA:
            if (print) {
                fprintf(output, "START_CDATA\n");
            }
            break;

        case MLPARSE_CDATA | MLPARSE_END:
            if (print) {
                fprintf(output, "END_CDATA\n");
            }
            break;

        default:
            fprintf(stderr, "unknown retval %d\n", parse_ret);
            return;
        }
    }

    if (parse_ret == MLPARSE_ERR) {
        fprintf(stderr, "parser failed at %u\n", mlparse_wrap_bytes(parser));
    }

    fputc('\n', output);
    return;
}

int main(int argc, char **argv) {
    unsigned int i;
    struct mlparse_wrap *wrapper;
    int fd;

    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (((fd = open(argv[i], O_RDONLY)) >= 0)
              && (wrapper 
                = mlparse_wrap_new_fd(WORDLEN, LOOKAHEAD, fd, BUFSIZ, 0))) {

                parse(wrapper, stdout);
                mlparse_wrap_delete(wrapper);
                close(fd);
            } else if (fd >= 0) {
                fprintf(stderr, "failed to open parser on %s: %s\n", argv[i], 
                  strerror(errno));
                close(fd);
                return EXIT_FAILURE;
            } else {
                fprintf(stderr, "failed to open %s: %s\n", argv[i], 
                  strerror(errno));
                return EXIT_FAILURE;
            }
        }
    } else {
        if ((wrapper 
          = mlparse_wrap_new_fd(WORDLEN, LOOKAHEAD, STDIN_FILENO, BUFSIZ, 0))) {
            parse(wrapper, stdout);
            mlparse_wrap_delete(wrapper);
        } else {
            fprintf(stderr, "failed to open parser on stdin\n");
            return EXIT_FAILURE;
        } 
    }

    return EXIT_SUCCESS;
}

#endif


