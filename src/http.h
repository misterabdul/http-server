#ifndef http_h
#define http_h

#include <sys/types.h>

#include "config.h"

/**
 * @brief HTTP header representation.
 */
typedef struct http_header {
    char *name, *value;
    size_t name_len, value_len;
} http_header_t;

/**
 * @brief HTTP message representation.
 */
typedef struct http_message {
    char *method, *target, *version, *body;
    http_header_t headers[MAX_HEADERS];
    size_t method_len, target_len, version_len, body_len, headers_len;
} http_message_t;

/**
 * @brief Parse raw request into HTTP message.
 *
 * @param[out] message     The message instance.
 * @param[in]  request     The raw request string.
 * @param[in]  request_len The raw request string length.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int http_parse_request(http_message_t *message, const char *request, size_t request_len);

/**
 * @brief Resolve file path from HTTP message.
 *
 * @param[in]  message        The message instance.
 * @param[out] file_path      The file path string buffer.
 * @param[in]  file_path_size The file path string buffer size.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int http_resolve_file_path(http_message_t *message, char *file_path, size_t file_path_size);

/**
 * @brief Build HTTP message header string for file response.
 *
 * @param[out] buffer      The string buffer.
 * @param[in]  buffer_size The string buffer size.
 * @param[in]  file_path   The file path.
 * @param[in]  file_size   The file size.
 *
 * @return Length of the header string or -1 for errors.
 */
int http_header_file(char *buffer, size_t buffer_size, char *file_path, off_t file_size);

/**
 * @brief Build the entire HTTP message string for not found response.
 *
 * @param[out] buffer      The string buffer.
 * @param[in]  buffer_size The string buffer size.
 *
 * @return Length of the entire message string or -1 for errors.
 */
int http_message_not_found(char *buffer, size_t buffer_size);

/**
 * @brief Build the entire HTTP message string for method not allowed response.
 *
 * @param[out] buffer      The string buffer.
 * @param[in]  buffer_size The string buffer size.
 *
 * @return Length of the entire message string or -1 for errors.
 */
int http_message_not_allowed(char *buffer, size_t buffer_size);

/**
 * @brief Build the entire HTTP message string for error esponse.
 *
 * @param[out] buffer      The string buffer.
 * @param[in]  buffer_size The string buffer size.
 *
 * @return Length of the entire message string or -1 for errors.
 */
int http_message_error(char *buffer, size_t buffer_size);

#endif
