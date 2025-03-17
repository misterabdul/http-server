#include "listener.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "connection.h"
#include "log.h"
#include "pipe.h"

int listener_socket_create(in_addr_t address, uint16_t port) {
    int listen_socket = socket(AF_INET, SOCK_STREAM, 0), sockopt = 1;
    struct sockaddr_in listen_addr;

    if (listen_socket <= 0) {
        LOG_ERROR("failed to create listen socket: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    if (fcntl(listen_socket, F_SETFL, fcntl(listen_socket, F_GETFL, 0) | O_NONBLOCK) == -1) {
        LOG_ERROR("failed to set the listen socket nonblock: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(int)) < 0) {
        LOG_ERROR("failed to set the listen socket reuse address: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    listen_addr = (struct sockaddr_in){.sin_family = AF_INET, .sin_addr.s_addr = address, .sin_port = htons(port)};
    if (bind(listen_socket, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        LOG_ERROR("failed binding address to socket: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (listen(listen_socket, MAX_CONNECTION) < 0) {
        LOG_ERROR("failed listening to the socket: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return listen_socket;
}

int listener_init(listener_t *listener) {
    struct epoll_event event;

    if (pipe(listener->pipe_fds)) {
        LOG_ERROR("failed creating pipes: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    listener->listen_sock = listener_socket_create(INADDR_ANY, LISTEN_PORT);
    if (listener->listen_sock < 0) {
        LOG_ERROR("failed creating listen socket: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    listener->epoll_fd = epoll_create1(0);
    if (listener->epoll_fd < 0) {
        LOG_ERROR("failed creating epoll fd: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    event.events = EPOLLIN | EPOLLET;
    event.data.fd = listener->listen_sock;
    if (epoll_ctl(listener->epoll_fd, EPOLL_CTL_ADD, listener->listen_sock, &event) < 0) {
        LOG_ERROR("failed adding listen socket to epoll: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    event.events = EPOLLIN;
    event.data.fd = listener->pipe_fds[0];
    if (epoll_ctl(listener->epoll_fd, EPOLL_CTL_ADD, listener->pipe_fds[0], &event) < 0) {
        LOG_ERROR("failed adding listen socket to epoll: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    listener->worker_cycle = 0;

    return 0;
}

int listener_poll_handle_pipe_message(listener_t *listener) {
    pipe_message_t message;

    if (read(listener->pipe_fds[0], (void *)&message, sizeof(pipe_message_t)) < 0) {
        LOG_ERROR("failed to read from pipe: %s (%d)\n", strerror(errno), errno);
        return 0;
    }
    if (message.type == PIPE_SIGNAL) {
        return -1;
    }

    return 0;
}

void listener_poll_handle_new_connections(listener_t *listener) {
    connection_t *new_connection;
    pipe_message_t message;

    for (;;) {
        if (connection_accept(&new_connection, listener->listen_sock) < 0) {
            LOG_ERROR("failed to accept new connection: %s (%d)\n", strerror(errno), errno);
            break;
        }
        if (new_connection == NULL) {
            break;
        }

        message.type = PIPE_CONNECTION;
        message.data = new_connection;
        message.data_size = sizeof(connection_t);

        if (write(listener->worker_fds[listener->worker_cycle], (void *)&message, sizeof(pipe_message_t)) < 0) {
            LOG_ERROR("failed to send new connection socket to worker: %s (%d)\n", strerror(errno), errno);
            free(new_connection);
            break;
        }

        listener->worker_cycle = (listener->worker_cycle + 1) % WORKER_COUNT;
    }
}

void *listener_runner(void *data) {
    listener_t *listener = (listener_t *)data;

    for (int epoll_nums, run = 1; run;) {
        epoll_nums = epoll_wait(listener->epoll_fd, listener->events, 2, EPOLL_TIMEOUT);
        if (epoll_nums < 0) {
            LOG_ERROR("failed to get epoll data: %s (%d)", strerror(errno), errno);
            return NULL;
        }

        for (int i = 0; i < epoll_nums; i++) {
            if (listener->events[i].data.fd == listener->pipe_fds[0]) {
                if (listener_poll_handle_pipe_message(listener) < 0) {
                    run = 0;
                    break;
                }
                continue;
            }

            listener_poll_handle_new_connections(listener);
        }
    }

    if (epoll_ctl(listener->epoll_fd, EPOLL_CTL_DEL, listener->listen_sock, NULL) == -1) {
        LOG_ERROR("failed to remove listen socket from epoll: %s (%d)\n", strerror(errno), errno);
    }
    if (epoll_ctl(listener->epoll_fd, EPOLL_CTL_DEL, listener->pipe_fds[0], NULL) == -1) {
        LOG_ERROR("failed to remove pipe fd (in) from epoll: %s (%d)\n", strerror(errno), errno);
    }
    if (close(listener->epoll_fd) == -1) {
        LOG_ERROR("failed to close epoll fd: %s (%d)\n", strerror(errno), errno);
    }
    if (close(listener->listen_sock) == -1) {
        LOG_ERROR("failed to close listen socket: %s (%d)\n", strerror(errno), errno);
    }
    if (close(listener->pipe_fds[0]) == -1) {
        LOG_ERROR("failed to close pipe fd (in): %s (%d)\n", strerror(errno), errno);
    }
    if (close(listener->pipe_fds[1]) == -1) {
        LOG_ERROR("failed to close pipe fd (out): %s (%d)\n", strerror(errno), errno);
    }

    return NULL;
}
