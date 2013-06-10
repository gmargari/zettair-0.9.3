/* getmaxfsize.h is a work-around for the problem that we can't determine
 * how large files can be under unix.  They seem to have this notion
 * that filesystems will provide large enough files that no-one will
 * care, even though history and the existance of this file seem to
 * prove them wrong
 *
 * written nml 2003-02-23
 *
 */

#ifndef GETMAXFS_H
#define GETMAXFS_H

#ifdef __cplusplus
extern "C" {
#endif

/* accepts an open file descriptor fd and an upper limit knownlimit
 * (which could be acquired through getrlimit(2)).  On success it
 * returns true and writes the upper limit to which the file descriptor can be 
 * extended, or UINT_MAX if no limit was found, into limit. (note that this
 * means read_only fds can't be extended) */
int getmaxfsize(int fd, unsigned int knownlimit, unsigned int *limit);

#ifdef __cplusplus
}
#endif

#endif

