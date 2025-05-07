#include "log.h"

#include <stdarg.h>
#include <stdio.h>

/**
 * Print a pretty debug log message to standard output.
 */
void log_debug(const char *source, int line, const char *func, const char *format, ...) {
    fprintf(stdout, "%s:%d '%s': ", source, line, func);

    va_list _args;
    va_start(_args, format);
    vfprintf(stdout, format, _args);
    va_end(_args);
}

/**
 * Print a pretty error log message to standard output.
 */
void log_error(const char *source, int line, const char *func, const char *format, ...) {
    fprintf(stderr, "%s:%d '%s': ", source, line, func);

    va_list _args;
    va_start(_args, format);
    vfprintf(stderr, format, _args);
    va_end(_args);
}
