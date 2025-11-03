#include <lib/poller.h>

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <misc/logger.h>

#include <lib/hashmap.h>
#include <lib/objpool.h>

#define POLL_TIMEOUT 1000 /* miliseconds */

/**
 * @brief All the private data for poller to run.
 */
typedef struct data {
    /**
     * @brief Hashmap for file descriptor integer with its corresponding data.
     */
    map_t fd_map;

    /**
     * @brief Object pool for the file descriptor data.
     */
    objpool_t fd_data_pool;

    /**
     * @brief The poll items buffer for the received poll events.
     */
    struct pollfd* items;

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
 * @brief File descriptor data representation.
 */
typedef struct fd_data {
    /**
     * @brief The file descriptor integer.
     */
    int fd;

    /**
     * @brief The index value in the poll items buffer.
     */
    size_t index;

    /**
     * @brief The context of the file descriptor.
     */
    void* context;
} fd_data_t;

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
        .items = calloc(item_size, sizeof(struct pollfd)),
        .fd_data_pool = (objpool_t){0},
        .fd_map = (map_t){0},
        .on_event = on_event,
        .on_stop = on_stop,
    };
    map_init(&_data->fd_map, item_size);
    objpool_init(&_data->fd_data_pool);

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

    data_t* _data = (data_t*)poller->data;
    _ret = pthread_mutex_init(&_data->lock, NULL);
    if (_ret != 0) {
        LOG_ERROR("pthread_mutex_init: %s (%d)\n", strerror(_ret), _ret);
        return -1;
    }
    if (map_setup(&_data->fd_map) == -1) {
        return -1;
    }
    if (objpool_setup(&_data->fd_data_pool) == -1) {
        return -1;
    }
    _ret = objpool_allocate(
        &_data->fd_data_pool, poller->item_size, sizeof(fd_data_t)
    );
    if (_ret == -1) {
        return -1;
    }

    return 0;
}

/**
 * @copydoc poller_run
 */
int poller_run(poller_t* poller) {
    /* Start running a separate thread. */
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
    if (poller->item_count >= poller->item_size - 1) {
        errno = EPERM;
        return -1;
    }

    data_t* _data = (data_t*)poller->data;
    pthread_mutex_lock(&_data->lock);

    /* Assign the file descriptor to the poll items. */
    size_t _index = poller->item_count;
    _data->items[_index] = (struct pollfd){
        .events = code2event(code),
        .revents = 0,
        .fd = fd,
    };

    /* Store the index of the file descriptor in the poll items and its context
     * on the hash table. */
    fd_data_t* _fd_data = objpool_acquire(&_data->fd_data_pool);

    *_fd_data = (fd_data_t){.fd = fd, .index = _index, .context = context};
    int _ret = map_add(&_data->fd_map, &_fd_data->fd, sizeof(int), _fd_data);
    if (_ret == -1) {
        objpool_release(&_data->fd_data_pool, _fd_data);
    } else {
        poller->item_count++;
    }

    pthread_mutex_unlock(&_data->lock);

    return _ret;
}

/**
 * @copydoc poller_modify
 */
int poller_modify(poller_t* poller, int fd, int code, void* context) {
    /* suppressing unused parameter */
    (void)context;

    /* Try to get the file descriptor data from the hash table. */
    data_t* _data = (data_t*)poller->data;
    fd_data_t* _fd_data = map_get(&_data->fd_map, &fd, sizeof(int), NULL);
    if (_fd_data) {
        return -1;
    }

    pthread_mutex_lock(&_data->lock);

    /* Perform the modification. */
    _data->items[_fd_data->index].events = code2event(code);

    pthread_mutex_unlock(&_data->lock);

    return 0;
}

/**
 * @copydoc poller_remove
 */
int poller_remove(poller_t* poller, int fd, int code) {
    (void)code;

    /* Try to get the file descriptor data from the hash table. */
    data_t* _data = (data_t*)poller->data;
    fd_data_t* _fd_data = map_get(&_data->fd_map, &fd, sizeof(int), NULL);
    if (_fd_data == NULL) {
        return -1;
    }

    pthread_mutex_lock(&_data->lock);

    /* Get the index of the file descriptor in the poll items. */
    size_t _index = _fd_data->index;

    /* Remove unnecessary data. */
    map_remove(&_data->fd_map, &fd, sizeof(int), NULL);
    objpool_release(&_data->fd_data_pool, _fd_data);

    /**
     * The idea is to replace the current item & its index with the end of the
     * items array. Then unset the end of the array.
     */
    struct pollfd* _tail = &_data->items[poller->item_count - 1];
    if (_tail->fd && _tail->fd != fd) {
        _data->items[_index] = (struct pollfd){
            .events = _tail->events,
            .revents = _tail->revents,
            .fd = _tail->fd,
        };
        _fd_data = map_get(&_data->fd_map, &_tail->fd, sizeof(int), NULL);
        if (_fd_data) {
            _fd_data->index = _index;
        }
    }
    *_tail = (struct pollfd){
        .events = 0,
        .revents = 0,
        .fd = 0,
    };
    poller->item_count--;

    pthread_mutex_unlock(&_data->lock);

    return 0;
}

/**
 * @copydoc poller_cleanup
 */
void poller_cleanup(poller_t* poller) {
    data_t* _data = (data_t*)poller->data;
    objpool_cleanup(&_data->fd_data_pool);
    map_cleanup(&_data->fd_map);
    free(_data->items);
    free(_data);
}

/**
 * @copydoc thread_routine
 */
static void* thread_routine(void* ptr) {
    poller_t* _poller = (poller_t*)ptr;
    data_t* _data = (data_t*)_poller->data;
    fd_data_t* _fd_data;

    /* Just stop when the even handler is not set. */
    if (_data->on_event == NULL) {
        if (_data->on_stop) {
            _data->on_stop(_poller);
        }

        return NULL;
    }

    /* Main loop. */
    pthread_cleanup_push(thread_cleanup, ptr);
    for (int _count, _code, _i, _run = 1; _run;) {
        pthread_testcancel();

        _count = poll(_data->items, _poller->item_count, POLL_TIMEOUT);
        if (_count == -1) {
            if (errno == EINTR) {
                continue;
            }

            LOG_ERROR("poll: %s (%d)\n", strerror(errno), errno);
            _run = 0;
            break;
        }

        for (_i = 0; _i < (int)_poller->item_count && _count > 0; _i++) {
            if (_data->items[_i].revents == 0) {
                continue;
            }

            _fd_data = map_get(
                &_data->fd_map, &_data->items[_i].fd, sizeof(int), NULL
            );
            if (_fd_data == NULL) {
                _run = 0;
                break;
            }

            _code = event2code(_data->items[_i].revents);
            _data->on_event(_poller, _code, _fd_data->context);
            _data->items[_i].revents = 0;
            _count--;
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
