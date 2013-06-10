/* mlparse_wrap.h declares a module that wraps the mlparse module with some 
 * convenience code that takes care of buffering and IO
 *
 * written nml 2004-05-13
 *
 */

#ifndef MLPARSE_WRAP_H
#define MLPARSE_WRAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "mlparse.h"

#include <stdio.h>

struct mlparse_wrap;

/* create a wrapper from an fd, using bufsize allocated space for buffering.
 * Limit specifies how much to read (or 0 for no limit) */
struct mlparse_wrap *mlparse_wrap_new_fd(unsigned int wordlen, 
  unsigned int lookahead, int fd, unsigned int bufsize, unsigned int limit);

/* create a wrapper from a file pointer, using bufsize allocated space for 
 * buffering.  Limit specifies how much to read (or 0 for no limit) */
struct mlparse_wrap *mlparse_wrap_new_file(unsigned int wordlen, 
  unsigned int lookahead, FILE *fp, unsigned int bufsize, unsigned int limit);

/* reinitialise an mlparse_wrapper to read from a different fd, with a different
 * byte limit */
void mlparse_wrap_reinit_fd(struct mlparse_wrap *wrapper, int fd, 
  unsigned int limit);

/* reinitialise an mlparse_wrapper to read from a different file pointer, with 
 * a different byte limit */
void mlparse_wrap_reinit_file(struct mlparse_wrap *wrapper, FILE *fp, 
  unsigned int limit);

/* get next entity from the parser.  Returns the same things as
 * mlparse_parse, except that MLPARSE_INPUT returns will be
 * intercepted and dealt with */
enum mlparse_ret mlparse_wrap_parse(struct mlparse_wrap *wrapper, 
  char *buf, unsigned int *len, int strip);

/* return the number of bytes parsed thus far */
unsigned int mlparse_wrap_bytes(struct mlparse_wrap *wrapper);

/* delete the wrapper */
void mlparse_wrap_delete(struct mlparse_wrap *wrapper);

#ifdef __cplusplus
}
#endif

#endif

