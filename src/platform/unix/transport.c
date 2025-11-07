#include <lib/transport.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <pthread.h>
#include <unistd.h>

#include <misc/logger.h>
#include <misc/macro.h>

#define BUFFER_SIZE 120

/**
 * @brief All the thread specific data.
 */
typedef struct thread_data {
    /**
     * @brief Global SSL error string buffer.
     */
    char buf[BUFFER_SIZE];

    /**
     * @brief Global SSL error code.
     */
    unsigned long err;
} thread_data_t;

/**
 * @brief The key for the thread specific data.
 */
static pthread_key_t g_data_key;

/**
 * @brief Get the thread specific data.
 *
 * @return Thread specific data instance.
 */
static inline thread_data_t* thread_data_get(void);

/**
 * @brief Cleanup the thread specific data.
 *
 * @param[out] ptr The pointer for the thread specific data instance.
 */
static void thread_data_cleanup(void* ptr);

/**
 * @brief Do the sendfile with user space buffer.
 *
 * @param[in]  connection  The transport connection instance.
 * @param[in]  file_fd     The file to be sent.
 * @param[in]  file_size   The size of the file.
 * @param[out] buffer      The buffer for the operation.
 * @param[in]  buffer_size The size of the buffer.
 * @param[out] sent        The portion of the file that has been sent.
 *
 * @return Size of the sent data or -1 for errors.
 */
static inline int buff_sendfile(
    connection_t* connection,
    int file_fd,
    off_t file_size,
    void* buffer,
    size_t buffer_size,
    off_t* sent
);

/**
 * @brief Do the SSL sendfile with user space buffer.
 *
 * @param[in]  connection  The transport connection instance.
 * @param[in]  file_fd     The file to be sent.
 * @param[in]  file_size   The size of the file.
 * @param[out] buffer      The buffer for the operation.
 * @param[in]  buffer_size The size of the buffer.
 * @param[out] sent        The portion of the file that has been sent.
 *
 * @return Size of the sent data or -1 for errors.
 */
static inline int bssl_sendfile(
    connection_t* connection,
    int file_fd,
    off_t file_size,
    void* buffer,
    size_t buffer_size,
    off_t* sent
);

/**
 * @copydoc lib_transport_init
 */
int lib_transport_init(void) {
    int _ret = pthread_key_create(&g_data_key, thread_data_cleanup);
    if (_ret != 0) {
        LOG_ERROR("pthread_key_create: %s (%d)\n", strerror(_ret), _ret);
        return -1;
    }

    OPENSSL_init();
    return 0;
}

/**
 * @copydoc lib_transport_cleanup
 */
void lib_transport_cleanup(void) {
    OPENSSL_cleanup();

    thread_data_t* _td = pthread_getspecific(g_data_key);
    if (_td) {
        free(_td);
    }

    int _ret = pthread_key_delete(g_data_key);
    if (_ret != 0) {
        LOG_ERROR("pthread_key_delete: %s (%d)\n", strerror(_ret), _ret);
    }
}

/**
 * @copydoc server_init
 */
void server_init(
    server_t* server,
    int family,
    const char address[16],
    int port,
    int max_connection
) {
    /* Initialize the transport server instance. */
    *server = (server_t){
        .address = (struct sockaddr_storage){0},
        .max_connection = max_connection,
        .certificate = NULL,
        .private_key = NULL,
        .address_size = 0,
        .socket = -1,
        .ssl = NULL,
    };

    /* IPv4 or IPv6 configuration. */
    if (family == AF_INET) {
        server->address_size = sizeof(struct sockaddr_in);
        struct sockaddr_in* _addr4 = (struct sockaddr_in*)&server->address;
        *_addr4 = (struct sockaddr_in){
            .sin_family = AF_INET,
            .sin_port = htons(port),
        };
        inet_ntop(AF_INET, &_addr4->sin_addr, (char*)address, INET_ADDRSTRLEN);
    } else {
        server->address_size = sizeof(struct sockaddr_in6);
        struct sockaddr_in6* _addr6 = (struct sockaddr_in6*)&server->address;
        *_addr6 = (struct sockaddr_in6){
            .sin6_family = AF_INET6,
            .sin6_port = htons(port),
        };
        inet_ntop(AF_INET, &_addr6->sin6_addr, (char*)address, INET_ADDRSTRLEN);
    }
}

/**
 * @copydoc server_setup
 */
int server_setup(server_t* server) {
    /* Request a new stream socket (TCP) for the server. */
    struct sockaddr* _address = (struct sockaddr*)&server->address;
    server->socket = socket(_address->sa_family, SOCK_STREAM, 0);
    if (server->socket <= 0) {
        LOG_ERROR("socket: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    /* Set the server socket to be non-blocking.  */
    int _opt = fcntl(server->socket, F_GETFL, 0);
    int _ret = fcntl(server->socket, F_SETFL, _opt | O_NONBLOCK);
    if (_ret == -1) {
        LOG_ERROR("fcntl: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    /* Set the server socket to reuse the address and ports.  */
    _opt = 1;
    _ret = setsockopt(
        server->socket, SOL_SOCKET, SO_REUSEADDR, &_opt, sizeof(int)
    );
    if (_ret == -1) {
        LOG_ERROR("setsockopt: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

#ifdef TCP_FASTOPEN

    /* Reduce TCP handshake latency. */
    _opt = 1;
    _ret = setsockopt(
        server->socket, IPPROTO_TCP, TCP_FASTOPEN, &_opt, sizeof(int)
    );
    if (_ret == -1) {
        LOG_ERROR("setsockopt: %s (%d)\n", strerror(errno), errno);
    }

#endif

    /* Bind the server socket to the given address and ports. */
    _ret = bind(server->socket, _address, server->address_size);
    if (_ret == -1) {
        LOG_ERROR("bind: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    /* Start listening from the server socket for a new connection. */
    _ret = listen(server->socket, server->max_connection);
    if (_ret == -1) {
        LOG_ERROR("listen: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * @copydoc server_enable_ssl
 */
int server_enable_ssl(
    server_t* server, const char* certificate, const char* private_key
) {
    thread_data_t* _td = thread_data_get();

    /* Create a new SSL context for the server. */
    server->ssl = SSL_CTX_new(TLS_server_method());
    if (server->ssl == NULL) {
        _td->err = ERR_get_error();
        ERR_error_string(_td->err, _td->buf);
        LOG_ERROR("SSL_CTX_new: %s\n", _td->buf);
        return -1;
    }

    /* Minimum TLS version 1.2 */
    SSL_CTX_set_min_proto_version(server->ssl, TLS1_2_VERSION);

    /* Assign the certificate file. */
    server->certificate = certificate;
    int _ret = SSL_CTX_use_certificate_file(
        server->ssl, server->certificate, SSL_FILETYPE_PEM
    );
    if (_ret != 1) {
        _td->err = ERR_get_error();
        ERR_error_string(_td->err, _td->buf);
        LOG_ERROR("SSL_CTX_use_certificate_file: %s\n", _td->buf);
        return -1;
    }

    /* Assign the private key file. */
    server->private_key = private_key;
    _ret = SSL_CTX_use_PrivateKey_file(
        server->ssl, server->private_key, SSL_FILETYPE_PEM
    );
    if (_ret != 1) {
        _td->err = ERR_get_error();
        ERR_error_string(_td->err, _td->buf);
        LOG_ERROR("SSL_CTX_use_PrivateKey_file: %s\n", _td->buf);
        return -1;
    }

    /* Validate the private key file with the assigned certificate file. */
    _ret = SSL_CTX_check_private_key(server->ssl);
    if (_ret != 1) {
        _td->err = ERR_get_error();
        ERR_error_string(_td->err, _td->buf);
        LOG_ERROR("SSL_CTX_check_private_key: %s\n", _td->buf);
        return -1;
    }

    /* More SSL configuration. */
    uint64_t _opt = SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
                    SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1;

#ifdef SSL_OP_ENABLE_KTLS

    _opt |= SSL_OP_ENABLE_KTLS;

#endif

    SSL_CTX_set_options(server->ssl, _opt);

    return 0;
}

/**
 * @copydoc server_accept
 */
int server_accept(server_t* server, connection_t* connection) {
    /* Accept the new connection's socket and address from the server. */
    struct sockaddr* _address = (struct sockaddr*)&connection->address;
    connection->socket =
        accept(server->socket, _address, &connection->address_size);
    if (connection->socket > 0) {
        return 0;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("accept: %s (%d)\n", strerror(errno), errno);
    }

    return -1;
}

/**
 * @copydoc server_close
 */
void server_close(server_t* server) {
    if (server->socket <= 0) {
        return;
    }

    /* Iterate until the server is closed. */
    for (;;) {
        if (close(server->socket) == 0) {
            break;
        }
    }
}

/**
 * @copydoc server_cleanup
 */
void server_cleanup(server_t* server) {
    if (server->ssl) {
        SSL_CTX_free(server->ssl);
    }
}

/**
 * @copydoc connection_init
 */
void connection_init(connection_t* connection, server_t* server) {
    *connection = (connection_t){
        .address = (struct sockaddr_storage){0},
        .ssl_established = false,
        .address_size = 0,
        .server = server,
        .socket = -1,
        .ssl = NULL,
    };
}

/**
 * @copydoc connection_setup
 */
int connection_setup(
    connection_t* connection,
    int receive_timeout,
    int receive_buffer,
    int send_timeout,
    int send_buffer
) {
    /* Set the connection socket to be non-blocking.  */
    int _opt = fcntl(connection->socket, F_GETFL, 0);
    int _ret = fcntl(connection->socket, F_SETFL, _opt | O_NONBLOCK);
    if (_ret == -1) {
        LOG_ERROR("fnctl: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    /* Set the connection's tcp socket nodelay. */
    _opt = 1;
    _ret = setsockopt(
        connection->socket, IPPROTO_TCP, TCP_NODELAY, &_opt, sizeof(int)
    );
    if (_ret == -1) {
        LOG_ERROR("setsockopt: %s (%d)\n", strerror(errno), errno);
    }

    /* Set the connection socket receive timeout. */
    struct timeval _topt = (struct timeval){0};
    _topt.tv_sec = receive_timeout;
    _ret = setsockopt(
        connection->socket,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &_topt,
        sizeof(struct timeval)
    );
    if (_ret == -1) {
        LOG_ERROR("setsockopt: %s (%d)\n", strerror(errno), errno);
    }

    /* Set the connection socket send timeout. */
    _topt.tv_sec = send_timeout;
    _ret = setsockopt(
        connection->socket,
        SOL_SOCKET,
        SO_SNDTIMEO,
        &_topt,
        sizeof(struct timeval)
    );
    if (_ret == -1) {
        LOG_ERROR("setsockopt: %s (%d)\n", strerror(errno), errno);
    }

    /* Set the kernel buffer for receiving data on the connection socket. */
    _opt = receive_buffer;
    _ret = setsockopt(
        connection->socket, SOL_SOCKET, SO_RCVBUF, &_opt, sizeof(int)
    );
    if (_ret == -1) {
        LOG_ERROR("setsockopt: %s (%d)\n", strerror(errno), errno);
    }

    /* Set the kernel buffer for sending data on the connection socket. */
    _opt = send_buffer;
    _ret = setsockopt(
        connection->socket, SOL_SOCKET, SO_SNDBUF, &_opt, sizeof(int)
    );
    if (_ret == -1) {
        LOG_ERROR("setsockopt: %s (%d)\n", strerror(errno), errno);
    }

    /* Set the connection socket to be kept alive. */
    _opt = 1;
    _ret = setsockopt(
        connection->socket, SOL_SOCKET, SO_KEEPALIVE, &_opt, sizeof(int)
    );
    if (_ret == -1) {
        LOG_ERROR("setsockopt: %s (%d)\n", strerror(errno), errno);
    }

    /* Controlling the `close` behavior. */
    struct linger _lopt = (struct linger){.l_onoff = 1, .l_linger = 0};
    _ret = setsockopt(
        connection->socket, SOL_SOCKET, SO_LINGER, &_lopt, sizeof(struct linger)
    );
    if (_ret == -1) {
        LOG_ERROR("setsockopt: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    if (connection->server->ssl) {
        thread_data_t* _td = thread_data_get();

        /* Create a new SSL instance from the server's SSL context. */
        connection->ssl = SSL_new(connection->server->ssl);
        if (connection->ssl == NULL) {
            _td->err = ERR_get_error();
            ERR_error_string(_td->err, _td->buf);
            LOG_ERROR("SSL_new: %s\n", _td->buf);
            return -1;
        }

        /* Assign the connection socket to the SSL instance. */
        if (SSL_set_fd(connection->ssl, connection->socket) != 1) {
            _td->err = ERR_get_error();
            ERR_error_string(_td->err, _td->buf);
            LOG_ERROR("SSL_set_fd: %s\n", _td->buf);
            return -1;
        }

#ifdef SSL_OP_ENABLE_KTLS

        /* Enable kernel offload for TLS operation. */
        SSL_set_options(connection->ssl, SSL_OP_ENABLE_KTLS);

#endif
    }

    return 0;
}

/**
 * @copydoc connection_establish_ssl
 */
int connection_establish_ssl(connection_t* connection) {
    if (connection->ssl == NULL) {
        return 0;
    }

    /* Accept the SSL handshake. */
    int _ret = SSL_accept(connection->ssl);
    if (_ret == 1) {
        connection->ssl_established = true;
        return 0;
    }

    thread_data_t* _td = thread_data_get();
    _td->err = SSL_get_error(connection->ssl, _ret);
    if (_td->err == SSL_ERROR_WANT_READ) {
        return 0;
    }

    ERR_error_string(_td->err, _td->buf);
    LOG_ERROR("SSL_accept: %s\n", _td->buf);
    return -1;
}

/**
 * @copydoc connection_get_error
 */
int connection_get_error(connection_t* connection) {
    int _sock = connection->socket, _error = 0;
    socklen_t _size = sizeof(int);

    if (getsockopt(_sock, SOL_SOCKET, SO_ERROR, &_error, &_size) == -1) {
        LOG_ERROR("getsockopt: %s (%d)\n", strerror(errno), errno);
    }

    return _error;
}

/**
 * @copydoc connection_receive
 */
int connection_receive(
    connection_t* connection, char* buffer, size_t size, size_t* received
) {
    /* To allow the insertion of null terminator at the end of the buffer. */
    size_t _limit = size - 1;

    if (connection->ssl) {
        thread_data_t* _td = thread_data_get();

        /* Iterate until the `SSL_read` function call return error. Ideally the
         * error should be `SSL_ERROR_WANT_READ` to notice that the next call
         * would be blocking. That's because the connection socket is operated
         * in non-blocking mode.
         */
        for (ssize_t _ret = 1; _ret >= 0;) {
            _ret = SSL_read(
                connection->ssl, buffer + *received, _limit - *received
            );
            if (_ret > 0) {
                *received += _ret;
                continue;
            }
            if (_ret == 0) {
                break;
            }
            _td->err = SSL_get_error(connection->ssl, _ret);
            if (_td->err == SSL_ERROR_WANT_READ) {
                break;
            }
            ERR_error_string(_td->err, _td->buf);
            LOG_ERROR("SSL_read: %s\n", _td->buf);
            return -1;
        }
    } else {
        /* Iterate until the `recv` function call return error. Ideally the
         * error should be `EAGAIN` or `EWOULDBLOCK` to notice that the next
         * call would be blocking. That's because the connection socket is
         * operated in non-blocking mode.
         */
        for (ssize_t _ret = 1; _ret > 0;) {
            _ret = recv(
                connection->socket, buffer + *received, _limit - *received, 0
            );
            if (_ret > 0) {
                *received += _ret;
                continue;
            }
            if (_ret == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            LOG_ERROR("recv: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
    }
    buffer[*received] = '\0';

    return 0;
}

/**
 * @copydoc connection_send
 */
int connection_send(
    connection_t* connection, char* buffer, size_t size, size_t* sent
) {
    if (connection->ssl) {
        thread_data_t* _td = thread_data_get();

        /* Iterate until the `SSL_write` function call return error. Ideally the
         * error should be `SSL_ERROR_WANT_WRITE` to notice that the next call
         * would be blocking. That's because the connection socket is operated
         * in non-blocking mode.
         */
        for (ssize_t _ret; *sent < size;) {
            _ret = SSL_write(connection->ssl, buffer + *sent, size - *sent);
            if (_ret > 0) {
                *sent += _ret;
                continue;
            }
            _td->err = SSL_get_error(connection->ssl, _ret);
            if (_td->err == SSL_ERROR_WANT_WRITE) {
                break;
            }
            ERR_error_string(_td->err, _td->buf);
            LOG_ERROR("SSL_write: %s\n", _td->buf);
            return -1;
        }
    } else {
        /* Iterate until the `send` function call return error. Ideally the
         * error should be `EAGAIN` or `EWOULDBLOCK` to notice that the next
         * call would be blocking. That's because the connection socket is
         * operated in non-blocking mode.
         */
        for (ssize_t _ret; *sent < size;) {
            _ret = send(connection->socket, buffer + *sent, size - *sent, 0);
            if (_ret > 0) {
                *sent += _ret;
                continue;
            }
            if (_ret == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            LOG_ERROR("send: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
    }

    return 0;
}

/**
 * @copydoc connection_sendfile
 */
int connection_sendfile(
    connection_t* connection,
    int file_fd,
    off_t file_size,
    char* buffer,
    size_t buffer_size,
    off_t* sent
) {
    if (connection->ssl) {
        return bssl_sendfile(
            connection, file_fd, file_size, buffer, buffer_size, sent
        );
    }

    return buff_sendfile(
        connection, file_fd, file_size, buffer, buffer_size, sent
    );
}

/**
 * @copydoc connection_close
 */
void connection_close(connection_t* connection, char* buffer, size_t size) {
    /* Close the established SSL connection. */
    if (connection->ssl && connection->ssl_established) {
        thread_data_t* _td = thread_data_get();
        int _ret = SSL_shutdown(connection->ssl);
        if (_ret < 0) {
            _td->err = SSL_get_error(connection->ssl, _ret);
            ERR_error_string(_td->err, _td->buf);
            LOG_ERROR("SSL_shutdown: %s\n", _td->buf);
        }
    }

    /* Exit immediately if the socket is not set. */
    if (connection->socket < 0) {
        return;
    }

    /* Tells the other end that we wouldn't send data anymore. */
    if (shutdown(connection->socket, SHUT_WR) == -1) {
        if (errno != ENOTCONN) {
            LOG_ERROR("shutdown: %s (%d)\n", strerror(errno), errno);
        }
    }

    /* Empty the socket receive buffer. */
    for (ssize_t _ret = 1; _ret > 0;) {
        _ret = recv(connection->socket, buffer, size, MSG_TRUNC);
        if (_ret > 0) {
            continue;
        }
        if (_ret == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("recv: %s (%d)\n", strerror(errno), errno);
        }
        break;
    }

    /* Iterate until the connection is closed. */
    for (;;) {
        if (close(connection->socket) == 0) {
            break;
        }
    }
}

/**
 * @copydoc connection_cleanup
 */
void connection_cleanup(connection_t* connection) {
    /* Cleanup SSL data. */
    if (connection->ssl) {
        SSL_free(connection->ssl);
    }
}

/**
 * @copydoc buff_sendfile
 */
static inline int buff_sendfile(
    connection_t* connection,
    int file_fd,
    off_t file_size,
    void* buffer,
    size_t buffer_size,
    off_t* sent
) {
    size_t _remaining, _chunk;
    ssize_t _read;
    off_t _seek;

    for (ssize_t _ret; *sent < file_size;) {
        /* Set the file offset before reading. */
        _seek = lseek(file_fd, *sent, SEEK_SET);
        if (_seek == -1) {
            LOG_ERROR("lseek: %s (%d)\n", strerror(errno), errno);
            return -1;
        }

        /* Read the content of the file into the buffer. */
        _remaining = (size_t)(file_size - *sent);
        _chunk = _remaining <= buffer_size ? _remaining : buffer_size;
        _read = read(file_fd, buffer, _chunk);
        if (_read == -1) {
            LOG_ERROR("read: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
        if (_read == 0) {
            break;
        }

        /* Send the content of the buffer. */
        _ret = send(connection->socket, buffer, _read, 0);
        if (_ret > 0) {
            *sent += _ret;
            continue;
        }
        if (_ret == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        LOG_ERROR("send: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * @copydoc bssl_sendfile
 */
static inline int bssl_sendfile(
    connection_t* connection,
    int file_fd,
    off_t file_size,
    void* buffer,
    size_t buffer_size,
    off_t* sent
) {
    thread_data_t* _td = thread_data_get();
    size_t _remaining, _chunk;
    ssize_t _read;
    off_t _seek;

    for (ssize_t _ret; *sent < file_size;) {
        /* Set the file offset before reading. */
        _seek = lseek(file_fd, *sent, SEEK_SET);
        if (_seek == -1) {
            LOG_ERROR("lseek: %s (%d)\n", strerror(errno), errno);
            return -1;
        }

        /* Read the content of the file into the buffer. */
        _remaining = (size_t)(file_size - *sent);
        _chunk = _remaining <= buffer_size ? _remaining : buffer_size;
        _read = read(file_fd, buffer, _chunk);
        if (_read == -1) {
            LOG_ERROR("read: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
        if (_read == 0) {
            break;
        }

        /* Send the content of the buffer via SSL. */
        _ret = SSL_write(connection->ssl, buffer, _read);
        if (_ret > 0) {
            *sent += _ret;
            continue;
        }
        if (_ret == 0) {
            break;
        }
        _td->err = SSL_get_error(connection->ssl, _ret);
        if (_td->err == SSL_ERROR_WANT_WRITE) {
            break;
        }
        ERR_error_string(_td->err, _td->buf);
        LOG_ERROR("SSL_write: %s\n", _td->buf);
        return -1;
    }

    return 0;
}

/**
 * @copydoc thread_data_get
 */
static inline thread_data_t* thread_data_get(void) {
    thread_data_t* _td = pthread_getspecific(g_data_key);
    if (_td) {
        return _td;
    }

    _td = malloc(sizeof(thread_data_t));
    *_td = (thread_data_t){.err = 0};
    int _ret = pthread_setspecific(g_data_key, _td);
    if (_ret != 0) {
        LOG_ERROR("pthread_setspecific: %s (%d)\n", strerror(_ret), _ret);
    }

    return _td;
}

/**
 * @copydoc thread_data_cleanup
 */
static void thread_data_cleanup(void* ptr) {
    thread_data_t* _td = (thread_data_t*)ptr;
    if (_td) {
        free(_td);
    }
}
