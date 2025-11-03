#ifndef misc_log_h
#define misc_log_h

/**
 * @brief Print a pretty log message for debugging.
 *
 * @param[in] source The source code file path.
 * @param[in] line   The line number in the source code file.
 * @param[in] func   The function that execute the code.
 * @param[in] format The format specifier as in printf.
 * @param[in] ...    Any extra data for the format as in printf.
 */
void log_debug(
    const char* source, int line, const char* func, const char* format, ...
);

/**
 * @brief Print a pretty log message for error.
 *
 * @param[in] source The source code file path.
 * @param[in] line   The line number in the source code file.
 * @param[in] func   The function that execute the code.
 * @param[in] format The format specifier as in printf.
 * @param[in] ...    Any extra data for the format as in printf.
 */
void log_error(
    const char* source, int line, const char* func, const char* format, ...
);

#define LOG_ERROR(...) log_error(__FILE__, __LINE__, __func__, __VA_ARGS__)

#ifdef DEBUG

#define LOG_DEBUG(...) log_debug(__FILE__, __LINE__, __func__, __VA_ARGS__)

#else

#define LOG_DEBUG(...)

#endif

#endif
