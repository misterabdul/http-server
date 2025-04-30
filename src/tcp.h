#ifndef tcp_h
#define tcp_h

#include <netinet/in.h>
#include <sys/epoll.h>

#include "config.h"

/**
 * @brief TCP server representation.
 */
typedef struct tcp_server {
    struct sockaddr *address;
    socklen_t address_length;
    int socket, max_connection;
} tcp_server_t;

/**
 * @brief TCP connection representation.
 */
typedef struct tcp_connection {
    struct sockaddr *address;
    socklen_t address_length;
    int socket;
    void *context;
} tcp_connection_t;

/**
 * @brief Initialize the TCP server.
 *
 * @param[out] server The TCP server instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int tcp_server_init(tcp_server_t *server);

/**
 * @brief Accept new connection from the TCP server.
 *
 * @param[out] server     The TCP server instance.
 * @param[out] connection The TCP connection instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int tcp_server_accept(tcp_server_t *server, tcp_connection_t *connection);

/**
 * @brief Close the TCP server.
 *
 * @param[out] server The TCP server instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
void tcp_server_close(tcp_server_t *server);

/**
 * @brief Receive data from the TCP connection.
 *
 * @param[out] connection  The TCP connection instance.
 * @param[out] buffer      The buffer to contain the data.
 * @param[in]  buffer_size The size of the buffer.
 * @param[in]  flags       The flags for the internal receive function.
 *
 * @return Size of the data received or -1 for errors.
 */
ssize_t tcp_connection_receive(tcp_connection_t *connection, char *buffer, size_t buffer_size, int flags);

/**
 * @brief Send data into the TCP connection.
 *
 * @param[out] connection  The TCP connection instance.
 * @param[in]  buffer      The buffer containing the data.
 * @param[in]  buffer_size The size of the buffer.
 * @param[in]  flags       The flags for the internal send function.
 *
 * @return Size of the sent data or -1 for errors.
 */
ssize_t tcp_connection_send(tcp_connection_t *connection, char *buffer, size_t buffer_size, int flags);

/**
 * @brief Send file into the TCP connection.
 *
 * @param[out] connection The TCP connection instance.
 * @param[in]  file_fd    The file descriptor.
 * @param[in]  file_size  The actual size of the file.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int tcp_connection_sendfile(tcp_connection_t *connection, int file_fd, off_t file_size);

/**
 * @brief Get the error code from the TCP connection.
 *
 * @param[out] connection The TCP connection instance.
 *
 * @return Error code.
 */
int tcp_connection_get_error(tcp_connection_t *connection);

/**
 * @brief Close the TCP connection.
 *
 * @param[out] connection  The TCP connection instance.
 * @param[out] buffer      The buffer for receiving data before closing.
 * @param[in]  buffer_size The size of the buffer.
 */
void tcp_connection_close(tcp_connection_t *connection, char *buffer, size_t buffer_size);

#endif
