#include "http.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "log.h"

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
    char *extension = NULL;
    for (char *c = (char *)file_path; *c != '\0'; c++) {
        if (*c == '.') {
            extension = c;
        }
    }

    if (extension) {
        for (int i = 0; i < mimes_len; i++) {
            if (strcmp(extension, mimes[i][0]) != 0) {
                continue;
            }
            strncpy(buffer, mimes[i][1], buffer_size);
            return;
        }
    }

    strncpy(buffer, "application/octet-stream", buffer_size);
}

/**
 * Parse the raw request string manually character by character.
 */
int http_parse_request(http_message_t *message, const char *request, size_t request_len) {
    size_t i = 0, j, headers_size = message->headers_len;

    /* parse method */
    for (j = i; i < request_len; i++) {
        if (request[i] == ' ' || request[i] == '\r' || request[i] == '\n' || request[i] == '\0') {
            break;
        }
    }
    if (request[i] != ' ') {
        return 1;
    }
    message->method = (char *)&request[j];
    message->method_len = i - j;

    /* parse target */
    for (j = ++i; i < request_len; i++) {
        if (request[i] == ' ' || request[i] == '\r' || request[i] == '\n' || request[i] == '\0') {
            break;
        }
    }
    if (request[i] != ' ') {
        return 1;
    }
    message->target = (char *)&request[j];
    message->target_len = i - j;

    /* parse version */
    for (j = ++i; i < request_len; i++) {
        if (request[i] == ' ' || request[i] == '\r' || request[i] == '\n' || request[i] == '\0') {
            break;
        }
    }
    message->version = (char *)&request[j];
    message->version_len = i - j;

    /* parse headers */
    message->headers_len = 0;
    for (; message->headers_len < headers_size && i < request_len;) {
        if (request[i] == '\0') {
            break;
        }

        for (; i < request_len; i++) {
            if (request[i] == '\n' || request[i] == '\0') {
                break;
            }
        }
        if (request[i] == '\0') {
            break;
        }

        for (j = ++i; i < request_len; i++) {
            if (request[i] == ':' || request[i] == '\n' || request[i] == '\0') {
                break;
            }
        }
        if (request[i] != ':') {
            break;
        }

        message->headers[message->headers_len].name = (char *)&request[j];
        message->headers[message->headers_len].name_len = i - j;

        for (++i; i < request_len; i++) {
            if (request[i] != ' ' || request[i] == '\0') {
                break;
            }
        }
        if (request[i] == '\0') {
            break;
        }

        for (j = i; i < request_len; i++) {
            if (request[i] == '\r' || request[i] == '\n' || request[i] == '\0') {
                break;
            }
        }
        message->headers[message->headers_len].value = (char *)&request[j];
        message->headers[message->headers_len].value_len = i - j;

        message->headers_len++;
    }

    /* parse body */
    if (++i < request_len) {
        message->body = (char *)&request[i];
        message->body_len = request_len - i;
    } else {
        message->body = NULL;
        message->body_len = 0;
    }

    return 0;
}

/**
 * Try to resolve the actual file path from the HTTP message's target.
 */
int http_resolve_file_path(http_message_t *message, char *file_path, size_t file_path_size) {
    if (message->target_len > 1) {
        for (size_t i = 1; i < message->target_len; i++) {
            if (message->target[i] == '?') {
                message->target_len = i;
                break;
            }
            if (message->target[i - 1] == '.' && message->target[i] == '.') {
                return -1;
            }
        }
    }

    if (message->target_len <= 0 || message->target[message->target_len - 1] == '/') {
        if (snprintf(
                file_path, file_path_size, "./%s%.*sindex.html", ROOT_PATH, (int)message->target_len, message->target
            ) < 0) {
            return -1;
        }
    } else {
        if (snprintf(file_path, file_path_size, "./%s%.*s", ROOT_PATH, (int)message->target_len, message->target) < 0) {
            return -1;
        }
    }

    if (access(file_path, F_OK) != 0) {
        return -1;
    }

    struct stat path_stat;
    if (stat(file_path, &path_stat) != 0) {
        return -1;
    };
    if (S_ISREG(path_stat.st_mode) == 0 &&
        snprintf(
            file_path, file_path_size, "./%s%.*s/index.html", ROOT_PATH, (int)message->target_len, message->target
        ) < 0) {
        return -1;
    }

    return 0;
}

/**
 * Build HTTP header string into the buffer for the given file path and file size.
 */
int http_header_file(char *buffer, size_t buffer_size, char *file_path, off_t file_size) {
    char mime[100];
    http_get_mime(mime, 100, file_path);

    return snprintf(
        buffer, buffer_size,
        "HTTP/1.1 200 OK\r\n"
        "Server: misterabdul-http-server\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n\r\n",
        mime, file_size
    );
}

/**
 * Build entire HTTP message string into the buffer for the HTTP not found response.
 */
int http_message_not_found(char *buffer, size_t buffer_size) {
    return snprintf(
        buffer, buffer_size,
        "HTTP/1.1 404 NOT FOUND\r\n"
        "Server: misterabdul-http-server\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %lu\r\n\r\n"
        "%s",
        strlen(html_not_found), html_not_found
    );
}

/**
 * Build entire HTTP message string into the buffer for the HTTP method not allowed response.
 */
int http_message_not_allowed(char *buffer, size_t buffer_size) {
    return snprintf(
        buffer, buffer_size,
        "HTTP/1.1 405 METHOD NOT ALLOWED\r\n"
        "Server: misterabdul-http-server\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %lu\r\n\r\n"
        "%s",
        strlen(html_method_not_allowed), html_method_not_allowed
    );
}

/**
 * Build entire HTTP message string into the buffer for the HTTP internal server error response.
 */
int http_message_error(char *buffer, size_t buffer_size) {
    return snprintf(
        buffer, buffer_size,
        "HTTP/1.1 500 INTERNAL SERVER ERROR\r\n"
        "Server: misterabdul-http-server\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %lu\r\n\r\n"
        "%s",
        strlen(html_internal_server_error), html_internal_server_error
    );
}
