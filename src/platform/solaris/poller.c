#include <lib/poller.h>

#include <errno.h>
#include <poll.h>
#include <port.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <misc/logger.h>

#define POLL_TIMEOUT 1 /* second */

/**
 * @brief All the private data for poller to run (BSD specific).
 */
typedef struct data {
    /**
     * @brief The Solaris event port file descriptor.
     */
    int port_fd;

    /**
     * @brief The Solaris port event items buffer for the received events.
     */
    port_event_t* items;

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
 * @brief Convert poll code to unix poll event.
 *
 * @param[in] code The poll code.
 *
 * @return Poll event.
 */
static inline short code2event(int code);

/**
 * @brief Convert unix poll event to poll code.
 *
 * @param[in] event The unix poll event.
 *
 * @return Poll code.
 */
static inline int event2code(short event);

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
        .items = malloc(item_size * sizeof(port_event_t)),
        .on_event = on_event,
        .on_stop = on_stop,
        .port_fd = -1,
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

    /* Create Solaris event ports. */
    _data->port_fd = port_create();
    if (_data->port_fd == -1) {
        LOG_ERROR("port_create: %s (%d)\n", strerror(errno), errno);
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
        errno = _ret;
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

    /* Assign the file descriptor to the event ports. */
    data_t* _data = (data_t*)poller->data;
    int _ret = port_associate(
        _data->port_fd, PORT_SOURCE_FD, fd, code2event(code), context
    );
    if (_ret == -1) {
        LOG_ERROR("port_associate: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    pthread_mutex_lock(&_data->lock);
    poller->item_count++;
    pthread_mutex_unlock(&_data->lock);

    return 0;
}

/**
 * @copydoc poller_modify
 */
int poller_modify(poller_t* poller, int fd, int code, void* context) {
    /* Reassign the file descriptor to the event ports. */
    data_t* _data = (data_t*)poller->data;
    int _ret = port_associate(
        _data->port_fd, PORT_SOURCE_FD, fd, code2event(code), context
    );
    if (_ret == -1) {
        LOG_ERROR("port_associate: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * @copydoc poller_remove
 */
int poller_remove(poller_t* poller, int fd, int code) {
    (void)code;

    /**
     * Remove the file descriptor from the event ports.
     * Normally no need to call `port_dissociate` because Solaris automatically
     * remove any file descriptor event association after an event is received.
     */
    data_t* _data = (data_t*)poller->data;
    int _ret = port_dissociate(_data->port_fd, PORT_SOURCE_FD, fd);
    if (_ret == -1) {
        LOG_ERROR("port_dissociate: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    pthread_mutex_lock(&_data->lock);
    poller->item_count--;
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
    int _ret;

    /* Just stop when the even handler is not set. */
    if (_data->on_event == NULL) {
        if (close(_data->port_fd) == -1) {
            LOG_ERROR("close: %s (%d)\n", strerror(errno), errno);
        }
        if (_data->on_stop) {
            _data->on_stop(_poller);
        }

        return NULL;
    }

    /* Main loop. */
    pthread_cleanup_push(thread_cleanup, ptr);
    timespec_t _timeout = (timespec_t){.tv_sec = POLL_TIMEOUT, .tv_nsec = 0};
    for (uint_t _count, _code, _i, _run = 1; _run;) {
        pthread_testcancel();

        _count = _poller->item_count > 0 ? _poller->item_count : 1;
        _ret = port_getn(
            _data->port_fd, _data->items, _poller->item_size, &_count, &_timeout
        );
        if (_ret == -1) {
            if (errno == EINTR || errno == ETIME) {
                continue;
            }

            LOG_ERROR("port_getn: %s (%d)\n", strerror(errno), errno);
            _run = 0;
            break;
        }

        /**
         * Solaris is automatically remove any file descriptor event association
         * after an event is received.
         */
        if (_count > 0) {
            pthread_mutex_lock(&_data->lock);
            _poller->item_count -= _count;
            pthread_mutex_unlock(&_data->lock);
        }

        for (_i = 0; _i < _count; _i++) {
            if (_data->on_event) {
                _code = event2code(_data->items[_i].portev_events);
                _data->on_event(_poller, _code, _data->items[_i].portev_user);
            }
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

    /* Close the Solaris event port. */
    if (close(_data->port_fd) == -1) {
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
static inline short code2event(int code) {
    short _event = 0x00;

    if (code & POLL_CODE_READ) {
        _event |= POLLIN | POLLPRI;
    }
    if (code & POLL_CODE_WRITE) {
        _event |= POLLOUT;
    }

    return _event;
}

/**
 * @copydoc event2code
 */
static inline int event2code(short event) {
    int _code = 0x00;

    if (event & POLLPRI) {
        _code |= POLL_CODE_READ;
    }
    if (event & POLLIN) {
        _code |= POLL_CODE_READ;
    }
    if (event & POLLOUT) {
        _code |= POLL_CODE_WRITE;
    }
    if (event & POLLHUP) {
        _code |= POLL_CODE_CLOSE;
    }
    if (event & POLLERR) {
        _code |= POLL_CODE_ERROR;
    }

    return _code;
}
