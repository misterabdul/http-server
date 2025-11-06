#ifndef http_h
#define http_h

#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <lib/httpmsg.h>

#define HEAD_BUFFER_SIZE 512
#define BODY_BUFFER_SIZE 2048

/**
 * @brief HTTP response type.
 */
typedef enum response_type {
    RESPONSE_TYPE_HEAD_ONLY = 0,
    RESPONSE_TYPE_STRING = 1,
    RESPONSE_TYPE_FILE = 2,
} response_type_t;

/**
 * @brief HTTP response representation.
 */
typedef struct response {
    /**
     * @brief The type of the response.
     */
    response_type_t type;

    /**
     * @brief The minor version for the HTTP version.
     */
    int minor_version;

    /**
     * @brief The string buffer for the HTTP head.
     */
    char head_buffer[HEAD_BUFFER_SIZE];

    /**
     * @brief The length of the head string buffer.
     */
    size_t head_length;

    /**
     * @brief The string buffer for the HTTP body.
     */
    char body_buffer[BODY_BUFFER_SIZE];

    /**
     * @brief The length of the body string buffer.
     */
    size_t body_length;

    /**
     * @brief The file descriptor for the response.
     */
    int file_fd;

    /**
     * @brief The metadata of the file.
     */
    struct stat file_stat;
} response_t;

/**
 * @brief The HTTP instance for HTTP related operation.
 */
typedef struct http {
    /**
     * @brief The root directory.
     */
    const char* root_dir;

    /**
     * @brief The length of the root directory.
     */
    size_t root_dir_length;

    /**
     * @brief The status to show whether to close the HTTP connection after
     * the response sent or not.
     */
    int should_close;

    /**
     * @brief The parsed HTTP request instance.
     */
    message_t request;

    /**
     * @brief The HTTP response instance.
     */
    response_t response;
} http_t;

/**
 * @brief Initialize the HTTP instance for HTTP operation.
 *
 * @param[out] http   The HTTP instance
 * @param[in]  root   The root directory.
 * @param[in]  length The length root directory.
 */
void http_init(http_t* http, const char* root, size_t length);

/**
 * @brief Process the raw HTTP request.
 *
 * @param[out] http   The HTTP instance.
 * @param[in]  buffer The raw HTTP request buffer.
 * @param[in]  length The length of the buffer.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int http_process(http_t* http, char* buffer, size_t length);

/**
 * @brief Mark the HTTP operation error, prepare the error response.
 *
 * @param[out] http The HTTP instance.
 */
void http_set_error(http_t* http);

/**
 * @brief Clean all related stuff from the HTTP instance.
 *
 * @param[out] http The HTTP instance.
 */
void http_cleanup(http_t* http);

#endif
