#ifndef http_h
#define http_h

#include <sys/types.h>

#include "config.h"

typedef struct http_header {
    char *name, *value;
    size_t name_len, value_len;
} http_header_t;

typedef struct http_message {
    char *method, *target, *version, *body;
    http_header_t headers[MAX_HEADERS];
    size_t method_len, target_len, version_len, body_len, headers_len;
} http_message_t;

int http_parse_request(http_message_t *message, const char *request, size_t request_len);

int http_resolve_file_path(http_message_t *message, char *file_path, size_t file_path_len);

int http_respond_file(int socket, char *buffer, size_t buffer_size, const char *file_path, int flags);

int http_respond_not_found(int socket, char *buffer, size_t buffer_size, int flags);

int http_respond_method_not_allowed(int socket, char *buffer, size_t buffer_size, int flags);

#endif
