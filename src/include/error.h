/* error.h declares macros and other facilities for error logging
 *
 * written wew 2004-06-07
 *
 */

#ifndef ERROR_H
#define ERROR_H

#include <stdio.h>

/* Variadic macros from C99 */
/*
#define ERROR(format, ...) \
  error_loc(-1, __func__, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define ERROR_CODE(code, format, ...) \
  error_loc(code, __func__, __FILE__, __LINE__, format, ##__VA_ARGS__)
  */

/* C99 provides __func__ as the name of the current function.
   Before this, GCC had the extension __FUNCTION__.   Otherwise,
   we don't attempt to determine it. */
#if __STDC_VERSION__ >= 199901L
   #define FUNC_NAME __func__
#elif defined(__GNUC__) && !defined(__STRICT_ANSI__)
   #define FUNC_NAME __FUNCTION__
#else
   #define FUNC_NAME "(unknown function)"
#endif

/* What C89 provides */
#define ERROR(format) \
  error_loc(-1, FUNC_NAME, __FILE__, __LINE__, format)

#define ERROR_CODE(code, format) \
  error_loc(code, FUNC_NAME, __FILE__, __LINE__, format)

#define ERROR1(format, arg1) \
  error_loc(-1, FUNC_NAME, __FILE__, __LINE__, format, arg1)

#define ERROR_CODE1(code, format, arg1) \
  error_loc(code, FUNC_NAME, __FILE__, __LINE__, format, arg1)

#define ERROR2(format, arg1, arg2) \
  error_loc(-1, FUNC_NAME, __FILE__, __LINE__, format, arg1, arg2)

#define ERROR_CODE2(code, format, arg1, arg2) \
  error_loc(code, FUNC_NAME, __FILE__, __LINE__, format, arg1, arg2)

#define ERROR3(format, arg1, arg2, arg3) \
  error_loc(-1, FUNC_NAME, __FILE__, __LINE__, format, arg1, arg2, arg3)

#define ERROR_CODE3(code, format, arg1, arg2, arg3) \
  error_loc(code, FUNC_NAME, __FILE__, __LINE__, format, arg1, arg2, arg3)

#define ERROR4(format, arg1, arg2, arg3, arg4) \
  error_loc(-1, FUNC_NAME, __FILE__, __LINE__, format, arg1, arg2, arg3, arg4)

#define ERROR_CODE4(code, format, arg1, arg2, arg3) \
  error_loc(code, FUNC_NAME, __FILE__, __LINE__, format, arg1, arg2, arg3, arg4)

/*
 *  Is there an error to report?.
 */
int error_has_msg();

/*
 *  Get the last error message as set by error_loc()/ERROR().
 */
const char * error_last_msg();

/*
 *  Get the last error code as set by error_loc()/ERROR().
 */
int error_last_code();

/*
 *  Set the stream that errors are logged to.
 *
 *  Setting to NULL stops errors being logged.
 *
 *  By default, errors are not logged, unless the LOGERRORS 
 *  macros is defined at compile time, in which case they go
 *  to stderr.
 */
void error_set_log_stream(FILE * stream);

/*
 *  Record an error message.
 *
 *  The error message will have the form 
 *  "ERROR: <func> (<file>::<line>): <msg> [(system error is "<syserr>")].
 *  This message is retrievable by error_last_msg().  It will also be
 *  logged to the stream set by error_set_stream() (unless this is
 *  NULL).
 *
 *  This function is most conveniently called via the ERROR macro.
 *
 *  @param code the code for this error.
 *  @param func the name of the function this error occurs in.
 *  @param file the name of the file this error occurs in.
 *  @param line the line of this file for this error message.
 *  @param fmt user-defined format.  It is not necessary to precede
 *    it with "ERROR", or put a trailing newline.
 *
 *  @return the error code, for convenience.
 */
int error_loc(int code, const char * func, const char * file, int line,
  const char * fmt, ...);

#endif

