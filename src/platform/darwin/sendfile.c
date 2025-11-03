#include <misc/sendfile.h>

#include <errno.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <misc/logger.h>

#define KTLS_CHUNK 134217728 /* 128 MB */

#define ERROR_BUFFER_SIZE 255

/**
 * @brief Global SSL error code.
 */
static unsigned long g_errno;

/**
 * @brief Global SSL error string buffer.
 */
static char g_error[ERROR_BUFFER_SIZE];

/**
 * @copydoc kernel_sendfile
 */
int kernel_sendfile(
    int socket, int file_fd, off_t file_size, off_t* sent, int flags
) {
    for (ssize_t _result; *sent < file_size;) {
        _result = sendfile(file_fd, socket, *sent, sent, NULL, flags);
        if (_result > 0) {
            continue;
        }
        if (_result == 0) {
            break;
        }
        if (errno != ENOSYS && errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("sendfile: %s (%d)\n", strerror(errno), errno);
        }
        return -1;
    }

    return 0;
}

int kernel_sendfile_secure(
    struct ssl_st* ssl, int file_fd, off_t file_size, off_t* sent, int flags
) {
    /* Offload the the sendfile to the kerne (if possible). */
    BIO* _bio = SSL_get_wbio(ssl);
    if (BIO_get_ktls_send(_bio) == 0) {
        errno = ENOSYS;
        return -1;
    }

    for (ssize_t _result; *sent < file_size;) {
        _result = SSL_sendfile(ssl, file_fd, *sent, KTLS_CHUNK, flags);
        if (_result > 0) {
            *sent += _result;
            continue;
        }
        g_errno = ERR_get_error();
        if (g_errno != SSL_ERROR_NONE) {
            continue;
        }
        if (g_errno == SSL_ERROR_WANT_WRITE) {
            break;
        }
        ERR_error_string(g_errno, g_error);
        LOG_ERROR("SSL_sendfile: %s (%ld)\n", g_error, g_errno);
        return -1;
    }

    return 0;
}

/**
 * @copydoc buffered_sendfile
 */
int buffered_sendfile(
    int socket,
    int file_fd,
    off_t file_size,
    void* buffer,
    size_t buffer_size,
    off_t* sent
) {
    size_t _remaining, _chunk;
    ssize_t _read;
    off_t _seek;

    for (ssize_t _result; *sent < file_size;) {
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
        _result = send(socket, buffer, _read, 0);
        if (_result >= 0) {
            *sent += _result;
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        LOG_ERROR("send: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * @copydoc secure_sendfile_secure
 */
int buffered_sendfile_secure(
    struct ssl_st* ssl,
    int file_fd,
    off_t file_size,
    void* buffer,
    size_t buffer_size,
    off_t* sent
) {
    size_t _remaining, _chunk;
    ssize_t _read;
    off_t _seek;

    for (ssize_t _result; *sent < file_size;) {
        /* Set the file offset to given offset. */
        _seek = lseek(file_fd, *sent, SEEK_SET);
        if (_seek == -1) {
            LOG_ERROR("lseek: %s (%d)\n", strerror(errno), errno);
            return -1;
        }

        /* Send the content of the buffer. */
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
        _result = SSL_write(ssl, buffer, _read);
        if (_result > 0) {
            *sent += _result;
            continue;
        }
        g_errno = ERR_get_error();
        if (g_errno == SSL_ERROR_NONE) {
            continue;
        }
        if (g_errno == SSL_ERROR_WANT_WRITE) {
            break;
        }
        ERR_error_string(g_errno, g_error);
        LOG_ERROR("SSL_write: %s (%ld)\n", g_error, g_errno);
        return -1;
    }

    return 0;
}
