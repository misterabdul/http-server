#ifndef lib_httpmsg_h
#define lib_httpmsg_h

#include <stddef.h>
#include <sys/stat.h>

#define HEADER_BUFFER_SIZE 128

/**
 * @brief HTTP header representation.
 */
typedef struct header {
    /**
     * @brief The name of the header.
     */
    char* name;

    /**
     * @brief The string length of the name.
     */
    size_t name_length;

    /**
     * @brief The value of the header.
     */
    char* value;

    /**
     * @brief The string length of the value.
     */
    size_t value_length;
} header_t;

/**
 * @brief HTTP message representation.
 */
typedef struct message {
    /**
     * @brief The HTTP method, e.g.: GET, POST, HEAD, OPTIONS, etc.
     */
    char* method;

    /**
     * @brief The string length of the method.
     */
    size_t method_length;

    /**
     * @brief The request target.
     */
    char* target;

    /**
     * @brief The string length of the target.
     */
    size_t target_length;

    /**
     * @brief The HTTP protocol version.
     */
    char* version;

    /**
     * @brief The string length of the version.
     */
    size_t version_length;

    /**
     * @brief The HTTP headers array.
     */
    header_t headers[HEADER_BUFFER_SIZE];

    /**
     * @brief The number of parsed headers.
     */
    size_t headers_count;

    /**
     * @brief The HTTP body.
     */
    char* body;

    /**
     * @brief The string length of the body.
     */
    size_t body_length;
} message_t;

/**
 * @brief Parse raw request into HTTP message.
 *
 * @param[out] message The HTTP message instance.
 * @param[in]  buffer  The raw request buffer.
 * @param[in]  length  The length of the buffer.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int message_parse(message_t* message, char* buffer, size_t length);

/**
 * @brief Resolve file path from the HTTP message.
 *
 * @param[in]  message The HTTP message instance.
 * @param[in]  root    The root directory.
 * @param[in]  length  The length of the root directory.
 * @param[out] path    The file path string buffer.
 * @param[in]  size    The file path string buffer size.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int message_resolve_path(
    message_t* message, const char* root, size_t length, char* path, size_t size
);

/**
 * @brief Resolve the minor version of valid HTTP/1.X version from the message.
 *
 * Assume the minor version 0 on invalid version or any error while parsing.
 *
 * @param[in] message The HTTP message instance.
 *
 * @return The minor version integer, any error will return 0.
 */
int message_resolve_version_minor(message_t* message);

#endif
