#include "thread.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "log.h"

/**
 * @brief Thread's pipe message type.
 */
typedef enum message_type {
    MESSAGE_SIGNAL = 0,
    MESSAGE_DATA = 1,
} message_type_t;

/**
 * @brief Thread's pipe message data structure.
 */
typedef struct pipe_message {
    message_type_t type;
    void *data;
    size_t data_size;
} pipe_message_t;

/**
 * @brief Thread runner function for pthread routine.
 *
 * @param data User data from the pthread.
 *
 * @return Data for pthread return value.
 */
void *thread_runner(void *data) {
    thread_t *_thread = (thread_t *)data;
    pipe_message_t _message = {0};
    int _signum = 0;

    for (int _epoll_nums, _run = 1; _run;) {
        _epoll_nums = epoll_wait(_thread->epoll_fd, _thread->watchlist, _thread->watchlist_size, EPOLL_TIMEOUT);
        if (_epoll_nums < 0) {
            LOG_ERROR("failed to get epoll data: %s (%d)\n", strerror(errno), errno);
            return NULL;
        }

        for (int _i = 0; _i < _epoll_nums; _i++) {
            if (_thread->watchlist[_i].data.fd == _thread->pipe_fds[0]) {
                if (read(_thread->pipe_fds[0], (void *)&_message, sizeof(pipe_message_t)) < 0) {
                    LOG_ERROR("failed to read from pipe: %s (%d)\n", strerror(errno), errno);
                    continue;
                }
                if (_message.type == MESSAGE_SIGNAL) {
                    _signum = (size_t)(_message.data);
                    _run = 0;
                    break;
                }

                if (_message.type == MESSAGE_DATA && _thread->data_handler) {
                    _thread->data_handler(_thread, _message.data, _message.data_size);
                }

                continue;
            }

            if (_thread->poll_handler) {
                _thread->poll_handler(_thread, _thread->watchlist[_i].events, _thread->watchlist[_i].data.ptr);
            }
        }
    }

    if (epoll_ctl(_thread->epoll_fd, EPOLL_CTL_DEL, _thread->pipe_fds[0], NULL) < 0) {
        LOG_ERROR("failed to remove pipe fd (in) from epoll: %s (%d)\n", strerror(errno), errno);
    }
    if (close(_thread->epoll_fd) < 0) {
        LOG_ERROR("failed to close epoll fd: %s (%d)\n", strerror(errno), errno);
    }
    if (close(_thread->pipe_fds[0]) < 0) {
        LOG_ERROR("failed to close pipe fd (in): %s (%d)\n", strerror(errno), errno);
    }
    if (close(_thread->pipe_fds[1]) < 0) {
        LOG_ERROR("failed to close pipe fd (out): %s (%d)\n", strerror(errno), errno);
    }

    if (_thread->stop_handler) {
        _thread->stop_handler(_thread, _signum);
    }

    return NULL;
}

/**
 * Initialize the thread instance.
 */
int thread_init(thread_t *thread, struct epoll_event *watchlist_buffer, size_t watchlist_buffer_size) {
    if (watchlist_buffer_size < 2) {
        errno = EPERM;
        return -1;
    }

    thread->watchlist = watchlist_buffer, thread->watchlist_size = watchlist_buffer_size;
    thread->watchlist_count = 1;

    if (pthread_attr_init(&thread->attr) < 0) {
        LOG_ERROR("failed to initialize pthread attribute: %s (%d)", strerror(errno), errno);
        return -1;
    }

    if ((thread->epoll_fd = epoll_create1(0)) < 0) {
        LOG_ERROR("failed to create epoll: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (pipe(thread->pipe_fds) < 0) {
        LOG_ERROR("failed to create pipes: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    struct epoll_event _event = (struct epoll_event){.events = EPOLLIN, .data.fd = thread->pipe_fds[0]};
    if (epoll_ctl(thread->epoll_fd, EPOLL_CTL_ADD, thread->pipe_fds[0], &_event) < 0) {
        LOG_ERROR("failed adding pipe in to the epoll watchlist: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Run the thread in a separate thread.
 */
int thread_run(thread_t *thread) {
    if (pthread_create(&thread->id, &thread->attr, thread_runner, (void *)thread) < 0) {
        LOG_ERROR("failed to create pthread: %s (%d)", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Wait until the thread stopped running.
 */
int thread_wait(thread_t *thread) {
    if (pthread_join(thread->id, NULL) < 0) {
        LOG_ERROR("failed to join pthread: %s (%d)", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Try to stop the thread.
 */
int thread_stop(thread_t *thread, int signum) {
    size_t _data = signum;

    pipe_message_t _message = (pipe_message_t){.type = MESSAGE_SIGNAL, .data = (void *)_data, .data_size = sizeof(int)};

    if (write(thread->pipe_fds[1], (const void *)&_message, sizeof(pipe_message_t)) < 0) {
        LOG_ERROR("failed to send pipe message: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Send custom data to the thread.
 */
int thread_send(thread_t *thread, void *data, size_t size) {
    pipe_message_t _message = (pipe_message_t){.type = MESSAGE_DATA, .data = data, .data_size = size};

    if (write(thread->pipe_fds[1], (const void *)&_message, sizeof(pipe_message_t)) < 0) {
        LOG_ERROR("failed to send pipe message: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Add into the thread's watchlist.
 */
int thread_add_watchlist(thread_t *thread, uint32_t events, int fd, void *data) {
    if (thread->watchlist_count >= thread->watchlist_size) {
        errno = EPERM;
        return -1;
    }

    struct epoll_event _event = (struct epoll_event){.events = events, .data.ptr = data};
    if (epoll_ctl(thread->epoll_fd, EPOLL_CTL_ADD, fd, &_event) < 0) {
        LOG_ERROR("failed to add file to the epoll watchlist: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    thread->watchlist_count++;

    return 0;
}

/**
 * Remove from the thread's watchlist.
 */
int thread_remove_watchlist(thread_t *thread, int fd) {
    if (epoll_ctl(thread->epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        LOG_ERROR("failed to remove file from the epoll watchlist: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    thread->watchlist_count--;

    return 0;
}
