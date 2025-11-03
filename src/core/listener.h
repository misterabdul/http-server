#ifndef listener_h
#define listener_h

#include <lib/poller.h>
#include <lib/transport.h>

#include "config.h"
#include "worker.h"

/**
 * @brief Listener data structure.
 */
typedef struct listener {
    /**
     * @brief Poller instance.
     */
    poller_t poller;

    /**
     * @brief Transport server instance.
     */
    server_t server;

    /**
     * @brief Private data, do not touch this.
     */
    void* data;
} listener_t;

/**
 * @brief Initialize the listener instance.
 *
 * The configuration data, job manager, and worker instances will not be freed
 * during the cleanup process.
 *
 * @param[out] listener     The listener instance.
 * @param[in]  config       The configuration data instance.
 * @param[in]  manager      The job manager instance.
 * @param[in]  workers      The worker instances.
 * @param[in]  worker_count The number of worker instances.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
void listener_init(
    listener_t* listener,
    listener_config_t* config,
    manager_t* manager,
    worker_t* workers,
    int worker_count
);

/**
 * @brief Setup the listener instance.
 *
 * @param[out] listener The listener instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int listener_setup(listener_t* listener);

/**
 * @brief Run the listener process.
 *
 * @param[in] listener The listener instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int listener_run(listener_t* listener);

/**
 * @brief Wait for the listener process to stop.
 *
 * @param[in] listener The listener instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int listener_wait(listener_t* listener);

/**
 * @brief Stop the listener process.
 *
 * @param[in] listener The listener instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int listener_stop(listener_t* listener);

/**
 * @brief Clean all related stuff from the listener instance.
 *
 * @param[out] listener The listener instance.
 */
void listener_cleanup(listener_t* listener);

#endif
