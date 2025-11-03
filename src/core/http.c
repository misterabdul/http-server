#include "http.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <misc/logger.h>
#include <misc/mime.h>

#define PATH_BUFFER_SIZE 1024
#define MIME_BUFFER_SIZE 100
#define TIME_BUFFER_SIZE 30

/**
 * @brief HTTP response status code translation. Just for helper.
 */
typedef enum status {
    STATUS_BAD_REQUEST = 400,
    STATUS_NOT_FOUND = 404,
    STATUS_NOT_ALLOWED = 405,
    STATUS_ERROR = 500,
} status_t;

/**
 * @brief The name of the server in the HTTP header.
 */
static const char* g_server_name = "misterabdul-http-server";

/**
 * @brief Simple HTML string for HTTP bad request response.
 */
static const char* g_html_400 =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "  <head><title>Bad Request</title></head>\n"
    "  <body><div><h1>Bad request.</h1></div></body>\n"
    "</html>\n";

/**
 * @brief Simple HTML string for HTTP not found response.
 */
static const char* g_html_404 =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "  <head><title>Not Found</title></head>\n"
    "  <body><div><h1>Not found.</h1></div></body>\n"
    "</html>\n";

/**
 * @brief Simple HTML string for HTTP method not allowed response.
 */
static const char* g_html_405 =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "  <head><title>Method Not Allowed</title></head>\n"
    "  <body><div><h1>Method not allowed.</h1></div></body>\n"
    "</html>\n";

/**
 * @brief Simple HTML string for HTTP internal server error response.
 */
static const char* g_html_500 =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "  <head><title>Internal Server Error</title></head>\n"
    "  <body><div><h1>Internal server error.</h1></div></body>\n"
    "</html>\n";

/**
 * @brief Get the current time string.
 *
 * @param[out] buffer The string buffer for the time.
 * @param[in]  size   The size of the buffer.
 *
 * @return The number of character written, or 0 if it would exceed buffer size.
 */
static inline size_t get_time(char* buffer, size_t size);

/**
 * @brief Build the HTTP head for file response.
 *
 * @param[out] response The HTTP response instance.
 * @param[in]  path     The full path of the file.
 */
static inline void build_head_file(response_t* response, char* path);

/**
 * @brief Build the HTTP head for error response.
 *
 * @param[out] response The HTTP response instance.
 * @param[in]  status   The HTTP status code.
 */
static inline void build_head_error(response_t* response, status_t status);

/**
 * @brief Build the HTTP head for options response.
 *
 * @param[out] response The HTTP response instance.
 */
static inline void build_head_options(response_t* response);

/**
 * @brief Build the HTTP body for error response.
 *
 * @param[out] response The HTTP response instance.
 * @param[in]  status   The HTTP status code.
 */
static inline void build_body_error(response_t* response, status_t status);

/**
 * @brief Process the HTTP request.
 *
 * @param[out] http The HTTP instance.
 */
static inline void process_request(http_t* http);

/**
 * @copydoc http_init
 */
void http_init(http_t* http, const char* root, size_t length) {
    *http = (http_t){
        .root_dir_length = length,
        .should_close = 0,
        .root_dir = root,
    };

    http->request = (message_t){0};
    http->response = (response_t){
        .type = RESPONSE_TYPE_STRING,
        .head_length = 0,
        .body_length = 0,
        .file_fd = -1,
        .file_stat = (struct stat){0},
    };
}

/**
 * @copydoc http_process
 */
int http_process(http_t* http, char* buffer, size_t length) {
    response_t* _res = &http->response;
    message_t* _req = &http->request;

    /* parse the raw request */
    if (message_parse(_req, buffer, length) == -1) {
        build_head_error(_res, STATUS_BAD_REQUEST);
        build_body_error(_res, STATUS_BAD_REQUEST);
        http->should_close = 1;
        return -1;
    }

    /* process get method */
    if (strncmp(_req->method, "GET", 3) == 0) {
        process_request(http);
        return 0;
    }

    /* process head method */
    if (strncmp(_req->method, "HEAD", 4) == 0) {
        process_request(http);
        _res->type = RESPONSE_TYPE_HEAD_ONLY;
        return 0;
    }

    /* process options method */
    if (strncmp(_req->method, "OPTIONS", 7) == 0) {
        build_head_options(_res);
        _res->type = RESPONSE_TYPE_HEAD_ONLY;
        return 0;
    }

    /* ignore the rest of the methods */
    build_head_error(_res, STATUS_NOT_ALLOWED);
    build_body_error(_res, STATUS_NOT_ALLOWED);
    http->should_close = 1;
    return -1;
}

/**
 * @copydoc http_set_error
 */
void http_set_error(http_t* http) {
    response_t* _res = &http->response;

    build_head_error(_res, STATUS_ERROR);
    build_body_error(_res, STATUS_ERROR);
    http->should_close = 1;
}

/**
 * @copydoc http_cleanup
 */
void http_cleanup(http_t* http) {
    response_t* _res = &http->response;

    if (_res->file_fd > 0 && close(_res->file_fd) == -1) {
        LOG_ERROR("close: %s (%d)\n", strerror(errno), errno);
    }
}

/**
 * @copydoc process_request
 */
static inline void process_request(http_t* http) {
    response_t* _res = &http->response;
    message_t* _req = &http->request;
    char _path[PATH_BUFFER_SIZE];

    /* Get valid request's file path. */
    int _ret = message_resolve_path(
        _req, http->root_dir, http->root_dir_length, _path, PATH_BUFFER_SIZE
    );
    if (_ret == -1) {
        build_head_error(_res, STATUS_NOT_FOUND);
        build_body_error(_res, STATUS_NOT_FOUND);
        return;
    }

    /* Try to open the file. */
    _res->file_fd = open(_path, O_RDONLY);
    if (_res->file_fd == -1) {
        LOG_ERROR("open: %s (%d)\n", strerror(errno), errno);

        build_head_error(_res, STATUS_NOT_FOUND);
        build_body_error(_res, STATUS_NOT_FOUND);
        return;
    }

    /* Get the file's metadata. */
    if (fstat(_res->file_fd, &_res->file_stat) == -1) {
        LOG_ERROR("fstat: %s (%d)\n", strerror(errno), errno);

        build_head_error(_res, STATUS_NOT_FOUND);
        build_body_error(_res, STATUS_NOT_FOUND);
        return;
    }

    /* Build the response. */
    _res->type = RESPONSE_TYPE_FILE;
    build_head_file(_res, _path);
}

/**
 * @copydoc build_head_file
 */
static inline void build_head_file(response_t* response, char* path) {
    char _mime[MIME_BUFFER_SIZE];
    mime_get(_mime, MIME_BUFFER_SIZE, path);

    char _date[TIME_BUFFER_SIZE];
    get_time(_date, TIME_BUFFER_SIZE);

    char _last_modified[TIME_BUFFER_SIZE];
    get_time(_last_modified, response->file_stat.st_mtime);

    int _ret = snprintf(
        response->head_buffer,
        HEAD_BUFFER_SIZE,
        "HTTP/1.1 200 OK\r\n"
        "Accept-Ranges: none\r\n"
        "Cache-Control: public, max-age=86400\r\n"
        "Connection: keep-alive\r\n"
        "Content-Length: %lld\r\n"
        "Content-Type: %s\r\n"
        "Date: %s\r\n"
        "Last-Modified: %s\r\n"
        "Server: %s\r\n\r\n",
        (long long)response->file_stat.st_size,
        _mime,
        _date,
        _last_modified,
        g_server_name
    );
    if (_ret < 0) {
        LOG_ERROR("snprintf: %s (%d)\n", strerror(errno), errno);
        response->head_length = 0;
    } else {
        response->head_length = (size_t)_ret;
    }
}

/**
 * @copydoc build_head_options
 */
static inline void build_head_options(response_t* response) {
    char _date[TIME_BUFFER_SIZE];
    get_time(_date, TIME_BUFFER_SIZE);

    int _ret = snprintf(
        response->head_buffer,
        HEAD_BUFFER_SIZE,
        "HTTP/1.1 204 NO CONTENT\r\n"
        "Access-Control-Allow-Methods: GET, HEAD, OPTIONS\r\n"
        "Allow: GET, HEAD, OPTIONS\r\n"
        "Connection: keep-alive\r\n"
        "Content-Length: 0\r\n"
        "Date: %s\r\n"
        "Server: %s\r\n\r\n",
        _date,
        g_server_name
    );
    if (_ret < 0) {
        LOG_ERROR("snprintf: %s (%d)\n", strerror(errno), errno);
    }
}

/**
 * @copydoc build_head_error
 */
static inline void build_head_error(response_t* response, status_t status) {
    char _date[TIME_BUFFER_SIZE];
    get_time(_date, TIME_BUFFER_SIZE);

    int _ret;
    switch (status) {
        default:
            _ret = snprintf(
                response->head_buffer,
                HEAD_BUFFER_SIZE,
                "HTTP/1.1 500 INTERNAL SERVER ERROR\r\n"
                "Cache-Control: no-store, private\r\n"
                "Connection: close\r\n"
                "Content-Length: %lu\r\n"
                "Content-Type: text/html; charset=UTF-8\r\n"
                "Date: %s\r\n"
                "Server: %s\r\n\r\n",
                strlen(g_html_500),
                _date,
                g_server_name
            );
            break;
        case STATUS_BAD_REQUEST:
            _ret = snprintf(
                response->head_buffer,
                HEAD_BUFFER_SIZE,
                "HTTP/1.1 400 BAD REQUEST\r\n"
                "Cache-Control: no-store, private\r\n"
                "Connection: close\r\n"
                "Content-Length: %lu\r\n"
                "Content-Type: text/html; charset=UTF-8\r\n"
                "Date: %s\r\n"
                "Server: %s\r\n\r\n",
                strlen(g_html_400),
                _date,
                g_server_name
            );
            break;
        case STATUS_NOT_FOUND:
            _ret = snprintf(
                response->head_buffer,
                HEAD_BUFFER_SIZE,
                "HTTP/1.1 404 NOT FOUND\r\n"
                "Cache-Control: no-store, private\r\n"
                "Connection: keep-alive\r\n"
                "Content-Length: %lu\r\n"
                "Content-Type: text/html; charset=UTF-8\r\n"
                "Date: %s\r\n"
                "Server: %s\r\n\r\n",
                strlen(g_html_404),
                _date,
                g_server_name
            );
            break;
        case STATUS_NOT_ALLOWED:
            _ret = snprintf(
                response->head_buffer,
                HEAD_BUFFER_SIZE,
                "HTTP/1.1 405 METHOD NOT ALLOWED\r\n"
                "Cache-Control: no-store, private\r\n"
                "Connection: keep-alive\r\n"
                "Content-Length: %lu\r\n"
                "Content-Type: text/html; charset=UTF-8\r\n"
                "Date: %s\r\n"
                "Server: %s\r\n\r\n",
                strlen(g_html_405),
                _date,
                g_server_name
            );
            break;
    }
    if (_ret < 0) {
        LOG_ERROR("snprintf: %s (%d)\n", strerror(errno), errno);
        response->head_length = 0;
    } else {
        response->head_length = (size_t)_ret;
    }
}

/**
 * @copydoc build_body_error
 */
static inline void build_body_error(response_t* response, status_t status) {
    char* _html_string;
    switch (status) {
        default:
            _html_string = (char*)g_html_500;
            break;
        case STATUS_BAD_REQUEST:
            _html_string = (char*)g_html_400;
            break;
        case STATUS_NOT_FOUND:
            _html_string = (char*)g_html_404;
            break;
        case STATUS_NOT_ALLOWED:
            _html_string = (char*)g_html_405;
            break;
    }

    char* _buffer = response->body_buffer;
    int _ret = snprintf(_buffer, BODY_BUFFER_SIZE, "%s", _html_string);
    if (_ret < 0) {
        LOG_ERROR("snprintf: %s (%d)\n", strerror(errno), errno);
        response->body_length = 0;
    } else {
        response->body_length = (size_t)_ret;
    }
}

/**
 * @copydoc get_time
 */
static inline size_t get_time(char* buffer, size_t size) {
    struct tm _tm;
    time_t _now = time(NULL);
    gmtime_r(&_now, &_tm);
    return strftime(buffer, size, "%a, %d %b %Y %H:%M:%S %Z", &_tm);
}
