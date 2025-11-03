#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @copydoc log_debug
 */
void log_debug(
    const char* source, int line, const char* func, const char* format, ...
) {
#ifndef DEBUG
    (void)source;
    (void)line;
    (void)func;
    (void)format;
#else
    int _result = fprintf(stdout, "%s:%d '%s': ", source, line, func);
    if (_result < 0) {
        exit(_result);
    }

    va_list _args;
    va_start(_args, format);
    _result = vfprintf(stdout, format, _args);
    va_end(_args);

    if (_result < 0) {
        exit(_result);
    }
#endif
}

/**
 * @copydoc log_error
 */
void log_error(
    const char* source, int line, const char* func, const char* format, ...
) {
#ifndef DEBUG
    (void)source;
    (void)line;
    int _ret = fprintf(stderr, "%s: ", func);
    if (_ret < 0) {
        exit(_ret);
    }
#else
    int _ret = fprintf(stderr, "%s:%d '%s': ", source, line, func);
    if (_ret < 0) {
        exit(_ret);
    }
#endif

    va_list _args;
    va_start(_args, format);
    _ret = vfprintf(stderr, format, _args);
    va_end(_args);

    if (_ret < 0) {
        exit(_ret);
    }
}
