#include <contracts/thread.h>
#include <core/config.h>
#include <core/log.h>
#include <core/thread.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

typedef struct platform_thread {
    thread_t thread;

    int epoll_fd;
    struct epoll_event *watchlist;
} platform_thread_t;

thread_t *thread_new(size_t watchlist_size) {
    platform_thread_t *_thread;
    if (watchlist_size < 2) {
        watchlist_size = 2;
    }

    _thread = malloc(sizeof(platform_thread_t) + watchlist_size * sizeof(struct epoll_event));
    _thread->watchlist = (struct epoll_event *)((char *)_thread + sizeof(platform_thread_t));
    _thread->thread.watchlist_size = watchlist_size;

    return (thread_t *)_thread;
}

/**
 * Initialize the thread instance.
 */
int thread_init(thread_t *thread) {
    platform_thread_t *_thread = (platform_thread_t *)thread;

    if (pthread_attr_init(&_thread->thread.attr) < 0) {
        LOG_ERROR("failed to initialize pthread attribute: %s (%d)", strerror(errno), errno);
        return -1;
    }

    if ((_thread->epoll_fd = epoll_create1(0)) < 0) {
        LOG_ERROR("failed to create epoll: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    if (pipe(_thread->thread.pipe_fds) < 0) {
        LOG_ERROR("failed to create pipes: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    struct epoll_event _event = (struct epoll_event){.events = EPOLLIN, .data.fd = _thread->thread.pipe_fds[0]};
    if (epoll_ctl(_thread->epoll_fd, EPOLL_CTL_ADD, _thread->thread.pipe_fds[0], &_event) < 0) {
        LOG_ERROR("failed adding pipe in to the epoll watchlist: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    _thread->thread.watchlist_count = 1;

    return 0;
}

/**
 * Add into the thread's watchlist.
 */
int thread_add_watchlist(thread_t *thread, int events, int fd, void *data) {
    platform_thread_t *_thread = (platform_thread_t *)thread;

    if (_thread->thread.watchlist_count >= _thread->thread.watchlist_size) {
        errno = EPERM;
        return -1;
    }

    struct epoll_event _event = (struct epoll_event){.events = events, .data.ptr = data};
    if (epoll_ctl(_thread->epoll_fd, EPOLL_CTL_ADD, fd, &_event) < 0) {
        LOG_ERROR("failed to add file to the epoll watchlist: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    _thread->thread.watchlist_count++;

    return 0;
}

/**
 * Remove from the thread's watchlist.
 */
int thread_remove_watchlist(thread_t *thread, int fd) {
    platform_thread_t *_thread = (platform_thread_t *)thread;

    if (epoll_ctl(_thread->epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        LOG_ERROR("failed to remove file from the epoll watchlist: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    _thread->thread.watchlist_count--;

    return 0;
}

void *platform_thread_runner(void *data) {
    platform_thread_t *_thread = (platform_thread_t *)data;
    pipe_message_t _message = {0};
    int _signum = 0;

    for (int _epoll_nums, _run = 1; _run;) {
        _epoll_nums = epoll_wait(_thread->epoll_fd, _thread->watchlist, _thread->thread.watchlist_size, EPOLL_TIMEOUT);
        if (_epoll_nums < 0) {
            LOG_ERROR("failed to get epoll data: %s (%d)\n", strerror(errno), errno);
            return NULL;
        }

        for (int _i = 0; _i < _epoll_nums; _i++) {
            if (_thread->watchlist[_i].data.fd == _thread->thread.pipe_fds[0]) {
                if (read(_thread->thread.pipe_fds[0], (void *)&_message, sizeof(pipe_message_t)) < 0) {
                    LOG_ERROR("failed to read from pipe: %s (%d)\n", strerror(errno), errno);
                    continue;
                }
                if (_message.type == MESSAGE_SIGNAL) {
                    _signum = (size_t)(_message.data);
                    _run = 0;
                    break;
                }

                if (_message.type == MESSAGE_DATA && _thread->thread.data_handler) {
                    _thread->thread.data_handler((thread_t *)_thread, _message.data, _message.data_size);
                }

                continue;
            }

            if (_thread->thread.poll_handler) {
                _thread->thread.poll_handler(
                    (thread_t *)_thread, _thread->watchlist[_i].events, _thread->watchlist[_i].data.ptr
                );
            }
        }
    }

    if (epoll_ctl(_thread->epoll_fd, EPOLL_CTL_DEL, _thread->thread.pipe_fds[0], NULL) < 0) {
        LOG_ERROR("failed to remove pipe fd (in) from epoll: %s (%d)\n", strerror(errno), errno);
    }
    if (close(_thread->epoll_fd) < 0) {
        LOG_ERROR("failed to close epoll fd: %s (%d)\n", strerror(errno), errno);
    }
    if (close(_thread->thread.pipe_fds[0]) < 0) {
        LOG_ERROR("failed to close pipe fd (in): %s (%d)\n", strerror(errno), errno);
    }
    if (close(_thread->thread.pipe_fds[1]) < 0) {
        LOG_ERROR("failed to close pipe fd (out): %s (%d)\n", strerror(errno), errno);
    }

    if (_thread->thread.stop_handler) {
        _thread->thread.stop_handler((thread_t *)_thread, _signum);
    }

    return NULL;
}
