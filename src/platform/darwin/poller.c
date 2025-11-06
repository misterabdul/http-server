#include <lib/poller.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <unistd.h>

#include <misc/logger.h>

#define POLL_TIMEOUT 1 /* second */

/**
 * @brief All the private data for poller to run (Darwin specific).
 */
typedef struct data {
    /**
     * @brief The Darwin kqueue file descriptor.
     */
    int kq;

    /**
     * @brief The Darwin kqueue items buffer for the received kqueue events.
     */
    struct kevent* items;

    /**
     * @brief The handler for incoming poll event.
     */
    poller_on_event_t on_event;

    /**
     * @brief The handler when the poller is about to stop.
     */
    poller_on_stop_t on_stop;

    /**
     * @brief Internal lock mutex.
     */
    pthread_mutex_t lock;
} data_t;

/**
 * @brief Convert poll code to kqueue flag.
 *
 * @param[in] code The poll code.
 *
 * @return Kqueue flag.
 */
static inline u_short code2flag(int code);

/**
 * @brief Convert kqueue filter to poll code.
 *
 * @param[in] filter The kqueue filter.
 *
 * @return Poll code.
 */
static inline int filter2code(short filter);

/**
 * @brief Convert kqueue flag to poll code.
 *
 * @param[in] flag The kqueue flag.
 *
 * @return Poll code.
 */
static inline int flag2code(u_short flag);

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
    item_size *= 2;

    data_t* _data = malloc(sizeof(data_t));
    *_data = (data_t){
        .items = malloc(item_size * sizeof(struct kevent)),
        .on_event = on_event,
        .on_stop = on_stop,
        .kq = -1,
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

    /* Initialize the POSIX's thread mutex. */
    data_t* _data = (data_t*)poller->data;
    _ret = pthread_mutex_init(&_data->lock, NULL);
    if (_ret != 0) {
        LOG_ERROR("pthread_mutex_init: %s (%d)\n", strerror(_ret), _ret);
        return -1;
    }

    /* Create BSD kqueue. */
    _data->kq = kqueue();
    if (_data->kq == -1) {
        LOG_ERROR("kqueue: %s (%d)\n", strerror(errno), errno);
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

    data_t* _data = (data_t*)poller->data;
    struct kevent _items[2];
    size_t _item_count = 0;
    if (code & POLL_CODE_READ) {
        _items[_item_count++] = (struct kevent){
            .ident = fd,
            .filter = EVFILT_READ,
            .flags = code2flag(code) | EV_ADD,
            .fflags = 0,
            .data = 0,
            .udata = context,
        };
    }
    if (code & POLL_CODE_WRITE) {
        _items[_item_count++] = (struct kevent){
            .ident = fd,
            .filter = EVFILT_WRITE,
            .flags = code2flag(code) | EV_ADD,
            .fflags = 0,
            .data = 0,
            .udata = context,
        };
    }
    if (_item_count == 0) {
        LOG_ERROR("unknow poller code: %d\n", code);
        return -1;
    }

    /* Assign the file descriptor to the kqueue. */
    if (kevent(_data->kq, _items, _item_count, NULL, 0, NULL) == -1) {
        LOG_ERROR("kevent: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    pthread_mutex_lock(&_data->lock);
    poller->item_count += _item_count;
    pthread_mutex_unlock(&_data->lock);

    return 0;
}

/**
 * @copydoc poller_modify
 */
int poller_modify(poller_t* poller, int fd, int code, void* context) {
    struct kevent _items[2];
    size_t _item_count = 0;
    if (code & POLL_CODE_READ) {
        _items[_item_count++] = (struct kevent){
            .ident = fd,
            .filter = EVFILT_READ,
            .flags = code2flag(code) | EV_ADD,
            .fflags = 0,
            .data = 0,
            .udata = context,
        };
    }
    if (code & POLL_CODE_WRITE) {
        _items[_item_count++] = (struct kevent){
            .ident = fd,
            .filter = EVFILT_WRITE,
            .flags = code2flag(code) | EV_ADD,
            .fflags = 0,
            .data = 0,
            .udata = context,
        };
    }
    if (_item_count == 0) {
        LOG_ERROR("unknow poller code: %d\n", code);
        return -1;
    }

    /* Reassign the file descriptor to the kqueue. */
    data_t* _data = (data_t*)poller->data;
    if (kevent(_data->kq, _items, _item_count, NULL, 0, NULL) == -1) {
        LOG_ERROR("kevent: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * @copydoc poller_remove
 */
int poller_remove(poller_t* poller, int fd, int code) {
    struct kevent _items[2];
    size_t _item_count = 0;
    if (code & POLL_CODE_READ) {
        _items[_item_count++] = (struct kevent){
            .ident = fd,
            .filter = EVFILT_READ,
            .flags = EV_DELETE,
            .fflags = 0,
            .data = 0,
            .udata = NULL,
        };
    }
    if (code & POLL_CODE_WRITE) {
        _items[_item_count++] = (struct kevent){
            .ident = fd,
            .filter = EVFILT_WRITE,
            .flags = EV_DELETE,
            .fflags = 0,
            .data = 0,
            .udata = NULL,
        };
    }
    if (_item_count == 0) {
        LOG_ERROR("unknow poller code: %d\n", code);
        return -1;
    }

    /* Remove the file descriptor from the kqueue. */
    data_t* _data = (data_t*)poller->data;
    if (kevent(_data->kq, _items, _item_count, NULL, 0, NULL) == -1) {
        LOG_ERROR("kevent: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    pthread_mutex_lock(&_data->lock);
    poller->item_count -= _item_count;
    pthread_mutex_unlock(&_data->lock);

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
        if (close(_data->kq) == -1) {
            LOG_ERROR("close: %s (%d)\n", strerror(errno), errno);
        }
        if (_data->on_stop) {
            _data->on_stop(_poller);
        }

        return NULL;
    }

    /* Main loop. */
    pthread_cleanup_push(thread_cleanup, ptr);
    struct timespec _timeout = (struct timespec){
        .tv_sec = POLL_TIMEOUT,
        .tv_nsec = 0,
    };
    for (int _count, _code, _i, _run = 1; _run;) {
        pthread_testcancel();

        _count = kevent(
            _data->kq, NULL, 0, _data->items, _poller->item_size, &_timeout
        );
        if (_count == -1) {
            if (errno == EINTR) {
                continue;
            }

            LOG_ERROR("kevent: %s (%d)\n", strerror(errno), errno);
            _run = 0;
            break;
        }

        for (_i = 0; _i < _count; _i++) {
            _code = filter2code(_data->items[_i].filter);
            _code |= flag2code(_data->items[_i].flags);
            _data->on_event(_poller, _code, _data->items[_i].udata);
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

    /* Close the Darwin kqueue. */
    if (close(_data->kq) == -1) {
        LOG_ERROR("close: %s (%d)\n", strerror(errno), errno);
    }

    /* Callback on stop if set. */
    if (_data->on_stop) {
        _data->on_stop(_poller);
    }
}

/**
 * @copydoc code2flag
 */
static inline u_short code2flag(int code) {
    flag_t _flag = 0x00;

    if (code & POLL_CODE_ET) {
        _flag |= EV_CLEAR;
    }

    return _flag;
}

/**
 * @copydoc filter2code
 */
static inline int filter2code(short filter) {
    if (filter == EVFILT_WRITE) {
        return POLL_CODE_WRITE;
    }

    return POLL_CODE_READ;
}

/**
 * @copydoc flag2code
 */
static inline int flag2code(u_short flag) {
    int _code = 0x00;

    if (flag & EV_EOF) {
        _code |= POLL_CODE_CLOSE;
    }
    if (flag & EV_ERROR) {
        _code |= POLL_CODE_ERROR;
    }

    return _code;
}
