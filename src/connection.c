#include "connection.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/time.h>
#include <unistd.h>

#include "http.h"
#include "log.h"

int connection_setup_socket(int accept_socket) {
    struct timeval timeout = (struct timeval){.tv_sec = 10, .tv_usec = 0};
    struct linger linger = (struct linger){.l_onoff = 1, .l_linger = 0};
    int socket_opt = 1;

    if (fcntl(accept_socket, F_SETFL, fcntl(accept_socket, F_GETFL, 0) | O_NONBLOCK) == -1) {
        LOG_ERROR("failed setting connection socket nonblock: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (setsockopt(accept_socket, SOL_SOCKET, SO_KEEPALIVE, &socket_opt, sizeof(int)) < 0) {
        LOG_ERROR("failed setting connection socket keepalive: %s (%d)\n", strerror(errno), errno);
    }
    if (setsockopt(accept_socket, SOL_SOCKET, SO_LINGER, &linger, sizeof(struct linger)) < 0) {
        LOG_ERROR("failed setting connection linger: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (setsockopt(accept_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval)) < 0) {
        LOG_ERROR("failed setting connection receive timeout: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (setsockopt(accept_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(struct timeval)) < 0) {
        LOG_ERROR("failed setting connection send timeout: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

int connection_accept(connection_t **connection, int listen_socket) {
    socklen_t address_size = sizeof(struct sockaddr_in);
    *connection = malloc(sizeof(connection_t));

    int accept_socket = accept(listen_socket, (struct sockaddr *)&(*connection)->address, &address_size);
    if (accept_socket < 0) {
        if (errno == EAGAIN) {
            free(*connection);
            *connection = NULL;
            return 0;
        }

        LOG_ERROR("failed to accept the listen socket: %s (%d)\n", strerror(errno), errno);
        free(*connection);
        *connection = NULL;
        return -1;
    }
    if (connection_setup_socket(accept_socket) == -1) {
        LOG_ERROR("failed to setup the accept socket: %s (%d)\n", strerror(errno), errno);
        free(*connection);
        *connection = NULL;
        return -1;
    }

    (*connection)->socket = accept_socket;
    (*connection)->is_done = 0;

    return 0;
}

int connection_receive(connection_t *connection, char *buffer, size_t buffer_size, int flags) {
    http_message_t message;
    ssize_t bytes_received;

    for (;;) {
        bytes_received = recv(connection->socket, buffer, buffer_size, flags);
        if (bytes_received == 0) {
            connection->is_done = 1;
            return 0;
        }
        if (bytes_received < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            LOG_ERROR("failed to receive from the connection socket: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
        break;
    }

    buffer[bytes_received] = '\0';
    message.headers_len = MAX_HEADERS;
    if (http_parse_request(&message, buffer, bytes_received)) {
        LOG_ERROR("failed to parse http request\n");
        return -1;
    }

    connection->is_done = 0;
    connection->response_status = 200;
    if (strncmp(message.method, "GET", message.method_len) != 0) {
        connection->response_file[0] = '\0';
        connection->response_status = 405;
    } else if (http_resolve_file_path(&message, connection->response_file, PATH_BUFFER_SIZE)) {
        connection->response_file[0] = '\0';
        connection->response_status = 404;
    }

    return 0;
}

int connection_respond(connection_t *connection, char *buffer, size_t buffer_size, int flags) {
    int result;

    if (connection->is_done) {
        return 0;
    }
    switch (connection->response_status) {
        default:
            result = http_respond_file(connection->socket, buffer, buffer_size, connection->response_file, flags);
            break;
        case 404:
            result = http_respond_not_found(connection->socket, buffer, buffer_size, flags);
            break;
        case 405:
            result = http_respond_method_not_allowed(connection->socket, buffer, buffer_size, flags);
            break;
    }
    if (result < 0) {
        connection->is_done = 1;
        return -1;
    }

    return 0;
}

int connection_error(connection_t *connection) {
    socklen_t size = sizeof(int);
    int socket_error;

    if (getsockopt(connection->socket, SOL_SOCKET, SO_ERROR, &socket_error, &size) < 0) {
        LOG_ERROR("failed getting connection socket error: %s (%d)\n", strerror(errno), errno);
    }

    return socket_error;
}

void connection_close(connection_t **connection, char *buffer, size_t buffer_size) {
    int socket = (*connection)->socket;
    free(*connection);
    *connection = NULL;

    if (shutdown(socket, SHUT_WR) < 0) {
        if (errno != ENOTCONN) {
            LOG_ERROR("failed to shutdown connection socket: %s (%d)\n", strerror(errno), errno);
        }
    }
    for (ssize_t n_bytes;;) {
        n_bytes = recv(socket, buffer, buffer_size, MSG_TRUNC);
        if (n_bytes == 0) {
            break;
        }
    }
    for (;;) {
        if (close(socket) == 0) {
            break;
        }
    }
}
