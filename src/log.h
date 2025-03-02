#ifndef log_h
#define log_h

#include <stdio.h>

void log_debug(const char *source, int line, const char *func, const char *format, ...);

void log_error(const char *source, int line, const char *func, const char *format, ...);

#ifdef VERBOSE

#define LOG_DEBUG(...) log_debug(__FILE__, __LINE__, __func__, __VA_ARGS__)

#define LOG_ERROR(...) log_error(__FILE__, __LINE__, __func__, __VA_ARGS__)

#else

#define LOG_DEBUG(...)

#define LOG_ERROR(...)

#endif

#endif
