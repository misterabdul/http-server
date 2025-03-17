#include "worker.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "connection.h"
#include "log.h"
#include "pipe.h"

int worker_init(worker_t *worker) {
    struct epoll_event event;

    if (pipe(worker->pipe_fds)) {
        LOG_ERROR("failed creating pipes: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    worker->epoll_fd = epoll_create1(0);
    if (worker->epoll_fd < 0) {
        LOG_ERROR("failed creating epoll fd: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    event.events = EPOLLIN;
    event.data.fd = worker->pipe_fds[0];
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, worker->pipe_fds[0], &event) < 0) {
        LOG_ERROR("failed adding listen socket to epoll: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    worker->buffer_size = REQUEST_BUFFER_SIZE;
    worker->event_size = MAX_CONNECTION;
    worker->event_count = 1;

    return 0;
}

int worker_poll_handle_pipe_message(worker_t *worker) {
    pipe_message_t pipe_message;
    connection_t *connection;
    struct epoll_event event;

    if (read(worker->pipe_fds[0], (void *)&pipe_message, sizeof(pipe_message_t)) < 0) {
        LOG_ERROR("failed to read from pipe: %s (%d)\n", strerror(errno), errno);
        return 0;
    }

    if (pipe_message.type == PIPE_SIGNAL) {
        return -1;
    }

    if (pipe_message.type == PIPE_CONNECTION && pipe_message.data_size == sizeof(connection_t)) {
        connection = pipe_message.data;

        event.events = EPOLLIN | EPOLLET;
        event.data.ptr = (void *)connection;
        if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, connection->socket, &event) < 0) {
            LOG_ERROR("failed to add new connection to epoll: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
        worker->event_count++;
    }

    return 0;
}

void worker_poll_handle_connection_stream(worker_t *worker, connection_t *connection) {
    if (connection_receive(connection, worker->buffer, worker->buffer_size, 0) < 0) {
        return;
    }
    if (connection->is_done) {
        return;
    }
    connection_respond(connection, worker->buffer, worker->buffer_size, MSG_NOSIGNAL);
}

void worker_poll_handle_connection_close(worker_t *worker, connection_t **connection) {
    if (*connection == NULL) {
        return;
    }

    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, (*connection)->socket, NULL) < 0) {
        LOG_ERROR("failed to remove connection from epoll: %s (%d)\n", strerror(errno), errno);
    }
    connection_close(connection, worker->buffer, REQUEST_BUFFER_SIZE);
    worker->event_count--;
}

void *worker_runner(void *data) {
    worker_t *worker = (worker_t *)data;
    connection_t *connection;

    for (int epoll_nums, run = 1; run;) {
        epoll_nums = epoll_wait(worker->epoll_fd, worker->events, worker->event_count, EPOLL_TIMEOUT);
        if (epoll_nums < 0) {
            LOG_ERROR("failed to get epoll data: %s (%d)", strerror(errno), errno);
            return NULL;
        }

        for (int i = 0; i < epoll_nums; i++) {
            if (worker->events[i].data.fd == worker->pipe_fds[0]) {
                if (worker_poll_handle_pipe_message(worker) < 0) {
                    run = 0;
                    break;
                }
                continue;
            }

            connection = (connection_t *)worker->events[i].data.ptr;
            if (worker->events[i].events & (EPOLLERR)) {
                int conn_error = connection_error(connection);
                LOG_ERROR("connection error: %s (%d)\n", strerror(conn_error), conn_error);
            }
            if (worker->events[i].events & (EPOLLIN | EPOLLPRI)) {
                worker_poll_handle_connection_stream(worker, connection);
            }
            if (worker->events[i].events & (EPOLLHUP | EPOLLRDHUP)) {
                worker_poll_handle_connection_close(worker, &connection);
            }
            if (connection && connection->is_done) {
                worker_poll_handle_connection_close(worker, &connection);
            }
        }
    }

    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, worker->pipe_fds[0], NULL) < 0) {
        LOG_ERROR("failed to remove pipe fd (in) from epoll: %s (%d)\n", strerror(errno), errno);
    }
    if (close(worker->epoll_fd) < 0) {
        LOG_ERROR("failed to close epoll fd: %s (%d)\n", strerror(errno), errno);
    }
    if (close(worker->pipe_fds[0]) < 0) {
        LOG_ERROR("failed to close pipe fd (in): %s (%d)\n", strerror(errno), errno);
    }
    if (close(worker->pipe_fds[1]) < 0) {
        LOG_ERROR("failed to close pipe fd (out): %s (%d)\n", strerror(errno), errno);
    }

    return NULL;
}
