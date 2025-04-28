#include "worker.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "log.h"

/**
 * @brief Worker's pipe message type.
 */
typedef enum pipe_message_type {
    PIPE_SIGNAL = 0,
    PIPE_DATA = 1,
} pipe_message_type_t;

/**
 * @brief Worker's pipe message data structure.
 */
typedef struct pipe_message {
    pipe_message_type_t type;
    void *data;
    size_t data_size;
} pipe_message_t;

/**
 * @brief Worker thread runner function for pthread routine.
 *
 * @param data User data from the pthread.
 *
 * @return Data for pthread return value.
 */
void *worker_runner(void *data) {
    worker_t *worker = (worker_t *)data;
    pipe_message_t message = {0};
    int signum = 0;

    for (int epoll_nums, run = 1; run;) {
        epoll_nums = epoll_wait(worker->epoll_fd, worker->watchlist, worker->watchlist_size, EPOLL_TIMEOUT);
        if (epoll_nums < 0) {
            LOG_ERROR("failed to get epoll data: %s (%d)\n", strerror(errno), errno);
            return NULL;
        }

        for (int i = 0; i < epoll_nums; i++) {
            if (worker->watchlist[i].data.fd == worker->pipe_fds[0]) {
                if (read(worker->pipe_fds[0], (void *)&message, sizeof(pipe_message_t)) < 0) {
                    LOG_ERROR("failed to read from pipe: %s (%d)\n", strerror(errno), errno);
                    continue;
                }
                if (message.type == PIPE_SIGNAL) {
                    signum = (size_t)(message.data);
                    run = 0;
                    break;
                }

                if (message.type == PIPE_DATA && worker->data_handler) {
                    worker->data_handler(worker, message.data, message.data_size);
                }

                continue;
            }

            if (worker->event_handler) {
                worker->event_handler(worker, worker->watchlist[i].events, worker->watchlist[i].data.ptr);
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

    if (worker->stop_handler) {
        worker->stop_handler(worker, signum);
    }

    return NULL;
}

/**
 * Initialize the worker instance.
 */
int worker_init(worker_t *worker) {
    if (pthread_attr_init(&worker->attr) < 0) {
        LOG_ERROR("failed to initialize pthread attribute: %s (%d)", strerror(errno), errno);
        return -1;
    }

    if ((worker->epoll_fd = epoll_create1(0)) < 0) {
        LOG_ERROR("failed to create epoll: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (pipe(worker->pipe_fds) < 0) {
        LOG_ERROR("failed to create pipes: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    struct epoll_event event = (struct epoll_event){.events = EPOLLIN, .data.fd = worker->pipe_fds[0]};
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, worker->pipe_fds[0], &event) < 0) {
        LOG_ERROR("failed adding pipe in to the epoll watchlist: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Run the worker in a separate thread.
 */
int worker_run(worker_t *worker) {
    if (pthread_create(&worker->id, &worker->attr, worker_runner, (void *)worker) < 0) {
        LOG_ERROR("failed to create pthread: %s (%d)", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Wait until the worker stopped running.
 */
int worker_wait(worker_t *worker) {
    if (pthread_join(worker->id, NULL) < 0) {
        LOG_ERROR("failed to join pthread: %s (%d)", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Try to stop the worker.
 */
int worker_stop(worker_t *worker, int signum) {
    size_t data = signum;

    pipe_message_t message = (pipe_message_t){.type = PIPE_SIGNAL, .data = (void *)data, .data_size = sizeof(int)};

    if (write(worker->pipe_fds[1], (const void *)&message, sizeof(pipe_message_t)) < 0) {
        LOG_ERROR("failed to send pipe message: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Send custom data to the worker.
 */
int worker_send(worker_t *worker, void *data, size_t size) {
    pipe_message_t message = (pipe_message_t){.type = PIPE_DATA, .data = data, .data_size = size};

    if (write(worker->pipe_fds[1], (const void *)&message, sizeof(pipe_message_t)) < 0) {
        LOG_ERROR("failed to send pipe message: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Add into the worker's watchlist.
 */
int worker_add_watchlist(worker_t *worker, uint32_t events, int fd, void *data) {
    if (worker->watchlist_count >= worker->watchlist_size) {
        errno = EPERM;
        return -1;
    }

    struct epoll_event event = (struct epoll_event){.events = events, .data.ptr = data};
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
        LOG_ERROR("failed to add file to the epoll watchlist: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    worker->watchlist_count++;

    return 0;
}

/**
 * Remove from the worker's watchlist.
 */
int worker_remove_watchlist(worker_t *worker, int fd) {
    if (epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        LOG_ERROR("failed to remove file from the epoll watchlist: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    worker->watchlist_count--;

    return 0;
}
