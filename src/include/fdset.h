/* fdset.h manages a set of file descriptions, allowing a maximum
 * number of them to be open at one time, whilst not requiring that
 * they all be open at once.  It attempts to minimise the number of times that
 * file descriptors are re-opened by caching them internally.  
 * It draws a distinction between different types of file descriptors, 
 * which allows in the search engine domain to have seperate numbering for 
 * repository and index file descriptors
 *
 * written nml 2004-02-13
 *
 */

#ifndef FDSET_H
#define FDSET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>  /* for whence values */
#include <sys/types.h>  /* for off_t */

enum {
    FDSET_OK = 0
};

struct fdset;

/* FIXME: security of file creation */

/* create a new fdset.  umask is used as the creation mask for any fds that need
 * to be created.  sizehint is a hint as to how many file descriptors will be
 * open. */
struct fdset *fdset_new(int umask, unsigned int sizehint);

/* delete an fdset object */
void fdset_delete(struct fdset *set);

/* set a general prefix for the type, which will be used with the
 * fileno to generate a default filename (used when no name set with
 * set_fd_name is present).  Returns FDSET_OK on success or -errno on 
 * failure. */
int fdset_set_type_name(struct fdset *set, unsigned int type, 
  const char *name, unsigned int namelen, int write);

/* create a new type, assigning it a name made up of "basename" and
   "suffix".  The number of the new type is returned in "type".  
   Returns FDSET_OK on success, -errno on failure. */
int fdset_create_new_type(struct fdset *set, const char *basename,
  const char *suffix, int write, unsigned int *typeno);

/* set the name of a specific file in the file set, which will be used
 * to open the fd if required.  Write indicates whether the file will
 * be opened for writing or not (they're always opened for reading).  Returns
 * FDSET_OK on success and -errno on failure. */
int fdset_set_fd_name(struct fdset *set, unsigned int type, unsigned int fileno,
  const char *name, unsigned int namelen, int write);

/* create a new file.  The file must not currently exist.  Returns the
 * file descriptor on success, or -errno on failure.  */
int fdset_create(struct fdset *set, unsigned int type, unsigned int fileno);

/* as above, but also seek to an offset.  */
int fdset_create_seek(struct fdset *set, unsigned int type, unsigned int fileno,
  off_t offset);

/* get an fd from the fdset specified by type and fileno.  Returns an open fd
 * or -errno on failure.  offset is where the fd should be seeked to upon
 * opening (has to be done, otherwise fd could be at a random location) and
 * whence is the usual seek flag indicating what offset is relative to.  Pass
 * offset 0 and whence SEEK_CUR to disable seeking.  The file must already
 * exist (see fdset_create above for how to make one). */
int fdset_pin(struct fdset *set, unsigned int type, unsigned int fileno, 
  off_t offset, int whence);

/* allow an fd allocated by pinfd to be reused for a different file
 * (note that its important that you use this call after calling
 * pinfd).  Returns FDSET_OK on success and -errno on failure. */
int fdset_unpin(struct fdset *set, unsigned int type, unsigned int fileno, 
  int fd);

/* close an fd (any fd) so that another module can open one.  Returns FDSET_OK
 * on success and -errno on failure. */
int fdset_close(struct fdset *set);

/* write the name of an fd into the provided buffer.  Writes as much as it can
 * into buffer (which is of length buflen) and writes how long the name should
 * be into len.  Returns FDSET_OK on success and -errno on failure. */
int fdset_name(struct fdset *set, unsigned int type, unsigned int fileno, 
  char *buffer, unsigned int buflen, unsigned int *len, int *write);

/* same as fdset_name, except that it returns the type template name */
int fdset_type_name(struct fdset *set, unsigned int type, 
  char *buffer, unsigned int buflen, unsigned int *len, int *write);

/* returns the number of open fds */
unsigned int fdset_opened(struct fdset *set);

/* returns the number of pinned fds */
unsigned int fdset_pinned(struct fdset *set);

/* returns FDSET_OK and writes whether this particular name (identified by type,
 * fileno) is specifically set (via set_fd_name) into *set on success */
int fdset_isset(struct fdset *set, unsigned int type, unsigned int fileno, 
  int *isset);

/* return number of types currently in the fdset */
unsigned int fdset_types(struct fdset *set);

/* close all fds for a specified file.  Returns FDSET_OK upon success.
 * Will fail if pinned fds exist for this file. */
int fdset_close_file(struct fdset *set, unsigned int type, unsigned int fileno);

/* unlink the specified file.  This needs to happen through the fdset so that
 * the fd cache is cleared of open files for this file.  Returns FDSET_OK upon 
 * success.  Will fail if the specified file isn't writable, or if pinned fds
 * exist for this file. */
int fdset_unlink(struct fdset *set, unsigned int type, unsigned int fileno);

/* debugging code. */

int fdset_debug_create(struct fdset *set, unsigned int typeno, 
  unsigned int fileno, const char * src_file, int src_line);

int fdset_debug_create_seek(struct fdset *set, unsigned int type, 
  unsigned int fileno, off_t offset, const char * src_file,
  int src_line);

int fdset_debug_pin(struct fdset *set, unsigned int type, unsigned int fileno, 
  off_t offset, int whence, const char * src_file, int src_line);

int fdset_debug_unpin(struct fdset *set, unsigned int type, 
  unsigned int fileno, int fd, const char * src_file, int src_line);

/* 
#define fdset_create(set, type, fileno) \
   fdset_debug_create(set, type, fileno, __FILE__, __LINE__)

#define fdset_create_seek(set, type, fileno, offset) \
   fdset_debug_create_seek(set, type, fileno, offset, __FILE__, __LINE__)

#define fdset_pin(set, type, fileno, offset, whence) \
   fdset_debug_pin(set, type, fileno, offset, whence, __FILE__, __LINE__)

#define fdset_unpin(set, type, fileno, fd) \
   fdset_debug_unpin(set, type, fileno, fd, __FILE__, __LINE__)
*/

#ifdef __cplusplus
}
#endif

#endif

