#include "http.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"

int http_parse_request(
    const char *request,
    size_t request_len,
    const char **method,
    size_t *method_len,
    const char **target,
    size_t *target_len,
    const char **version,
    size_t *version_len,
    http_header_t *headers,
    size_t *headers_len,
    const char **body,
    size_t *body_len
) {
    size_t i = 0, j, headers_size = *headers_len;

    /* parse method */
    for (j = i; i < request_len; i++) {
        if (request[i] == ' ' || request[i] == '\r' || request[i] == '\n' || request[i] == '\0') { break; }
    }
    if (request[i] != ' ') { return 1; }
    *method = &request[j];
    *method_len = i - j;

    /* parse target */
    for (j = ++i; i < request_len; i++) {
        if (request[i] == ' ' || request[i] == '\r' || request[i] == '\n' || request[i] == '\0') { break; }
    }
    if (request[i] != ' ') { return 1; }
    *target = &request[j];
    *target_len = i - j;

    /* parse version */
    for (j = ++i; i < request_len; i++) {
        if (request[i] == ' ' || request[i] == '\r' || request[i] == '\n' || request[i] == '\0') { break; }
    }
    *version = &request[j];
    *version_len = i - j;

    /* parse headers */
    *headers_len = 0;
    for (; *headers_len < headers_size && i < request_len;) {
        if (request[i] == '\0') { break; }

        for (; i < request_len; i++) {
            if (request[i] == '\n' || request[i] == '\0') { break; }
        }
        if (request[i] == '\0') { break; }

        for (j = ++i; i < request_len; i++) {
            if (request[i] == ':' || request[i] == '\n' || request[i] == '\0') { break; }
        }
        if (request[i] != ':') { break; }

        headers[*headers_len].name = (char *)&request[j];
        headers[*headers_len].name_len = i - j;

        for (++i; i < request_len; i++) {
            if (request[i] != ' ' || request[i] == '\0') { break; }
        }
        if (request[i] == '\0') { break; }

        for (j = i; i < request_len; i++) {
            if (request[i] == '\r' || request[i] == '\n' || request[i] == '\0') { break; }
        }
        headers[*headers_len].value = (char *)&request[j];
        headers[*headers_len].value_len = i - j;

        (*headers_len)++;
    }

    /* parse body */
    if (++i < request_len) {
        *body = (char *)&request[i];
        *body_len = request_len - i;
    } else {
        *body = NULL;
        *body_len = 0;
    }

    return 0;
}

int http_resolve_file_path(char *file_path, size_t file_path_len, const char *target, size_t target_len) {
    if (target_len > 1) {
        for (size_t i = 1; i < target_len; i++) {
            if (target[i] == '?') {
                target_len = i;
                break;
            }
            if (target[i - 1] == '.' && target[i] == '.') { return -1; }
        }
    }

    if (target_len <= 0 || target[target_len - 1] == '/') {
        if (snprintf(file_path, file_path_len, "./%s%.*sindex.html", ROOT_PATH, (int)target_len, target) < 0) {
            return -1;
        }
    } else {
        if (snprintf(file_path, file_path_len, "./%s%.*s", ROOT_PATH, (int)target_len, target) < 0) { return -1; }
    }

    if (access(file_path, F_OK) != 0) { return -1; }

    struct stat path_stat;
    if (stat(file_path, &path_stat) != 0) { return -1; };
    if (S_ISREG(path_stat.st_mode) == 0 &&
        snprintf(file_path, file_path_len, "./%s%.*s/index.html", ROOT_PATH, (int)target_len, target) < 0) {
        return -1;
    }

    return 0;
}

void http_get_mime(char *buffer, size_t buffer_size, const char *file_path);

int http_sendfile(int socket, const char *file_path);

int http_respond_file(int socket, char *buffer, size_t buffer_size, const char *file_path, int flag) {
    int bytes_to_send;
    ssize_t sent_bytes;
    char mime[100];

    http_get_mime(mime, 100, file_path);
    bytes_to_send = snprintf(
        buffer, buffer_size,
        "HTTP/1.0 200 OK\r\n"
        "Server: misterabdul-http-server\r\n"
        "Content-Type: %s\r\n\r\n",
        mime
    );

    sent_bytes = send(socket, buffer, bytes_to_send, flag);
    if (sent_bytes < 0) { return -1; }

    if (http_sendfile(socket, file_path) < 0) { return -1; }

    return 0;
}

int http_respond_not_found(int socket, char *buffer, size_t buffer_size, int flag) {
    int bytes_to_send;
    ssize_t sent_bytes;

    bytes_to_send = snprintf(
        buffer, buffer_size,
        "HTTP/1.0 404 NOT FOUND\r\n"
        "Server: misterabdul-http-server\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "  <head><title>Not found</title></head>\n"
        "  <body><div><h1>Not found.</h1></div></body>\n"
        "</html>\n"
    );

    sent_bytes = send(socket, buffer, bytes_to_send, flag);
    if (sent_bytes < 0) { return -1; }

    return 0;
}

int http_respond_method_not_allowed(int socket, char *buffer, size_t buffer_size, int flag) {
    int bytes_to_send;
    ssize_t sent_bytes;

    bytes_to_send = snprintf(
        buffer, buffer_size,
        "HTTP/1.0 405 METHOD NOT ALLOWED\r\n"
        "Server: misterabdul-http-server\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "  <head><title>Method not allowed</title></head>\n"
        "  <body><div><h1>Method not allowed.</h1></div></body>\n"
        "</html>\n"
    );

    sent_bytes = send(socket, buffer, bytes_to_send, flag);
    if (sent_bytes < 0) { return -1; }

    return 0;
}

int http_sendfile(int socket, const char *file_path) {
    int file_fd = open(file_path, O_RDONLY);
    if (file_fd < 0) { return -1; }

    struct stat st;
    if (fstat(file_fd, &st) < 0) { return -1; }
    size_t len = st.st_size;

    off_t offset = 0;
    for (ssize_t sent = 1; sent > 0;) {
        sent = sendfile(socket, file_fd, &offset, len);
        if (sent < 0) { return -1; }
    }

    return 0;
}

int mimes_len = 76;
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

void http_get_mime(char *buffer, size_t buffer_size, const char *file_path) {
    char *extension = NULL;
    for (char *c = (char *)file_path; *c != '\0'; c++) {
        if (*c == '.') { extension = c; }
    }

    if (extension) {
        for (int i = 0; i < mimes_len; i++) {
            if (strcmp(extension, mimes[i][0]) != 0) { continue; }
            strncpy(buffer, mimes[i][1], buffer_size);
            return;
        }
    }

    strncpy(buffer, "application/octet-stream", buffer_size);
}
