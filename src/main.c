#include <errno.h>
#include <fcntl.h>
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
#include "worker.h"

/**
 * @brief Listener worker context data.
 */
typedef struct listen_context {
    tcp_server_t *server;
    int cycle;
} listen_context_t;

/**
 * @brief Process worker context data.
 */
typedef struct process_context {
    http_message_t message;
    char buffer[WORKER_BUFFER_SIZE];
    size_t buffer_size, bytes_to_send;
    ssize_t received_size;

    char file_path[PATH_BUFFER_SIZE];
    off_t file_size;
    int file_fd;
} process_context_t;

/**
 * @brief Listener worker global instance.
 */
worker_t listen_worker = {0};

/**
 * @brief Process workers global instance.
 */
worker_t process_workers[WORKER_COUNT] = {0};

/**
 * @brief Global epoll buffer for the process workers.
 */
struct epoll_event process_watchlist_buffers[WORKER_COUNT][MAX_CONNECTION] = {0};

/**
 * @brief Get the actual file size.
 *
 * @param[in] file_fd The file descriptor.
 *
 * @return The size of the file.
 */
off_t get_file_size(int file_fd) {
    struct stat st;
    if (fstat(file_fd, &st) < 0) {
        LOG_ERROR("failed to stat file: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return st.st_size;
}

/**
 * @brief The epoll event handler for the listener worker.
 *
 * @param[in] listen_worker The listener worker instance.
 * @param[in] events        The epoll events.
 * @param[in] data          The data of the event.
 */
void listen_worker_event_handler(worker_t *listen_worker, uint32_t events, void *data) {
    listen_context_t *context = (listen_context_t *)listen_worker->context;
    tcp_server_t *server = (tcp_server_t *)data;
    tcp_connection_t *new_connection;

    for (;;) {
        if ((events & (EPOLLIN | EPOLLPRI)) == 0) {
            continue;
        }

        new_connection = malloc(sizeof(tcp_connection_t));
        if (tcp_server_accept(server, new_connection) < 0) {
            free(new_connection);
            return;
        }

        for (;; context->cycle = (context->cycle + 1) % WORKER_COUNT) {
            if (process_workers[context->cycle].watchlist_count >= process_workers[context->cycle].watchlist_size) {
                continue;
            }

            if (worker_send(&process_workers[context->cycle], (void *)new_connection, sizeof(tcp_connection_t)) < 0) {
                continue;
            }

            break;
        }
    }
}

/**
 * @brief The thread stop handler for the listener worker.
 *
 * @param[in] listen_worker The listener worker instance.
 * @param[in] signum        The signal number.
 */
void listen_worker_stop_handler(worker_t *listen_worker, int signum) {
    listen_context_t *context = (listen_context_t *)listen_worker->context;

    if (signum != SIGINT) {
        return;
    }

    tcp_server_close(context->server);
}

/**
 * @brief The new data handler for the process worker.
 *
 * @param[in] process_worker The process worker instance.
 * @param[in] data           The incoming new data.
 * @param[in] size           The size of the data.
 */
void process_worker_data_handler(worker_t *process_worker, void *data, size_t size) {
    if (size != sizeof(tcp_connection_t)) {
        return;
    }

    tcp_connection_t *new_connection = (tcp_connection_t *)data;
    if (worker_add_watchlist(process_worker, EPOLLIN | EPOLLET, new_connection->socket, (void *)new_connection) < 0) {
        return;
    }
}

/**
 * @brief The epoll event handler for the process worker.
 *
 * @param[in] process_worker The listener worker instance.
 * @param[in] events         The epoll events.
 * @param[in] data           The data of the event.
 */
void process_worker_event_handler(worker_t *process_worker, uint32_t events, void *data) {
    process_context_t *context = (process_context_t *)process_worker->context;
    tcp_connection_t *connection = (tcp_connection_t *)data;

    if (events & (EPOLLERR)) {
        int conn_error = tcp_connection_get_error(connection);
        LOG_ERROR("connection error: %s (%d)\n", strerror(conn_error), conn_error);
    }
    if (events & (EPOLLIN | EPOLLPRI)) {
        context->received_size = tcp_connection_receive(connection, context->buffer, context->buffer_size, 0);
        if (context->received_size < 0) {
            context->bytes_to_send = (size_t)http_message_error(context->buffer, context->buffer_size);
            if (tcp_connection_send(connection, context->buffer, context->bytes_to_send, 0) < 0) {
                worker_remove_watchlist(process_worker, connection->socket);
                tcp_connection_close(connection, context->buffer, context->buffer_size);
                free(connection);
            }
            return;
        }
        if (context->received_size == 0) {
            worker_remove_watchlist(process_worker, connection->socket);
            tcp_connection_close(connection, context->buffer, context->buffer_size);
            free(connection);
            return;
        }

        if (http_parse_request(&context->message, context->buffer, context->received_size) < 0) {
            context->bytes_to_send = (size_t)http_message_error(context->buffer, context->buffer_size);
            if (tcp_connection_send(connection, context->buffer, context->bytes_to_send, 0) < 0) {
                worker_remove_watchlist(process_worker, connection->socket);
                tcp_connection_close(connection, context->buffer, context->buffer_size);
                free(connection);
            }
            return;
        }

        if (strncmp(context->message.method, "GET", 3) != 0) {
            context->bytes_to_send = (size_t)http_message_not_allowed(context->buffer, context->buffer_size);
            if (tcp_connection_send(connection, context->buffer, context->bytes_to_send, 0) < 0) {
                worker_remove_watchlist(process_worker, connection->socket);
                tcp_connection_close(connection, context->buffer, context->buffer_size);
                free(connection);
            }
            return;
        }

        if (http_resolve_file_path(&context->message, context->file_path, PATH_BUFFER_SIZE) < 0) {
            context->bytes_to_send = (size_t)http_message_not_found(context->buffer, context->buffer_size);
            if (tcp_connection_send(connection, context->buffer, context->bytes_to_send, 0) < 0) {
                worker_remove_watchlist(process_worker, connection->socket);
                tcp_connection_close(connection, context->buffer, context->buffer_size);
                free(connection);
            }
            return;
        }

        if ((context->file_fd = open(context->file_path, O_RDONLY)) < 0) {
            LOG_ERROR("failed to open file: %s (%d)\n", strerror(errno), errno);
            context->bytes_to_send = (size_t)http_message_not_found(context->buffer, context->buffer_size);
            if (tcp_connection_send(connection, context->buffer, context->bytes_to_send, 0) < 0) {
                worker_remove_watchlist(process_worker, connection->socket);
                tcp_connection_close(connection, context->buffer, context->buffer_size);
                free(connection);
            }
            return;
        }

        context->file_size = get_file_size(context->file_fd);
        context->bytes_to_send =
            http_header_file(context->buffer, context->buffer_size, context->file_path, context->file_size);
        if (tcp_connection_send(connection, context->buffer, context->bytes_to_send, 0) < 0) {
            worker_remove_watchlist(process_worker, connection->socket);
            tcp_connection_close(connection, context->buffer, context->buffer_size);
            free(connection);

            if (close(context->file_fd) < 0) {
                LOG_ERROR("failed to close the file: %s (%d)\n", strerror(errno), errno);
            }
            return;
        }

        if (tcp_connection_sendfile(connection, context->file_fd, context->file_size) < 0) {
            worker_remove_watchlist(process_worker, connection->socket);
            tcp_connection_close(connection, context->buffer, context->buffer_size);
            free(connection);

            if (close(context->file_fd) < 0) {
                LOG_ERROR("failed to close the file: %s (%d)\n", strerror(errno), errno);
            }
            return;
        }

        if (close(context->file_fd) < 0) {
            LOG_ERROR("failed to close the file: %s (%d)\n", strerror(errno), errno);
        }
    }
    if (events & (EPOLLHUP | EPOLLRDHUP)) {
        tcp_connection_close(connection, context->buffer, context->buffer_size);
        worker_remove_watchlist(process_worker, connection->socket);
        free(connection);
        return;
    }
}

/**
 * @brief The signal handler function.
 *
 * @param[in] signum The signal number.
 */
void signal_handler(int signum) {
    if (signum == SIGINT) {
        for (int i = 0; i < WORKER_COUNT; i++) {
            worker_stop(&process_workers[i], signum);
        }
        worker_stop(&listen_worker, signum);
    }
}

/**
 * @brief The main function.
 *
 * @return The result code of the program.
 */
int main(void) {
    process_context_t process_contexts[WORKER_COUNT] = {0};

    for (int i = 0; i < WORKER_COUNT; i++) {
        process_contexts[i].buffer_size = WORKER_BUFFER_SIZE;

        process_workers[i].watchlist = process_watchlist_buffers[i];
        process_workers[i].watchlist_size = MAX_CONNECTION;
        process_workers[i].context = (void *)&process_contexts[i];

        process_workers[i].data_handler = process_worker_data_handler;
        process_workers[i].event_handler = process_worker_event_handler;
        process_workers[i].stop_handler = NULL;
        if (worker_init(&process_workers[i]) < 0 || worker_run(&process_workers[i]) < 0) {
            return errno;
        }
    }

    tcp_server_t server = (tcp_server_t){
        .max_connection = MAX_CONNECTION,
        .address = (struct sockaddr_in){
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = htons(LISTEN_PORT),
        },
    };
    if (tcp_server_init(&server) < 0) {
        return errno;
    }

    listen_context_t listen_context = {.server = &server, .cycle = 0};
    struct epoll_event listen_watchlist_buffer[2] = {0};

    listen_worker.watchlist = listen_watchlist_buffer;
    listen_worker.watchlist_size = 2;
    listen_worker.context = (void *)&listen_context;

    listen_worker.data_handler = NULL;
    listen_worker.event_handler = listen_worker_event_handler;
    listen_worker.stop_handler = listen_worker_stop_handler;
    if (worker_init(&listen_worker) < 0) {
        return errno;
    }
    if (worker_add_watchlist(&listen_worker, EPOLLIN | EPOLLET, server.socket, (void *)&server) < 0) {
        return errno;
    }
    if (worker_run(&listen_worker) < 0) {
        return errno;
    }

    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    if (worker_wait(&listen_worker) < 0) {
        return errno;
    }
    for (int i = 0; i < WORKER_COUNT; i++) {
        if (worker_wait(&process_workers[i]) < 0) {
            return errno;
        }
    }

    return 0;
}
