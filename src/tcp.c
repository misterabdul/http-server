#include "tcp.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "log.h"

/**
 * Initialize TCP socket.
 */
int tcp_server_init(tcp_server_t *server) {
    if ((server->socket = socket(AF_INET, SOCK_STREAM, 0)) <= 0) {
        LOG_ERROR("failed to create tcp socket: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (fcntl(server->socket, F_SETFL, fcntl(server->socket, F_GETFL, 0) | O_NONBLOCK) == -1) {
        LOG_ERROR("failed to set the tcp socket nonblock: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    int socket_option = 1;
    if (setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &socket_option, sizeof(int)) < 0) {
        LOG_ERROR("failed to set the tcp socket reuse address: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (bind(server->socket, (struct sockaddr *)&server->address, sizeof(struct sockaddr_in)) < 0) {
        LOG_ERROR("failed to bind the tcp socket to given host and port: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (listen(server->socket, server->max_connection) < 0) {
        LOG_ERROR("failed to listen to the tcp socket: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Accept new connection and initialize it.
 */
int tcp_server_accept(tcp_server_t *server, tcp_connection_t *connection) {
    socklen_t address_size = sizeof(struct sockaddr_in);

    connection->socket = accept(server->socket, (struct sockaddr *)&connection->address, &address_size);
    if (connection->socket < 0) {
        if (errno == EAGAIN) {
            return -1;
        }

        LOG_ERROR("failed to accept the tcp socket: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    if (fcntl(connection->socket, F_SETFL, fcntl(connection->socket, F_GETFL, 0) | O_NONBLOCK) == -1) {
        LOG_ERROR("failed to set the connection socket nonblock: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    int socket_option = 1;
    if (setsockopt(connection->socket, SOL_SOCKET, SO_KEEPALIVE, &socket_option, sizeof(int)) < 0) {
        LOG_ERROR("failed to set the connection socket keepalive: %s (%d)\n", strerror(errno), errno);
    }

    struct linger linger_option = (struct linger){.l_onoff = 1, .l_linger = 0};
    if (setsockopt(connection->socket, SOL_SOCKET, SO_LINGER, &linger_option, sizeof(struct linger)) < 0) {
        LOG_ERROR("failed to set the connection socket linger: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    struct timeval timeout_option = (struct timeval){.tv_sec = 10, .tv_usec = 0};
    if (setsockopt(connection->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout_option, sizeof(struct timeval)) < 0) {
        LOG_ERROR("failed to set the connection socket receive timeout: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (setsockopt(connection->socket, SOL_SOCKET, SO_SNDTIMEO, &timeout_option, sizeof(struct timeval)) < 0) {
        LOG_ERROR("failed to set the connection socket send timeout: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Close the TCP socket.
 */
void tcp_server_close(tcp_server_t *server) {
    for (;;) {
        if (close(server->socket) == 0) {
            break;
        }
    }
}

/**
 * Receive data into the buffer from the connection socket.
 */
ssize_t tcp_connection_receive(tcp_connection_t *connection, char *buffer, size_t buffer_size, int flags) {
    ssize_t bytes_received;

    for (;;) {
        bytes_received = recv(connection->socket, buffer, buffer_size, flags);
        if (bytes_received == 0) {
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

    return bytes_received;
}

/**
 * Send data from the buffer into the connection socket.
 */
ssize_t tcp_connection_send(tcp_connection_t *connection, char *buffer, size_t buffer_size, int flags) {
    size_t total_bytes_sent = 0;

    for (ssize_t bytes_sent; total_bytes_sent < buffer_size; total_bytes_sent += (size_t)bytes_sent) {
        bytes_sent = send(connection->socket, buffer + total_bytes_sent, buffer_size - total_bytes_sent, flags);
        if (bytes_sent == 0) {
            return 0;
        }
        if (bytes_sent < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            LOG_ERROR("failed to send data to the connection socket: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
    }

    return total_bytes_sent;
}

/**
 * Send file into the connection socket.
 */
int tcp_connection_sendfile(tcp_connection_t *connection, int file_fd, off_t file_size) {
    off_t offset = 0;
    for (ssize_t bytes_sent; offset < file_size;) {
        bytes_sent = sendfile(connection->socket, file_fd, &offset, (size_t)(file_size - offset));
        if (bytes_sent == 0) {
            return 0;
        }
        if (bytes_sent < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            LOG_ERROR("failed to send file to the connection socket: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
    }

    return 0;
}

/**
 * Get socket error number.
 */
int tcp_connection_get_error(tcp_connection_t *connection) {
    socklen_t size = sizeof(int);
    int socket_error;

    if (getsockopt(connection->socket, SOL_SOCKET, SO_ERROR, &socket_error, &size) < 0) {
        LOG_ERROR("failed to get the connection socket error: %s (%d)\n", strerror(errno), errno);
    }

    return socket_error;
}

/**
 * Properly close TCP connection socket.
 */
void tcp_connection_close(tcp_connection_t *connection, char *buffer, size_t buffer_size) {
    if (shutdown(connection->socket, SHUT_WR) < 0) {
        if (errno != ENOTCONN) {
            LOG_ERROR("failed to shutdown connection socket: %s (%d)\n", strerror(errno), errno);
        }
    }
    for (ssize_t bytes_received = 1; bytes_received > 0;) {
        bytes_received = recv(connection->socket, buffer, buffer_size, MSG_TRUNC);
    }
    for (;;) {
        if (close(connection->socket) == 0) {
            break;
        }
    }
}
