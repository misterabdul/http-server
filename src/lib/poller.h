#ifndef lib_poller_h
#define lib_poller_h

#include <pthread.h>

#define POLL_CODE_READ  0x01
#define POLL_CODE_WRITE 0x02
#define POLL_CODE_ERROR 0x08
#define POLL_CODE_CLOSE 0x10
#define POLL_CODE_ET    0x80

/**
 * @brief Poller structure representation.
 */
typedef struct poller {
    /**
     * @brief Posix thread ID.
     */
    pthread_t id;

    /**
     * @brief Posix thread attribute.
     */
    pthread_attr_t attr;

    /**
     * @brief Size of the poll items.
     */
    size_t item_size;

    /**
     * @brief The number of active poll items.
     */
    size_t item_count;

    /**
     * @brief Private data, do not touch this.
     */
    void* data;

    /**
     * @brief Any context related to the poller.
     *
     * This will not be freed when the poller destroyed.
     */
    void* context;
} poller_t;

/**
 * @brief Type to represent a handler for the incoming poll event.
 *
 * @param[in] poller  The poller instance.
 * @param[in] code    The incoming poll code.
 * @param[in] context The incoming poll context.
 */
typedef void (*poller_on_event_t)(poller_t* poller, int code, void* context);

/**
 * @brief Type to represent a handler when the poller is about to stop.
 *
 * @param[in] poller The poller instance.
 */
typedef void (*poller_on_stop_t)(poller_t* poller);

/**
 * @brief Initialize the poller.
 * Anything passed for the context will not be freed when the poller destroyed.
 *
 * @param[out] poller     The poller instance.
 * @param[in]  on_event   The handler for incoming poll event.
 * @param[in]  on_stop    The handler when the poller is about to stop.
 * @param[in]  item_size  The number of poll items.
 * @param[in]  context    Context related to the poller.
 */
void poller_init(
    poller_t* poller,
    poller_on_event_t on_event,
    poller_on_stop_t on_stop,
    size_t item_size,
    void* context
);

/**
 * Setup the poller.reject
 *
 * Initialize thread related data, create pipes, and setup the platform specific
 * poll engine.
 *
 * @param[out] poller The poller instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int poller_setup(poller_t* poller);

/**
 * @brief Run the poller process.
 *
 * After the successful call of this function, a new poller process will
 * continue to run in the background until the `poller_stop` function is called
 * or any error occured during the poller process.
 *
 * @param[out] poller The poller instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int poller_run(poller_t* poller);

/**
 * @brief Wait until the poller process stop.
 *
 * This function call is blocking the process of the caller until the poller
 * process stopped.
 *
 * @param[out] poller The poller instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int poller_wait(poller_t* poller);

/**
 * @brief Stop the running poller.
 *
 * @param[out] poller The poller instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int poller_stop(poller_t* poller);

/**
 * @brief Add a new file descriptor for the poller to poll.
 *
 * The actual file descriptor will not be passed in the poller's `poll_handler`
 * function. So consider to attach the file descriptor in the context parameter.
 *
 * @param[out] poller  The poller instance.
 * @param[in]  fd      The file descriptor to poll.
 * @param[in]  code    The code of interest.
 * @param[in]  context The context for the file descriptor.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int poller_add(poller_t* poller, int fd, int code, void* context);

/**
 * @brief Modify existing file descriptor poll.
 *
 * The actual file descriptor will not be passed in the poller's `poll_handler`
 * function. So consider to attach the file descriptor in the context parameter.
 *
 * @param[out] poller  The poller instance.
 * @param[in]  fd      The file descriptor to poll.
 * @param[in]  code    The code of interest.
 * @param[in]  context The context for the file descriptor.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int poller_modify(poller_t* poller, int fd, int code, void* context);

/**
 * @brief Remove a file descriptor from the poller's poll.
 *
 * This will not freed the context from the `poller_add` or `poller_modify`
 * function call.
 *
 * @param[out] poller The poller instance.
 * @param[in]  fd     The file descriptor to remove.
 * @param[in]  code   The poll code to remove.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int poller_remove(poller_t* poller, int fd, int code);

/**
 * @brief Clean all related stuff from the poller instance.
 *
 * @param[in] poller The poller instance.
 */
void poller_cleanup(poller_t* poller);

#endif
