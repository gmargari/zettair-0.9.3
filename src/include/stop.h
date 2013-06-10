/* stop.h is a simple stoplist (i.e. a list of (common) words we don't to 
 * deal with).  Stopping is an extremely common information retrieval 
 * technique, see Managing Gigabytes for more information.
 *
 * written nml 2004-11-19
 *
 */

#ifndef STOP_H
#define STOP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include <stdio.h>

/* return values from (most) stop functions */
enum stop_ret {
    STOP_OK = 0,
    STOP_STOPPED = 1,

    /* error values */
    STOP_EEXIST = -EEXIST,
    STOP_EINVAL = -EINVAL,
    STOP_ENOMEM = -ENOMEM,
    STOP_EACCESS = -EACCES 
};

struct stop;

/* create a new (empty) stop list */
struct stop *stop_new(void (*stem)(void *opaque, char *term), void *opaque);

/* create a new stop list from the contents of filename.  This is just
 * a wrapper for stop_new followed by stop_add_file.  See
 * stop_add_file for more details. */
struct stop *stop_new_file(void (*stem)(void *opaque, char *term), 
  void *opaque, const char *filename);

/* creates a stoplist populated with default data */
struct stop *stop_new_default(void (*stem)(void *opaque, char *term), 
  void *opaque);

/* delete a stop list */
void stop_delete(struct stop *list);

/* add the contents of the given file to the stoplist.  The file
 * format is one word per line, comments starting with # ignored, with
 * a line length limit of 4096.  Various things can go wrong when
 * reading the file, with error return values corresponding to those
 * returned from the system calls open() and read(), anything
 * unrecognised is returned as STOP_EINVAL.  Returns STOP_OK on
 * success.  All terms are case-folded to lower-case as they are put
 * into the table. */
enum stop_ret stop_add_file(struct stop *list, const char *filename);

/* add a term to the stoplist.  Can return STOP_ENOMEM if memory
 * cannot be allocated, otherwise returns STOP_OK.  Returns EEXIST if
 * the term already appears in the table.  term is case-folded to
 * lowercase and stemmed prior to insertion. */
enum stop_ret stop_add_term(struct stop *list, const char *term);

/* test a term to see if it is stopped by the stoplist.  Returns
 * STOP_STOPPED if the term is stopped, STOP_OK if not.  No
 * case-folding or stemming are performed, so you'd better make sure that 
 * term is in the case that you want (typically lower) and appropriately 
 * stemmed before calling this function.  */
enum stop_ret stop_stop(struct stop *list, const char *term);

/* output c code to write function stop_new_default based on current contents 
 * of the stoplist */
enum stop_ret stop_write_code(struct stop *list, FILE *output);

/* outputs the contents of the stoplist, one word per line, to the given output
 * file */
enum stop_ret stop_print(struct stop *list, FILE *output);

#ifdef __cplusplus
}
#endif

#endif

