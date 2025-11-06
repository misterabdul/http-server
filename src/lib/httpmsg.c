#include "httpmsg.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <misc/logger.h>

#define REAL_PATH_BUFFER_SIZE 1024

/**
 * @brief Check if the character match inside the given array of characters.
 *
 * @param[in] c     The character to be checked.
 * @param[in] arr   The array of characters.
 * @param[in] count The number of characters in the array.
 *
 * @return Match result, true for matched, false for not matched.
 */
static inline bool match(char c, char* arr, size_t count);

/**
 * @brief Decode the percent encoded string.
 *
 * @param[out] url The string to be decoded, must be null terminated string.
 *
 * @return The length of decoded string or -1 for errors.
 */
static inline int decode_url(char* url);

/**
 * @brief Check for directory traversal attempt.
 *
 * @param[in] path   The path to be checked.
 * @param[in] root   The root path.
 * @param[in] length The length of the root path.
 *
 * @return Check result, true for safe path, false for not safe.
 */
static inline bool check_path(
    const char* path, const char* root, size_t length
);

/**
 * @brief Convert hex character into its integer value.
 *
 * @param[in] c The hex character.
 *
 * @return The integer value of the hex character or -1 for errors.
 */
static inline int hex2int(char c);

/**
 * @copydoc http_message_parse
 */
int message_parse(message_t* message, char* request, size_t length) {
    size_t _cursor = 0, _prev;

    /* Parse HTTP method. */
    for (_prev = _cursor; _cursor < length; _cursor++) {
        if (match(request[_cursor], " \r\n\0", 4)) {
            break;
        }
    }
    if (request[_cursor] != ' ') {
        return -1;
    }
    message->method = (char*)&request[_prev];
    message->method_length = _cursor - _prev;

    /* Parse HTTP target. */
    for (_prev = ++_cursor; _cursor < length; _cursor++) {
        if (match(request[_cursor], " \r\n\0", 4)) {
            break;
        }
    }
    if (request[_cursor] != ' ') {
        return -1;
    }
    message->target = (char*)&request[_prev];
    message->target_length = _cursor - _prev;

    /* Parse HTTP version. */
    for (_prev = ++_cursor; _cursor < length; _cursor++) {
        if (match(request[_cursor], " \r\n\0", 4)) {
            break;
        }
    }
    message->version = (char*)&request[_prev];
    message->version_length = _cursor - _prev;

    /* Parse HTTP headers. */
    message->headers_count = 0;
    for (size_t _i = 0; _i < HEADER_BUFFER_SIZE && _cursor < length; _i++) {
        if (request[_cursor] == '\0') {
            break;
        }

        for (; _cursor < length; _cursor++) {
            if (match(request[_cursor], "\n\0", 2)) {
                break;
            }
        }
        if (request[_cursor] == '\0') {
            break;
        }

        for (_prev = ++_cursor; _cursor < length; _cursor++) {
            if (match(request[_cursor], ":\n\0", 2)) {
                break;
            }
        }
        if (request[_cursor] != ':') {
            break;
        }

        message->headers[_i].name = (char*)&request[_prev];
        message->headers[_i].name_length = _cursor - _prev;

        for (++_cursor; _cursor < length; _cursor++) {
            if (match(request[_cursor], " \0", 2)) {
                break;
            }
        }
        if (request[_cursor] == '\0') {
            break;
        }
        for (_prev = _cursor; _cursor < length; _cursor++) {
            if (match(request[_cursor], "\r\n\0", 2)) {
                break;
            }
        }

        message->headers[_i].value = (char*)&request[_prev];
        message->headers[_i].value_length = _cursor - _prev;
        message->headers_count = _i + 1;
    }

    /* Parse the body. */
    if (++_cursor < length) {
        message->body = (char*)&request[_cursor];
        message->body_length = length - _cursor;
    } else {
        message->body = NULL;
        message->body_length = 0;
    }

    return 0;
}

/**
 * @copydoc http_message_resolve_path
 */
int message_resolve_path(
    message_t* message, const char* root, size_t length, char* path, size_t size
) {
    int _length;
    size_t _i;

    /* Find the proper length for copying, stop on the start of query param. */
    for (_i = 0; _i < message->target_length; _i++) {
        if (message->target[_i] == '?') {
            break;
        }
    }

    /* Copy into proper null terminated string. */
    _length = snprintf(path, size, "%s%.*s", root, (int)_i, message->target);
    if (_length == -1) {
        LOG_ERROR("snprintf: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    /* Perform percent decoding */
    _length = decode_url(path);
    if (_length == -1) {
        return -1;
    }

    /* Append "index.html" if no file specified. */
    if (_length <= 0 || path[_length - 1] == '/') {
        strncat(path, "index.html", size - (size_t)_length);
    }

    /* prevent directory traversal, eg "/../" */
    if (!check_path(path, root, length)) {
        return -1;
    }

    /* Check path permission. */
    if (access(path, F_OK) == -1) {
        LOG_ERROR("access: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    /* Check whether the path is regular file or directory. */
    struct stat _stat;
    if (stat(path, &_stat) == -1) {
        LOG_ERROR("stat: %s (%d)\n", strerror(errno), errno);
        return -1;
    };
    if (S_ISREG(_stat.st_mode) != 0) {
        return 0;
    }

    /* If the path is directory, then append the "index.html" string. */
    strncat(path, "/index.html", size - (size_t)_length);

    return 0;
}

/**
 * @copydoc message_resolve_version_minor
 */
int message_resolve_version_minor(message_t* message) {
    const char* _prefix = "HTTP/1.";

    if (message->version_length < 8) {
        return 0;
    }
    if (strncmp(message->version, _prefix, 7) != 0) {
        return 0;
    }

    return message->version[7] == '1' ? 1 : 0;
}

/**
 * @copydoc match
 */
static inline bool match(char c, char* arr, size_t count) {
    for (size_t _i = 0; _i < count; _i++) {
        if (c == arr[_i]) {
            return true;
        }
    }

    return false;
}

/**
 * @copydoc decode_url
 */
static inline int decode_url(char* url) {
    int _hex1, _hex2;
    size_t _newlen = 0;

    for (size_t _i = 0; url[_i] != '\0'; _i++) {
        /* Replace plus character with space. */
        if (url[_i] == '+') {
            url[_newlen++] = ' ';
            continue;
        }

        /* Anything other than percent character, copy as is. */
        if (url[_i] != '%') {
            url[_newlen++] = url[_i];
            continue;
        }

        /* Malformed percent encoding. */
        if (url[_i + 1] == '\0' || url[_i + 2] == '\0') {
            return -1;
        }

        /* Convert the hex value of the next 2 characters after the percent. */
        _hex1 = hex2int(url[_i + 1]);
        if (_hex1 == -1) {
            return -1;
        }
        _hex2 = hex2int(url[_i + 2]);
        if (_hex2 == -1) {
            return -1;
        }

        /* Convert back the hex value into byte for the UTF-8 string. */
        url[_newlen++] = (char)((_hex1 << 4) | _hex2);
        _i += 2;
    }
    url[_newlen] = '\0';

    return _newlen;
}

/**
 * @copydoc check_path
 */
static inline bool check_path(
    const char* path, const char* root, size_t root_length
) {
    /* Resolve the absolute path. */
    char _resolved[REAL_PATH_BUFFER_SIZE];
    if (realpath(path, _resolved) == NULL) {
        if (errno != ENOENT) {
            LOG_ERROR("realpath: %s (%d)\n", strerror(errno), errno);
        }
        return false;
    }

    /* Check whether the path is inside the absolute root path. */
    if (strncmp(_resolved, root, root_length) != 0) {
        return false;
    }

    /* Edge case. */
    if (_resolved[root_length] != '/' && _resolved[root_length] != '\0') {
        return false;
    }

    return true;
}

/**
 * @copydoc hex2int
 */
static inline int hex2int(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    /* Not a hex value, return error. */
    return -1;
}
