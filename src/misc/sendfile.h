#ifndef lib_sendfile_h
#define lib_sendfile_h

#include <openssl/ssl.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * @brief Let the kernel do the sendfile.
 *
 * @param[in]  socket    The target socket.
 * @param[in]  file_fd   The file descriptor.
 * @param[in]  file_size The size of the file.
 * @param[out] sent      The portion of the file that has been sent.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int kernel_sendfile(int socket, int file_fd, off_t file_size, off_t* sent);

/**
 * @brief Let the kernel do the sendfile.
 *
 * @param[in]  ssl       The target ssl connection.
 * @param[in]  file_fd   The file descriptor.
 * @param[in]  file_size The size of the file.
 * @param[out] sent      The portion of the file that has been sent.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int kernel_sendfile_secure(
    struct ssl_st* ssl, int file_fd, off_t file_size, off_t* sent
);

/**
 * @brief Send the file to the socket using the given buffer.
 * 
 * The buffer is just to contain the necessary data for sending the file.
 * Ignore the content of the buffer after this function call.
 *
 * @param[in]  socket      The target socket.
 * @param[in]  file_fd     The file to be sent.
 * @param[in]  file_size   The size of the file.
 * @param[out] buffer      The buffer for the operation.
 * @param[in]  buffer_size The size of the buffer.
 * @param[out] sent        The portion of the file that has been sent.
 *
 * @return Size of the sent data or -1 for errors.
 */
int buffered_sendfile(
    int socket,
    int file_fd,
    off_t file_size,
    void* buffer,
    size_t buffer_size,
    off_t* sent
);

/**
 * @brief Send the file to the ssl connection using the given buffer.
 * 
 * The buffer is just to contain the necessary data for sending the file.
 * Ignore the content of the buffer after this function call.
 *
 * @param[in]  ssl         The target ssl connection.
 * @param[in]  file_fd     The file to be sent.
 * @param[in]  file_size   The size of the file.
 * @param[out] buffer      The buffer for the operation.
 * @param[in]  buffer_size The size of the buffer.
 * @param[out] sent        The portion of the file that has been sent.
 */
int buffered_sendfile_secure(
    struct ssl_st* ssl,
    int file_fd,
    off_t file_size,
    void* buffer,
    size_t buffer_size,
    off_t* sent
);

#endif
