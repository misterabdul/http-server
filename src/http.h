#ifndef http_h
#define http_h

#include <sys/types.h>

typedef struct http_header {
    char *name, *value;
    size_t name_len, value_len;
} http_header_t;

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
);

int http_resolve_file_path(char *file_path, size_t file_path_len, const char *target, size_t target_len);

int http_respond_file(int socket, char *buffer, size_t buffer_size, const char *file_path, int flag);

int http_respond_not_found(int socket, char *buffer, size_t buffer_size, int flag);

int http_respond_method_not_allowed(int socket, char *buffer, size_t buffer_size, int flag);

#endif
