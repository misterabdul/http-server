#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "http.h"
#include "log.h"

volatile sig_atomic_t run;

typedef struct http_message {
    char *method, *target, *version, *body;
    http_header_t headers[MAX_HEADERS];
    size_t method_len, target_len, version_len, body_len, headers_len;
} http_message_t;

void signal_handler(int signum);
int listen_socket_create(in_addr_t address, uint16_t port);
int listen_socket_accept(int listen_socket, struct sockaddr_in *request_address);

int main(void) {
    run = 1;

    LOG_DEBUG("creating socket\n");
    int listen_sock = listen_socket_create(INADDR_ANY, LISTEN_PORT);
    if (listen_sock < 0) {
        LOG_ERROR("failed creating listen socket: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    int req_sock;
    struct sockaddr_in req_addr;

    char buffer[REQUEST_BUFFER_SIZE], full_path[PATH_BUFFER_SIZE];
    http_message_t request;
    ssize_t bytes_received;

    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    for (; run;) {
        req_sock = listen_socket_accept(listen_sock, &req_addr);
        if (req_sock < 0) {
            LOG_ERROR("failed to accept the listen socket: %d\n", errno);
            continue;
        }

        bytes_received = recv(req_sock, buffer, REQUEST_BUFFER_SIZE, 0);
        if (bytes_received < 0) {
            LOG_ERROR("failed to receive from the request socket: %d\n", errno);
            close(req_sock);
            continue;
        }
        buffer[bytes_received] = '\0';

        if (bytes_received == 0) {
            sprintf(full_path, "./%s/index.html", ROOT_PATH);
            if (http_respond_file(req_sock, buffer, REQUEST_BUFFER_SIZE, full_path, MSG_NOSIGNAL) < 0) {
                LOG_ERROR("failed to respond the request socket: %d\n", errno);
            }
        }

        request.headers_len = MAX_HEADERS;
        if (http_parse_request(
                buffer, bytes_received, (const char **)&request.method, &request.method_len,
                (const char **)&request.target, &request.target_len, (const char **)&request.version,
                &request.version_len, request.headers, &request.headers_len, (const char **)&request.body,
                &request.body_len
            )) {
            LOG_ERROR("failed to parse http request\n");
            close(req_sock);
            continue;
        }

        if (strncmp(request.method, "GET", request.method_len) != 0) {
            http_respond_method_not_allowed(req_sock, buffer, REQUEST_BUFFER_SIZE, MSG_NOSIGNAL);
            close(req_sock);
            continue;
        }

        if (http_resolve_file_path(full_path, PATH_BUFFER_SIZE, request.target, request.target_len)) {
            http_respond_not_found(req_sock, buffer, REQUEST_BUFFER_SIZE, MSG_NOSIGNAL);
            close(req_sock);
            continue;
        }

        if (http_respond_file(req_sock, buffer, REQUEST_BUFFER_SIZE, full_path, MSG_NOSIGNAL) < 0) {
            LOG_ERROR("failed to respond the request socket: %d\n", errno);
        }

        close(req_sock);
    }
    close(listen_sock);

    return 0;
}

void signal_handler(int signum) {
    if (signum == SIGINT) { run = 0; }
}

int listen_socket_create(in_addr_t address, uint16_t port) {
    int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket <= 0) { return listen_socket; }

    struct sockaddr_in listen_addr = {.sin_family = AF_INET, .sin_addr.s_addr = address, .sin_port = htons(port)};
    if (bind(listen_socket, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) { return listen_socket; }

    if (listen(listen_socket, MAX_CONNECTION) < 0) { return listen_socket; }

    return listen_socket;
}

int listen_socket_accept(int listen_socket, struct sockaddr_in *request_address) {
    socklen_t address_size = sizeof(struct sockaddr_in);

    return accept(listen_socket, (struct sockaddr *)request_address, &address_size);
}
