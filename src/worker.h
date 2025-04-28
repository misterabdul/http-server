#ifndef worker_h
#define worker_h

#include <pthread.h>
#include <sys/epoll.h>

/**
 * @brief Worker data structure.
 */
typedef struct worker {
    pthread_t id;
    pthread_attr_t attr;
    struct epoll_event *watchlist;
    int epoll_fd, pipe_fds[2], watchlist_count, watchlist_size;
    void *context;

    void (*data_handler)(struct worker *worker, void *data, size_t size);
    void (*event_handler)(struct worker *worker, uint32_t events, void *data);
    void (*stop_handler)(struct worker *worker, int signum);
} worker_t;

/**
 * @brief Initialize worker.
 *
 * @param[out] worker The worker instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int worker_init(worker_t *worker);

/**
 * @brief Run worker in a separate thread.
 *
 * @param[out] worker The worker instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int worker_run(worker_t *worker);

/**
 * @brief Wait until the worker no longer run.
 *
 * @param[out] worker The worker instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int worker_wait(worker_t *worker);

/**
 * @brief Send stop signal to the running worker.
 *
 * @param[out] worker The worker instance.
 * @param[in]  signum The signal number.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int worker_stop(worker_t *worker, int signum);

/**
 * @brief Send a pointer of data and its size to the running worker.
 *
 * @param[out] worker The worker instance.
 * @param[in]  data   The pointer of the data to be sent.
 * @param[in]  size   The size of the actual data data.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int worker_send(worker_t *worker, void *data, size_t size);

/**
 * @brief Add file descriptor to the worker's watchlist.
 *
 * @param[out] worker The worker instance.
 * @param[in]  events The epoll event to be watched.
 * @param[in]  fd     The file descriptor to be watched.
 * @param[in]  data   The data to be included.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int worker_add_watchlist(worker_t *worker, uint32_t events, int fd, void *data);

/**
 * @brief Remove file descriptor to the worker's watchlist.
 *
 * @param[out] worker The worker instance.
 * @param[in]  fd     The file descriptor to be removed.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int worker_remove_watchlist(worker_t *worker, int fd);

#endif
