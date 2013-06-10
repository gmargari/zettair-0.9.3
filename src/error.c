#include "firstinclude.h"

#include "error.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define ERROR_BUF_SIZE 2048

static FILE * error_stream = NULL;
static int error_stream_inited = 0;
static char error_buf[ERROR_BUF_SIZE] = "";
static int error_code = 0;

static void ensure_error_stream_inited() {
    if (error_stream_inited == 0) {
#ifdef LOGERRORS
        error_stream = stderr;
#else /* LOGERRORS */
        error_stream = NULL;
#endif /* LOGERRORS */
        error_stream_inited = 1;
    }
}

int error_loc(int code, const char * func, const char * file, int line,
  const char * fmt, ...) {
    va_list ap;
    char msg_buf[ERROR_BUF_SIZE];

    ensure_error_stream_inited();

    error_code = code;

    va_start(ap, fmt);
    vsnprintf(msg_buf, ERROR_BUF_SIZE, fmt, ap);

    if (errno) {
        snprintf(error_buf, ERROR_BUF_SIZE, "ERROR: %s() (%s::%d): %s "
          "(system error is '%s')", func, file, line, msg_buf, 
          strerror(errno));
        errno = 0;
    } else {
        snprintf(error_buf, ERROR_BUF_SIZE, "ERROR: %s (%s::%d): %s",
          func, file, line, msg_buf);
    }
    if (error_stream != NULL) {
        fputs(error_buf, error_stream);
        fputc('\n', error_stream);
    }
    va_end(ap);
    return code;
}

int error_has_msg() {
    return error_buf[0] != '\0';
}

const char * error_last_msg() {
    return error_buf;
}

int error_last_code() {
    return error_code;
}
void error_set_log_stream(FILE * stream) {
    error_stream = stream;
    error_stream_inited = 1;
}
