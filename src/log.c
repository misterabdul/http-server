#include "log.h"

#include <stdarg.h>
#include <stdio.h>

/**
 * Print a pretty debug log message to standard output.
 */
void log_debug(const char *source, int line, const char *func, const char *format, ...) {
    fprintf(stdout, "%s:%d '%s': ", source, line, func);

    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
}

/**
 * Print a pretty error log message to standard output.
 */
void log_error(const char *source, int line, const char *func, const char *format, ...) {
    fprintf(stderr, "%s:%d '%s': ", source, line, func);

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}
