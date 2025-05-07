#include "http.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "log.h"

#define MIME_BUFFER_SIZE 100
#define TIME_BUFFER_SIZE 30

/**
 * @brief Simple HTML string for HTTP not found response.
 */
const char *html_not_found =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "  <head><title>Not found</title></head>\n"
    "  <body><div><h1>Not found.</h1></div></body>\n"
    "</html>\n";

/**
 * @brief Simple HTML string for HTTP method not allowed response.
 */
const char *html_method_not_allowed =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "  <head><title>Method not allowed</title></head>\n"
    "  <body><div><h1>Method not allowed.</h1></div></body>\n"
    "</html>\n";

/**
 * @brief Simple HTML string for HTTP internal server error response.
 */
const char *html_internal_server_error =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "  <head><title>Internal server error</title></head>\n"
    "  <body><div><h1>Internal server error.</h1></div></body>\n"
    "</html>\n";

/**
 * @brief The length of the mimes array.
 */
int mimes_len = 76;

/**
 * @brief The array containing basic common mimes.
 */
char *mimes[][2] = {
    {".aac", "audio/aac"},
    {".abw", "application/x-abiword"},
    {".apng", "image/apng"},
    {".arc", "application/x-freearc"},
    {".avif", "image/avif"},
    {".avi", "video/x-msvideo"},
    {".azw", "application/vnd.amazon.ebook"},
    {".bin", "application/octet-stream"},
    {".bmp", "image/bmp"},
    {".bz", "application/x-bzip"},
    {".bz2", "application/x-bzip2"},
    {".cda", "application/x-cdf"},
    {".csh", "application/x-csh"},
    {".css", "text/css"},
    {".csv", "text/csv"},
    {".doc", "application/msword"},
    {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".eot", "application/vnd.ms-fontobject"},
    {".epub", "application/epub+zip"},
    {".gz", "applicatio/gzip"},
    {".gif", "image/gif"},
    {".htm", "text/html"},
    {".html", "text/html"},
    {".ico", "image/vnd.microsoft.icon"},
    {".ics", "text/calendar"},
    {".jar", "application/java-archive"},
    {".jpeg", "image/jpeg"},
    {".jpg", "image/jpg"},
    {".js", "text/javascript"},
    {".json", "application/json"},
    {".jsonld", "application/ld+json"},
    {".mid", "audio/midi"},
    {".midi", "audio/midi"},
    {".mjs", "text/javascript"},
    {".mp3", "audio/mpeg"},
    {".mp4", "video/mp4"},
    {".mpeg", "video/mpeg"},
    {".mpkg", "application/vnd.apple.installer+xml"},
    {".odp", "application/vnd.oasis.opendocument.presentation"},
    {".ods", "application/vnd.oasis.opendocument.spreadsheet"},
    {".odt", "application/vnd.oasis.opendocument.text"},
    {".oga", "audio/ogg"},
    {".ogv", "video/ogg"},
    {".ogx", "application/ogg"},
    {".opus", "audio/ogg"},
    {".otf", "font/otf"},
    {".png", "image/png"},
    {".pdf", "application/pdf"},
    {".php", "application/x-httpd-php"},
    {".ppt", "application/vnd.ms-powerpoint"},
    {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {".rar", "application/vnd.rar"},
    {".rtf", "application/rtf"},
    {".sh", "application/x-sh"},
    {".svg", "image/svg+xml"},
    {".tar", "application/x-tar"},
    {".tif", "application/tiff"},
    {".tiff", "application/tiff"},
    {".ts", "video/mp2t"},
    {".ttf", "font/ttf"},
    {".txt", "text/plain"},
    {".vsd", "application/vnd.visio"},
    {".wav", "audio/wav"},
    {".weba", "audio/webm"},
    {".webm", "video/webm"},
    {".webp", "image/webp"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".xhtml", "application/xhtml+xml"},
    {".xls", "application/vnd.ms-excel"},
    {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {".xml", "application/xml"},
    {".xul", "application/vnd.mozilla.xul+xml"},
    {".zip", "application/zip"},
    {".3gp", "video/3gpp"},
    {".3g2", "video/3gpp2"},
    {".7z", "application/x-7z-compressed"},
};

/**
 * @brief Get the mime for the given file path string.
 *
 * @param[out] buffer      The string buffer.
 * @param[in]  buffer_size The string buffer size.
 * @param[in]  file_path   The file path.
 */
void http_get_mime(char *buffer, size_t buffer_size, const char *file_path) {
    char *_extension = NULL;
    for (char *_c = (char *)file_path; *_c != '\0'; _c++) {
        if (*_c == '.') {
            _extension = _c;
        }
    }

    if (_extension) {
        for (int _i = 0; _i < mimes_len; _i++) {
            if (strcmp(_extension, mimes[_i][0]) != 0) {
                continue;
            }
            strncpy(buffer, mimes[_i][1], buffer_size);
            return;
        }
    }

    strncpy(buffer, "application/octet-stream", buffer_size);
}

/**
 * @brief Get the current time string.
 *
 * @param[out] buffer      The string buffer for the time.
 * @param[in]  buffer_size The string buffer size.
 *
 * @return The number of character written, or 0 if it would exceed buffer size.
 */
size_t http_get_time(char *buffer, size_t buffer_size) {
    time_t _now = time(0);
    struct tm _tm = *gmtime(&_now);

    return strftime(buffer, buffer_size, "%a, %d %b %Y %H:%M:%S %Z", &_tm);
}

/**
 * Parse the raw request string manually character by character.
 */
int http_parse_request(http_message_t *message, const char *request, size_t request_len) {
    size_t _i = 0, _j;

    /* parse method */
    for (_j = _i; _i < request_len; _i++) {
        if (request[_i] == ' ' || request[_i] == '\r' || request[_i] == '\n' || request[_i] == '\0') {
            break;
        }
    }
    if (request[_i] != ' ') {
        return 1;
    }
    message->method = (char *)&request[_j];
    message->method_len = _i - _j;

    /* parse target */
    for (_j = ++_i; _i < request_len; _i++) {
        if (request[_i] == ' ' || request[_i] == '\r' || request[_i] == '\n' || request[_i] == '\0') {
            break;
        }
    }
    if (request[_i] != ' ') {
        return 1;
    }
    message->target = (char *)&request[_j];
    message->target_len = _i - _j;

    /* parse version */
    for (_j = ++_i; _i < request_len; _i++) {
        if (request[_i] == ' ' || request[_i] == '\r' || request[_i] == '\n' || request[_i] == '\0') {
            break;
        }
    }
    message->version = (char *)&request[_j];
    message->version_len = _i - _j;

    /* parse headers */
    size_t _headers_size = message->headers_len;
    message->headers_len = 0;
    for (; message->headers_len < _headers_size && _i < request_len;) {
        if (request[_i] == '\0') {
            break;
        }

        for (; _i < request_len; _i++) {
            if (request[_i] == '\n' || request[_i] == '\0') {
                break;
            }
        }
        if (request[_i] == '\0') {
            break;
        }

        for (_j = ++_i; _i < request_len; _i++) {
            if (request[_i] == ':' || request[_i] == '\n' || request[_i] == '\0') {
                break;
            }
        }
        if (request[_i] != ':') {
            break;
        }

        message->headers[message->headers_len].name = (char *)&request[_j];
        message->headers[message->headers_len].name_len = _i - _j;

        for (++_i; _i < request_len; _i++) {
            if (request[_i] != ' ' || request[_i] == '\0') {
                break;
            }
        }
        if (request[_i] == '\0') {
            break;
        }

        for (_j = _i; _i < request_len; _i++) {
            if (request[_i] == '\r' || request[_i] == '\n' || request[_i] == '\0') {
                break;
            }
        }
        message->headers[message->headers_len].value = (char *)&request[_j];
        message->headers[message->headers_len].value_len = _i - _j;

        message->headers_len++;
    }

    /* parse body */
    if (++_i < request_len) {
        message->body = (char *)&request[_i];
        message->body_len = request_len - _i;
    } else {
        message->body = NULL;
        message->body_len = 0;
    }

    return 0;
}

/**
 * Try to resolve the actual file path from the HTTP message's target.
 */
int http_resolve_file_path(http_message_t *message, char *root_path, char *file_path, size_t file_path_size) {
    if (message->target_len > 1) {
        for (size_t _i = 1; _i < message->target_len; _i++) {
            if (message->target[_i] == '?') {
                message->target_len = _i;
                break;
            }
            if (message->target[_i - 1] == '.' && message->target[_i] == '.') {
                return -1;
            }
        }
    }

    if (message->target_len <= 0 || message->target[message->target_len - 1] == '/') {
        if (snprintf(
                file_path, file_path_size, "./%s%.*sindex.html", root_path, (int)message->target_len, message->target
            ) < 0) {
            return -1;
        }
    } else {
        if (snprintf(file_path, file_path_size, "./%s%.*s", root_path, (int)message->target_len, message->target) < 0) {
            return -1;
        }
    }

    if (access(file_path, F_OK) != 0) {
        return -1;
    }

    struct stat _path_stat;
    if (stat(file_path, &_path_stat) != 0) {
        return -1;
    };
    if (S_ISREG(_path_stat.st_mode) == 0 &&
        snprintf(
            file_path, file_path_size, "./%s%.*s/index.html", root_path, (int)message->target_len, message->target
        ) < 0) {
        return -1;
    }

    return 0;
}

/**
 * Build HTTP header string into the buffer for the given file path and file size.
 */
int http_header_file(char *buffer, size_t buffer_size, char *file_path, off_t file_size) {
    char _mime[MIME_BUFFER_SIZE], _time_string[TIME_BUFFER_SIZE];
    http_get_mime(_mime, MIME_BUFFER_SIZE, file_path);
    http_get_time(_time_string, TIME_BUFFER_SIZE);

    return snprintf(
        buffer, buffer_size,
        "HTTP/1.1 200 OK\r\n"
        "Connection: keep-alive\r\n"
        "Content-Length: %lu\r\n"
        "Content-Type: %s\r\n"
        "Date: %s\r\n"
        "Server: misterabdul-http-server\r\n\r\n",
        file_size, _mime, _time_string
    );
}

/**
 * Build entire HTTP message string into the buffer for the HTTP not found response.
 */
int http_message_not_found(char *buffer, size_t buffer_size) {
    char _time_string[TIME_BUFFER_SIZE];
    http_get_time(_time_string, TIME_BUFFER_SIZE);

    return snprintf(
        buffer, buffer_size,
        "HTTP/1.1 404 NOT FOUND\r\n"
        "Connection: keep-alive\r\n"
        "Content-Length: %lu\r\n"
        "Content-Type: text/html\r\n"
        "Date: %s\r\n"
        "Server: misterabdul-http-server\r\n\r\n"
        "%s",
        strlen(html_not_found), _time_string, html_not_found
    );
}

/**
 * Build entire HTTP message string into the buffer for the HTTP method not allowed response.
 */
int http_message_not_allowed(char *buffer, size_t buffer_size) {
    char _time_string[TIME_BUFFER_SIZE];
    http_get_time(_time_string, TIME_BUFFER_SIZE);

    return snprintf(
        buffer, buffer_size,
        "HTTP/1.1 405 METHOD NOT ALLOWED\r\n"
        "Connection: keep-alive\r\n"
        "Content-Length: %lu\r\n"
        "Content-Type: text/html\r\n"
        "Date: %s\r\n"
        "Server: misterabdul-http-server\r\n\r\n"
        "%s",
        strlen(html_method_not_allowed), _time_string, html_method_not_allowed
    );
}

/**
 * Build entire HTTP message string into the buffer for the HTTP internal server error response.
 */
int http_message_error(char *buffer, size_t buffer_size) {
    char _time_string[TIME_BUFFER_SIZE];
    http_get_time(_time_string, TIME_BUFFER_SIZE);

    return snprintf(
        buffer, buffer_size,
        "HTTP/1.1 500 INTERNAL SERVER ERROR\r\n"
        "Connection: keep-alive\r\n"
        "Content-Length: %lu\r\n"
        "Content-Type: text/html\r\n"
        "Date: %s\r\n"
        "Server: misterabdul-http-server\r\n\r\n"
        "%s",
        strlen(html_internal_server_error), _time_string, html_internal_server_error
    );
}
