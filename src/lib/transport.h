#ifndef lib_transport_h
#define lib_transport_h

#include <openssl/ssl.h>
#include <stdbool.h>
#include <sys/socket.h>

/**
 * @brief Transport server representation.
 */
typedef struct server {
    /**
     * @brief The listen socket of the server.
     */
    int socket;

    /**
     * @brief The address buffer for the listen socket.
     */
    struct sockaddr_storage address;

    /**
     * @brief The size of the address.
     */
    socklen_t address_size;

    /**
     * @brief TLS related data. Just let the library handle this.
     */
    struct ssl_ctx_st* ssl_context;

    /**
     * @brief The TLS certificate file path.
     */
    const char* certificate;

    /**
     * @brief The TLS private key file path.
     */
    const char* private_key;

    /**
     * @brief The maximum number of connection that could be accepted at a time.
     */
    int max_connection;
} server_t;

/**
 * @brief Transport connection representation.
 */
typedef struct connection {
    /**
     * @brief The connection socket.
     */
    int socket;

    /**
     * @brief The address buffer for the connection socket.
     */
    struct sockaddr_storage address;

    /**
     * @brief The size of the address.
     */
    socklen_t address_size;

    /**
     * @brief OpenSSL related data. Just let the OpenSSL handle this.
     */
    struct ssl_st* ssl;

    /**
     * @brief Whether the TLS connection successfully established or not.
     */
    bool tls_established;

    /**
     * @brief The transport server instance from which the connection accepted.
     * Just for helper, this pointer won't be freed during the cleanup.
     */
    server_t* server;
} connection_t;

/**
 * @brief Initialize the library.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int lib_transport_init(void);

/**
 * @brief Clean all the library related stuff.
 */
void lib_transport_cleanup(void);

/**
 * @brief Initialize the transport server.
 *
 * @param[out] server         The transport server instance.
 * @param[in]  family         The address family.
 * @param[in]  address        The address for the server.
 * @param[in]  port           The port number for the server.
 * @param[in]  max_connection The maximum number of connections that could be
 * accepted at a time.
 */
void server_init(
    server_t* server,
    int family,
    const char address[16],
    int port,
    int max_connection
);

/**
 * @brief Setup the transport server, set TCP socket related stuff.
 *
 * @param[out] server The transport server instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int server_setup(server_t* server);

/**
 * @brief Enable TLS for the transport server, set the TLS related stuff.
 *
 * @param[out] server      The transport server instance.
 * @param[in]  certificate The TLS certificate file path.
 * @param[in]  private_key The TLS private key file path.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int server_enable_tls(
    server_t* server, const char* certificate, const char* private_key
);

/**
 * @brief Accept new connection from the transport server.
 *
 * @param[in]  server     The TCP server instance.
 * @param[out] connection The TCP connection instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int server_accept(server_t* server, connection_t* connection);

/**
 * @brief Close the transport server, after this the server will no longer
 * accept any connections.
 *
 * @param[out] server The transport server instance.
 */
void server_close(server_t* server);

/**
 * @brief Clean all related stuff from the transport server instance.
 *
 * @param[out] server The transport server instance.
 */
void server_cleanup(server_t* server);

/**
 * @brief Initialize the transport connection.
 *
 * The server instance will not be freed during the cleanup process.
 *
 * @param[out] connection The transport connection instance.
 * @param[in]  server     The transport server instance.
 */
void connection_init(connection_t* connection, server_t* server);

/**
 * @brief Setup the transport connection, set the TCP and TLS related stuff.
 *
 * @param[out] connection      The transport connection instance.
 * @param[in]  receive_timeout Timeout for receiving data in seconds.
 * @param[in]  receive_buffer  Kernel buffer receiving data in bytes.
 * @param[in]  send_timeout    Timeout for sending data in seconds.
 * @param[in]  send_buffer     Kernel buffer sending data in bytes.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int connection_setup(
    connection_t* connection,
    int receive_timeout,
    int receive_buffer,
    int send_timeout,
    int send_buffer
);

/**
 * @brief Establish TLS connection.
 *
 * This has no effect on tranport connection coming from non-TLS enabled
 * transport server.
 *
 * @param[out] connection The transport connection instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int connection_establish_tls(connection_t* connection);

/**
 * @brief Get the error code from the transport connection.
 *
 * @param[in] connection The transport connection instance.
 *
 * @return The error code from the transport connection.
 */
int connection_get_error(connection_t* connection);

/**
 * @brief Receive data from the transport connection.
 *
 * @param[in]  connection The transport connection instance.
 * @param[out] buffer     The buffer to contain the data.
 * @param[in]  size       The size of the buffer.
 * @param[out] received   The size of the received data.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int connection_receive(
    connection_t* connection, char* buffer, size_t size, size_t* received
);

/**
 * @brief Send data into the transport connection.
 *
 * @param[in]  connection The transport connection instance.
 * @param[in]  buffer     The buffer containing the data.
 * @param[in]  size       The size of the buffer.
 * @param[out] sent       The portion of the data that has been sent.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int connection_send(
    connection_t* connection, char* buffer, size_t size, size_t* sent
);

/**
 * @brief Send file into the transport connection.
 *
 * The buffer is just to contain the necessary data for sending the file.
 * Ignore the content of the buffer after this function call.
 *
 * @param[in]  connection  The transport connection instance.
 * @param[in]  file_fd     The file descriptor.
 * @param[in]  file_size   The size of the file.
 * @param[out] buffer      The buffer for the operation.
 * @param[in]  buffer_size The size of the buffer.
 * @param[out] sent        The portion of the file that has been sent.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int connection_sendfile(
    connection_t* connection,
    int file_fd,
    off_t file_size,
    char* buffer,
    size_t buffer_size,
    off_t* sent
);

/**
 * @brief Close the transport connection.
 *
 * The buffer is just to contain the unnecessary data for closing the
 * connection. Ignore the content of the buffer after this function call.
 *
 * @param[in]  connection The transport connection instance.
 * @param[out] buffer     The buffer for the operation.
 * @param[in]  size       The size of the buffer.
 */
void connection_close(connection_t* connection, char* buffer, size_t size);

/**
 * @brief Clean all related stuff from the transport connection instance.
 *
 * @param[out] connection The transport connection instance.
 */
void connection_cleanup(connection_t* connection);

#endif
