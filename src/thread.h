#ifndef thread_h
#define thread_h

#include <pthread.h>
#include <sys/epoll.h>

/**
 * @brief Thread data structure.
 */
typedef struct thread {
    pthread_t id;
    pthread_attr_t attr;
    struct epoll_event *watchlist;
    int epoll_fd, pipe_fds[2], watchlist_count, watchlist_size;
    void *context;

    void (*data_handler)(struct thread *thread, void *data, size_t size);
    void (*poll_handler)(struct thread *thread, uint32_t events, void *data);
    void (*stop_handler)(struct thread *thread, int signum);
} thread_t;

/**
 * @brief Initialize the thread.
 *
 * @param[out] thread                The thread instance.
 * @param[in]  watchlist_buffer      The watchlist buffer for the thread.
 * @param[in]  watchlist_buffer_size The watchlist buffer size.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int thread_init(thread_t *thread, struct epoll_event *watchlist_buffer, size_t watchlist_buffer_size);

/**
 * @brief Run the thread in the background.
 *
 * @param[out] thread The thread instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int thread_run(thread_t *thread);

/**
 * @brief Wait until the thread no longer run.
 *
 * @param[out] thread The thread instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int thread_wait(thread_t *thread);

/**
 * @brief Send stop signal to the running thread.
 *
 * @param[out] thread The thread instance.
 * @param[in]  signum The signal number.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int thread_stop(thread_t *thread, int signum);

/**
 * @brief Send a pointer of data and its size to the running thread.
 *
 * @param[out] thread The thread instance.
 * @param[in]  data   The pointer of the data to be sent.
 * @param[in]  size   The size of the actual data data.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int thread_send(thread_t *thread, void *data, size_t size);

/**
 * @brief Add file descriptor to the thread's watchlist.
 *
 * @param[out] thread The thread instance.
 * @param[in]  events The epoll event to be watched.
 * @param[in]  fd     The file descriptor to be watched.
 * @param[in]  data   The data to be included.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int thread_add_watchlist(thread_t *thread, uint32_t events, int fd, void *data);

/**
 * @brief Remove file descriptor to the thread's watchlist.
 *
 * @param[out] thread The thread instance.
 * @param[in]  fd     The file descriptor to be removed.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int thread_remove_watchlist(thread_t *thread, int fd);

#endif
