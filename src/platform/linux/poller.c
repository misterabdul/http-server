#include <lib/poller.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <misc/logger.h>

#define POLL_TIMEOUT 1000 /* miliseconds */

/**
 * @brief All the private data for poller to run (Linux specific).
 */
typedef struct data {
    /**
     * @brief The Linux epoll file descriptor.
     */
    int epoll_fd;

    /**
     * @brief The Linux epoll items buffer for the received epoll events.
     */
    struct epoll_event* items;

    /**
     * @brief The handler for incoming poll event.
     */
    poller_on_event_t on_event;

    /**
     * @brief The handler when the poller is about to stop.
     */
    poller_on_stop_t on_stop;
} data_t;

/**
 * @brief Convert poll code to epoll event.
 *
 * @param[in] code The poll code.
 *
 * @return Epoll event.
 */
static inline uint32_t code2event(int code);

/**
 * @brief Convert epoll event to poll code.
 *
 * @param[in] event The epoll event.
 *
 * @return Poll code.
 */
static inline int event2code(uint32_t event);

/**
 * @brief Poller's thread routine.
 *
 * @param[in] ptr Pointer to be passed, in this case the poller instance.
 *
 * @return Nothing because the result is ignored.
 */
static void* thread_routine(void* ptr);

/**
 * @brief Poller's thread cleanup routine.
 *
 * @param[in] ptr Pointer to be passed, in this case the poller instance.
 */
static void thread_cleanup(void* ptr);

/**
 * @copydoc poller_init
 */
void poller_init(
    poller_t* poller,
    poller_on_event_t on_event,
    poller_on_stop_t on_stop,
    size_t item_size,
    void* context
) {
    data_t* _data = malloc(sizeof(data_t));
    *_data = (data_t){
        .items = malloc(item_size * sizeof(struct epoll_event)),
        .on_event = on_event,
        .on_stop = on_stop,
        .epoll_fd = -1,
    };
    *poller = (poller_t){
        .item_size = item_size,
        .context = context,
        .item_count = 0,
        .data = _data,
    };
}

/**
 * @copydoc poller_setup
 */
int poller_setup(poller_t* poller) {
    /* Initialize the POSIX's thread attribute. */
    int _ret = pthread_attr_init(&poller->attr);
    if (_ret != 0) {
        LOG_ERROR("pthread_attr_init: %s (%d)\n", strerror(_ret), _ret);
        return -1;
    }

    /* Create Linux epoll. */
    data_t* _data = (data_t*)poller->data;
    _data->epoll_fd = epoll_create1(0);
    if (_data->epoll_fd == -1) {
        LOG_ERROR("epoll_create1: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * @copydoc poller_run
 */
int poller_run(poller_t* poller) {
    /* Dispatch a separate thread. */
    int _ret = pthread_create(
        &poller->id, &poller->attr, thread_routine, (void*)poller
    );
    if (_ret != 0) {
        LOG_ERROR("pthread_create: %s (%d)\n", strerror(_ret), _ret);
        return -1;
    }
    _ret = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    if (_ret != 0) {
        LOG_ERROR("pthread_setcancelstate: %s (%d)\n", strerror(_ret), _ret);
        return -1;
    }
    _ret = pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    if (_ret != 0) {
        LOG_ERROR("pthread_setcanceltype: %s (%d)\n", strerror(_ret), _ret);
        return -1;
    }

    return 0;
}

/**
 * @copydoc poller_wait
 */
int poller_wait(poller_t* poller) {
    /* Wait for the separate running thread to stop. */
    int _ret = pthread_join(poller->id, NULL);
    if (_ret != 0) {
        LOG_ERROR("pthread_join: %s (%d)\n", strerror(_ret), _ret);
        return -1;
    }

    return 0;
}

/**
 * @copydoc poller_stop
 */
int poller_stop(poller_t* poller) {
    int _ret = pthread_cancel(poller->id);
    if (_ret != 0) {
        LOG_ERROR("pthread_cancel: %s (%d)\n", strerror(_ret), _ret);
        return -1;
    }

    return 0;
}

/**
 * @copydoc poller_add
 */
int poller_add(poller_t* poller, int fd, int code, void* context) {
    /* Check if the poller could store the item. */
    if (poller->item_count >= poller->item_size) {
        errno = EPERM;
        return -1;
    }

    /* Assign the file descriptor to the epoll. */
    struct epoll_event _item = (struct epoll_event){
        .events = code2event(code),
        .data.ptr = context,
    };
    data_t* _data = (data_t*)poller->data;
    if (epoll_ctl(_data->epoll_fd, EPOLL_CTL_ADD, fd, &_item) == -1) {
        LOG_ERROR("epoll_ctl: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    poller->item_count++;

    return 0;
}

/**
 * @copydoc poller_modify
 */
int poller_modify(poller_t* poller, int fd, int code, void* context) {
    /* Reassign the file descriptor to the epoll. */
    struct epoll_event _item = (struct epoll_event){
        .events = code2event(code),
        .data.ptr = context,
    };
    data_t* _data = (data_t*)poller->data;
    if (epoll_ctl(_data->epoll_fd, EPOLL_CTL_MOD, fd, &_item) == -1) {
        LOG_ERROR("epoll_ctl: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * @copydoc poller_remove
 */
int poller_remove(poller_t* poller, int fd, int code) {
    (void)code;

    /* Remove the file descriptor from the epoll. */
    data_t* _data = (data_t*)poller->data;
    if (epoll_ctl(_data->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        LOG_ERROR("epoll_ctl: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    poller->item_count--;

    return 0;
}

/**
 * @copydoc poller_cleanup
 */
void poller_cleanup(poller_t* poller) {
    /* Free the private data memory. */
    data_t* _data = (data_t*)poller->data;
    free(_data->items);
    free(_data);
}

/**
 * @copydoc thread_routine
 */
static void* thread_routine(void* ptr) {
    poller_t* _poller = (poller_t*)ptr;
    data_t* _data = (data_t*)_poller->data;

    /* Just stop when the even handler is not set. */
    if (_data->on_event == NULL) {
        if (close(_data->epoll_fd) == -1) {
            LOG_ERROR("close: %s (%d)\n", strerror(errno), errno);
        }
        if (_data->on_stop) {
            _data->on_stop(_poller);
        }

        return NULL;
    }

    /* Main loop. */
    pthread_cleanup_push(thread_cleanup, ptr);
    for (int _count, _code, _i, _run = 1; _run;) {
        pthread_testcancel();

        _count = epoll_wait(
            _data->epoll_fd, _data->items, _poller->item_size, POLL_TIMEOUT
        );
        if (_count == -1) {
            if (errno == EINTR) {
                continue;
            }

            LOG_ERROR("epoll_wait: %s (%d)\n", strerror(errno), errno);
            _run = 0;
            break;
        }

        for (_i = 0; _i < _count; _i++) {
            _code = event2code(_data->items[_i].events);
            _data->on_event(_poller, _code, _data->items[_i].data.ptr);
        }
    }
    pthread_cleanup_pop(1);

    return NULL;
}

/**
 * @copydoc thread_cleanup
 */
static void thread_cleanup(void* ptr) {
    poller_t* _poller = (poller_t*)ptr;
    data_t* _data = (data_t*)_poller->data;

    /* Close the Linux epoll. */
    if (close(_data->epoll_fd) == -1) {
        LOG_ERROR("close: %s (%d)\n", strerror(errno), errno);
    }

    /* Callback on stop if set. */
    if (_data->on_stop) {
        _data->on_stop(_poller);
    }
}

/**
 * @copydoc code2event
 */
static inline uint32_t code2event(int code) {
    uint32_t _event = 0x00;

    if (code & POLL_CODE_READ) {
        _event |= EPOLLIN | EPOLLPRI;
    }
    if (code & POLL_CODE_WRITE) {
        _event |= EPOLLOUT;
    }
    if (code & POLL_CODE_ET) {
        _event |= EPOLLET;
    }

    return _event;
}

/**
 * @copydoc event2code
 */
static inline int event2code(uint32_t event) {
    int _code = 0x00;

    if (event & EPOLLPRI) {
        _code |= POLL_CODE_READ;
    }
    if (event & EPOLLIN) {
        _code |= POLL_CODE_READ;
    }
    if (event & EPOLLOUT) {
        _code |= POLL_CODE_WRITE;
    }
    if (event & EPOLLHUP) {
        _code |= POLL_CODE_CLOSE;
    }
    if (event & EPOLLRDHUP) {
        _code |= POLL_CODE_CLOSE;
    }
    if (event & EPOLLERR) {
        _code |= POLL_CODE_ERROR;
    }

    return _code;
}
