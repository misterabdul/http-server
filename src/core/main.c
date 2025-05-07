#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "http.h"
#include "log.h"
#include "tcp.h"
#include "thread.h"

/**
 * @brief Shared mutex for each listener.
 */
typedef struct listener_mutex {
    pthread_mutex_t lock;
    int cycle, worker_count;
} listener_mutex_t;

/**
 * @brief Listener context data.
 */
typedef struct listener_context {
    server_config_t *config;
    listener_mutex_t *mutex;
    tcp_server_t *server;
} listener_context_t;

/**
 * @brief Worker context data.
 */
typedef struct worker_context {
    size_t buffer_size, file_path_size;
    char *buffer, *file_path;
    http_message_t *message;
} worker_context_t;

/**
 * @brief Connection context data.
 */
typedef struct connection_context {
    tcp_server_t *server;
    server_config_t *config;
} connection_context_t;

/**
 * @brief Global main configuration.
 */
config_t main_config;

/**
 * @brief Global mutex for each listener workers.
 */
listener_mutex_t listener_mutex;

/**
 * @brief Global listener thread instances.
 */
thread_t **listeners;

/**
 * @brief Global worker thread instances.
 */
thread_t **workers;

/**
 * @brief Allocate memory and initialize listener context data.
 *
 * @param[in] server_config Server configuration.
 *
 * @return Listener context data.
 *
 * @note Contains pointer arithmetic, allocate one big block of memory for everything.
 */
listener_context_t *listener_context_create(server_config_t *server_config) {
    listener_context_t *_context;
    tcp_server_t *_server;

    if (server_config->family == AF_INET) {
        /* IPv4 configuration*/

        _context = malloc(sizeof(listener_context_t) + sizeof(tcp_server_t) + sizeof(struct sockaddr_in));

        _server = (tcp_server_t *)((char *)_context + sizeof(listener_context_t));
        struct sockaddr_in *_address = (struct sockaddr_in *)((char *)_server + sizeof(tcp_server_t));

        *_address = (struct sockaddr_in){.sin_family = AF_INET, .sin_port = htons(server_config->port)};
        inet_ntop(AF_INET, &(_address->sin_addr), server_config->address, INET_ADDRSTRLEN);

        *_server = (tcp_server_t){
            .max_connection = main_config.max_connection,
            .address_length = sizeof(struct sockaddr_in),
            .address = (struct sockaddr *)_address,
        };
    } else {
        /* IPv6 configuration*/

        _context = malloc(sizeof(listener_context_t) + sizeof(tcp_server_t) + sizeof(struct sockaddr_in6));

        _server = (tcp_server_t *)((char *)_context + sizeof(listener_context_t));
        struct sockaddr_in6 *_address6 = (struct sockaddr_in6 *)((char *)_server + sizeof(tcp_server_t));

        *_address6 = (struct sockaddr_in6){.sin6_family = AF_INET6, .sin6_port = htons(server_config->port)};
        inet_ntop(AF_INET, &(_address6->sin6_addr), server_config->address, INET_ADDRSTRLEN);

        *_server = (tcp_server_t){
            .max_connection = main_config.max_connection,
            .address_length = sizeof(struct sockaddr_in6),
            .address = (struct sockaddr *)_address6,
        };
    }

    *_context = (listener_context_t){
        .mutex = &listener_mutex,
        .config = server_config,
        .server = _server,
    };

    return _context;
}

/**
 * @brief Allocate memory and initialize worker context data.
 *
 * @return Worker context data.
 *
 * @note Contains pointer arithmetic, allocate one big block of memory for everything.
 */
worker_context_t *worker_context_create(void) {
    worker_context_t *_context =
        malloc(sizeof(worker_context_t) + WORKER_BUFFER_SIZE + PATH_BUFFER_SIZE + sizeof(http_message_t));
    char *_buffer = (char *)_context + sizeof(worker_context_t), *_path_buffer = _buffer + WORKER_BUFFER_SIZE;
    http_message_t *_message = (http_message_t *)(_path_buffer + PATH_BUFFER_SIZE);

    *_context = (worker_context_t){
        .file_path_size = PATH_BUFFER_SIZE,
        .buffer_size = WORKER_BUFFER_SIZE,
        .file_path = _path_buffer,
        .message = _message,
        .buffer = _buffer,
    };

    return _context;
}

/**
 * @brief Create a new connection instance.
 *
 * @param[in] server The TCP server.
 * @param[in] config The server config.
 *
 * @return A new connection instance.
 *
 * @note Contains pointer arithmetic, allocate one big block of memory for everything.
 */
tcp_connection_t *connection_create(tcp_server_t *server, server_config_t *config) {
    connection_context_t *_context;
    tcp_connection_t *_connection;

    if (server->address->sa_family == AF_INET) {
        /* IPv4 connection */
        _connection = malloc(sizeof(tcp_connection_t) + sizeof(struct sockaddr_in) + sizeof(connection_context_t));
        struct sockaddr_in *_address = (struct sockaddr_in *)((char *)_connection + sizeof(tcp_connection_t));
        _context = (connection_context_t *)((char *)_address + sizeof(struct sockaddr_in));

        *_context = (connection_context_t){.server = server, .config = config};
        *_connection = (tcp_connection_t){
            .address_length = sizeof(struct sockaddr_in),
            .address = (struct sockaddr *)_address,
            .context = _context,
        };
    } else {
        /* IPv6 connection */
        _connection = malloc(sizeof(tcp_connection_t) + sizeof(struct sockaddr_in6) + sizeof(connection_context_t));
        struct sockaddr_in6 *_address6 = (struct sockaddr_in6 *)((char *)_connection + sizeof(tcp_connection_t));
        _context = (connection_context_t *)((char *)_address6 + sizeof(struct sockaddr_in6));

        *_context = (connection_context_t){.server = server, .config = config};
        *_connection = (tcp_connection_t){
            .address_length = sizeof(struct sockaddr_in6),
            .address = (struct sockaddr *)_address6,
            .context = _context,
        };
    }

    return _connection;
}

/**
 * @brief Get the actual file size.
 *
 * @param[in] file_fd The file descriptor.
 *
 * @return The size of the file.
 */
off_t file_get_size(int file_fd) {
    struct stat _st;
    if (fstat(file_fd, &_st) < 0) {
        LOG_ERROR("failed to stat file: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return _st.st_size;
}

/**
 * @brief The poll handler for the listener.
 *
 * @param[in] listener The listener thread instance.
 * @param[in] events   The poll events.
 * @param[in] data     The data of the event.
 */
void listener_poll_handler(thread_t *listener, int events, void *data) {
    listener_context_t *_context = (listener_context_t *)listener->context;
    tcp_server_t *_server = (tcp_server_t *)data;
    tcp_connection_t *_connection;

    for (int cycle;;) {
        if ((events & (POLLIN | POLLPRI)) == 0) {
            continue;
        }
        _connection = connection_create(_server, _context->config);
        if (tcp_server_accept(_server, _connection) < 0) {
            free(_connection);
            return;
        }

        pthread_mutex_lock(&_context->mutex->lock);
        for (cycle = _context->mutex->cycle;; cycle = (cycle + 1) % _context->mutex->worker_count) {
            if (workers[cycle]->watchlist_count >= workers[cycle]->watchlist_size) {
                continue;
            }

            if (thread_send(workers[cycle], (void *)_connection, sizeof(tcp_connection_t)) < 0) {
                continue;
            }

            break;
        }
        _context->mutex->cycle = (cycle + 1) % _context->mutex->worker_count;
        pthread_mutex_unlock(&_context->mutex->lock);
    }
}

/**
 * @brief The stop handler for the listener.
 *
 * @param[in] listener The listener thread instance.
 * @param[in] signum   The signal number.
 */
void listener_stop_handler(thread_t *listener, int signum) {
    listener_context_t *_context = (listener_context_t *)listener->context;

    if (signum != SIGINT) {
        return;
    }

    tcp_server_close(_context->server);
}

/**
 * @brief Initialize the listener instance.
 *
 * @param[out] listener The listener thread instance.
 * @param[in]  config   The server configuration.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int listener_init(thread_t *listener, server_config_t *config) {
    listener_context_t *_context = listener_context_create(config);

    if (tcp_server_init(_context->server) < 0) {
        return -1;
    }

    listener->data_handler = NULL;
    listener->poll_handler = listener_poll_handler;
    listener->stop_handler = listener_stop_handler;
    listener->context = _context;

    if (thread_init(listener) < 0) {
        return -1;
    }
    if (thread_add_watchlist(listener, POLLIN | POLLET, _context->server->socket, (void *)_context->server) < 0) {
        return -1;
    }
    if (thread_run(listener) < 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief Clean all related memory from the listener instance.
 *
 * @param[out] listener The listener thread instance.
 */
void listener_clean(thread_t *listener) {
    free(listener->context);
}

/**
 * @brief The new data handler for the worker.
 *
 * @param[in] worker The worker thread instance.
 * @param[in] data   The incoming new data.
 * @param[in] size   The size of the data.
 */
void worker_data_handler(thread_t *worker, void *data, size_t size) {
    if (size != sizeof(tcp_connection_t)) {
        return;
    }

    tcp_connection_t *_connection = (tcp_connection_t *)data;
    if (thread_add_watchlist(worker, POLLIN | POLLET, _connection->socket, (void *)_connection) < 0) {
        return;
    }
}

/**
 * @brief The poll handler for the worker.
 *
 * @param[in] worker The worker thread instance.
 * @param[in] events The poll events.
 * @param[in] data   The data of the event.
 */
void worker_poll_handler(thread_t *worker, int events, void *data) {
    worker_context_t *_context = (worker_context_t *)worker->context;
    tcp_connection_t *_connection = (tcp_connection_t *)data;
    connection_context_t *_connection_context = (connection_context_t *)_connection->context;

    size_t _buffer_size = _context->buffer_size, _file_path_size = _context->file_path_size, _bytes_to_send;
    char *_buffer = _context->buffer, *_file_path = _context->file_path,
         *_root_path = _connection_context->config->root_path;
    http_message_t *_message = _context->message;
    int _error_code, _file_fd;
    ssize_t _received_size;
    off_t _file_size;

    /* client connection has issue */
    if (events & (POLLERR)) {
        _error_code = tcp_connection_get_error(_connection);
        LOG_ERROR("connection error: %s (%d)\n", strerror(_error_code), _error_code);
    }

    /* client send a new http request */
    if (events & (POLLIN | POLLPRI)) {
        /* get the request string */
        _received_size = tcp_connection_receive(_connection, _buffer, _buffer_size, 0);
        if (_received_size < 0) {
            _bytes_to_send = (size_t)http_message_error(_buffer, _buffer_size);
            if (tcp_connection_send(_connection, _buffer, _bytes_to_send, 0) < 0) {
                thread_remove_watchlist(worker, _connection->socket);
                tcp_connection_close(_connection, _buffer, _buffer_size);
                free(_connection);
            }
            return;
        }

        if (_received_size == 0) {
            thread_remove_watchlist(worker, _connection->socket);
            tcp_connection_close(_connection, _buffer, _buffer_size);
            free(_connection);
            return;
        }

        /* parse request as http message */
        if (http_parse_request(_message, _buffer, _received_size) < 0) {
            _bytes_to_send = (size_t)http_message_error(_buffer, _buffer_size);
            if (tcp_connection_send(_connection, _buffer, _bytes_to_send, 0) < 0) {
                thread_remove_watchlist(worker, _connection->socket);
                tcp_connection_close(_connection, _buffer, _buffer_size);
                free(_connection);
            }
            return;
        }

        /* could only handle http get method */
        if (strncmp(_message->method, "GET", 3) != 0) {
            _bytes_to_send = (size_t)http_message_not_allowed(_buffer, _buffer_size);
            if (tcp_connection_send(_connection, _buffer, _bytes_to_send, 0) < 0) {
                thread_remove_watchlist(worker, _connection->socket);
                tcp_connection_close(_connection, _buffer, _buffer_size);
                free(_connection);
            }
            return;
        }

        /* try looking for the requested resource */
        if (http_resolve_file_path(_message, _root_path, _file_path, _file_path_size) < 0) {
            _bytes_to_send = (size_t)http_message_not_found(_buffer, _buffer_size);
            if (tcp_connection_send(_connection, _buffer, _bytes_to_send, 0) < 0) {
                thread_remove_watchlist(worker, _connection->socket);
                tcp_connection_close(_connection, _buffer, _buffer_size);
                free(_connection);
            }
            return;
        }

        /* try to open the resource */
        if ((_file_fd = open(_context->file_path, O_RDONLY)) < 0) {
            LOG_ERROR("failed to open file: %s (%d)\n", strerror(errno), errno);
            _bytes_to_send = (size_t)http_message_not_found(_buffer, _buffer_size);
            if (tcp_connection_send(_connection, _buffer, _bytes_to_send, 0) < 0) {
                thread_remove_watchlist(worker, _connection->socket);
                tcp_connection_close(_connection, _buffer, _buffer_size);
                free(_connection);
            }
            return;
        }

        /* send the http header first */
        _file_size = file_get_size(_file_fd);
        _bytes_to_send = http_header_file(_buffer, _buffer_size, _file_path, _file_size);
        if (tcp_connection_send(_connection, _buffer, _bytes_to_send, 0) < 0) {
            thread_remove_watchlist(worker, _connection->socket);
            tcp_connection_close(_connection, _buffer, _buffer_size);
            free(_connection);

            if (close(_file_fd) < 0) {
                LOG_ERROR("failed to close the file: %s (%d)\n", strerror(errno), errno);
            }
            return;
        }

        /* send the actual resource */
        if (tcp_connection_sendfile(_connection, _file_fd, _file_size) < 0) {
            thread_remove_watchlist(worker, _connection->socket);
            tcp_connection_close(_connection, _buffer, _buffer_size);
            free(_connection);

            if (close(_file_fd) < 0) {
                LOG_ERROR("failed to close the file: %s (%d)\n", strerror(errno), errno);
            }
            return;
        }

        /* close the resource */
        if (close(_file_fd) < 0) {
            LOG_ERROR("failed to close the file: %s (%d)\n", strerror(errno), errno);
        }
    }

    /* client has close the connection */
    if (events & (POLLHUP | POLLRDHUP)) {
        tcp_connection_close(_connection, _buffer, _buffer_size);
        thread_remove_watchlist(worker, _connection->socket);
        free(_connection);
        return;
    }
}

/**
 * @brief Initialize the worker instance.
 *
 * @param[out] worker The worker thread instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int worker_init(thread_t *worker) {
    worker_context_t *_context = worker_context_create();

    worker->data_handler = worker_data_handler;
    worker->poll_handler = worker_poll_handler;
    worker->stop_handler = NULL;
    worker->context = _context;

    if (thread_init(worker) < 0) {
        return -1;
    }
    if (thread_run(worker) < 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief Clean all related memory from the worker instance.
 *
 * @param[out] worker The worker thread instance.
 */
void worker_clean(thread_t *worker) {
    free(worker->context);
}

/**
 * @brief The signal handler function.
 *
 * @param[in] signum The signal number.
 */
void signal_handler(int signum) {
    if (signum == SIGINT) {
        for (int i = 0; i < main_config.listener_count; i++) {
            thread_stop(listeners[i], signum);
        }
        for (int i = 0; i < main_config.worker_count; i++) {
            thread_stop(workers[i], signum);
        }
    }
}

/**
 * @brief The main function.
 *
 * @return The result code of the program.
 */
int main(void) {
    get_config(&main_config);

    listener_mutex = (listener_mutex_t){.cycle = 0, .worker_count = main_config.worker_count};
    if (pthread_mutex_init(&listener_mutex.lock, NULL) < 0) {
        return errno;
    }

    workers = malloc(main_config.worker_count * sizeof(thread_t *));
    for (int i = 0; i < main_config.worker_count; i++) {
        if ((workers[i] = thread_new(main_config.max_connection)) == NULL) {
            return errno;
        }
        if (worker_init(workers[i]) < 0) {
            return errno;
        }
    }

    listeners = malloc(main_config.listener_count * sizeof(thread_t *));
    for (int i = 0; i < main_config.listener_count; i++) {
        listeners[i] = thread_new(0);
        if (listener_init(listeners[i], &main_config.servers[i]) < 0) {
            return errno;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    for (int i = 0; i < main_config.listener_count; i++) {
        if (thread_wait(listeners[i]) < 0) {
            return errno;
        }
        listener_clean(listeners[i]);
        free(listeners[i]);
    }
    free(listeners);

    for (int i = 0; i < main_config.worker_count; i++) {
        if (thread_wait(workers[i]) < 0) {
            return errno;
        }
        worker_clean(workers[i]);
        free(workers[i]);
    }
    free(workers);

    if (pthread_mutex_destroy(&listener_mutex.lock) < 0) {
        return errno;
    }

    return 0;
}
