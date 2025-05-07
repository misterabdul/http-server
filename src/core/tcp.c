#include "tcp.h"

#include <contracts/tcp.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "log.h"

/**
 * Initialize TCP socket.
 */
int tcp_server_init(tcp_server_t *server) {
    if ((server->socket = socket(server->address->sa_family, SOCK_STREAM, 0)) <= 0) {
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

    if (server->address->sa_family == AF_INET6) {
        socket_option = 0;
        if (setsockopt(server->socket, IPPROTO_IPV6, IPV6_V6ONLY, &socket_option, sizeof(int)) < 0) {
            LOG_ERROR("failed to set the tcp socket reuse address: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
    }

    if (bind(server->socket, server->address, server->address_length) < 0) {
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
    connection->socket = accept(server->socket, connection->address, &connection->address_length);
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

    int _socket_option = 1;
    if (setsockopt(connection->socket, SOL_SOCKET, SO_KEEPALIVE, &_socket_option, sizeof(int)) < 0) {
        LOG_ERROR("failed to set the connection socket keepalive: %s (%d)\n", strerror(errno), errno);
    }

    struct linger _linger_option = (struct linger){.l_onoff = 1, .l_linger = 0};
    if (setsockopt(connection->socket, SOL_SOCKET, SO_LINGER, &_linger_option, sizeof(struct linger)) < 0) {
        LOG_ERROR("failed to set the connection socket linger: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    struct timeval _timeout_option = (struct timeval){.tv_sec = 10, .tv_usec = 0};
    if (setsockopt(connection->socket, SOL_SOCKET, SO_RCVTIMEO, &_timeout_option, sizeof(struct timeval)) < 0) {
        LOG_ERROR("failed to set the connection socket receive timeout: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (setsockopt(connection->socket, SOL_SOCKET, SO_SNDTIMEO, &_timeout_option, sizeof(struct timeval)) < 0) {
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
    ssize_t _bytes_received;

    for (;;) {
        _bytes_received = recv(connection->socket, buffer, buffer_size, flags);
        if (_bytes_received == 0) {
            return 0;
        }
        if (_bytes_received < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            LOG_ERROR("failed to receive from the connection socket: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
        break;
    }
    buffer[_bytes_received] = '\0';

    return _bytes_received;
}

/**
 * Send data from the buffer into the connection socket.
 */
ssize_t tcp_connection_send(tcp_connection_t *connection, char *buffer, size_t buffer_size, int flags) {
    size_t _total_bytes_sent = 0;

    for (ssize_t _bytes_sent; _total_bytes_sent < buffer_size; _total_bytes_sent += (size_t)_bytes_sent) {
        _bytes_sent = send(connection->socket, buffer + _total_bytes_sent, buffer_size - _total_bytes_sent, flags);
        if (_bytes_sent == 0) {
            return 0;
        }
        if (_bytes_sent < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            LOG_ERROR("failed to send data to the connection socket: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
    }

    return _total_bytes_sent;
}

/**
 * Send file into the connection socket.
 */
int tcp_connection_sendfile(tcp_connection_t *connection, int file_fd, off_t file_size) {
    off_t _offset = 0;
    for (ssize_t _bytes_sent; _offset < file_size;) {
        _bytes_sent = platform_sendfile(connection->socket, file_fd, &_offset, (size_t)(file_size - _offset));
        if (_bytes_sent == 0) {
            return 0;
        }
        if (_bytes_sent < 0) {
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
    socklen_t _size = sizeof(int);
    int _socket_error;

    if (getsockopt(connection->socket, SOL_SOCKET, SO_ERROR, &_socket_error, &_size) < 0) {
        LOG_ERROR("failed to get the connection socket error: %s (%d)\n", strerror(errno), errno);
    }

    return _socket_error;
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
    for (ssize_t _bytes_received = 1; _bytes_received > 0;) {
        _bytes_received = recv(connection->socket, buffer, buffer_size, MSG_TRUNC);
    }
    for (;;) {
        if (close(connection->socket) == 0) {
            break;
        }
    }
}
