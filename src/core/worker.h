#ifndef worker_h
#define worker_h

#include <lib/poller.h>

#include "config.h"
#include "job.h"

/**
 * @brief Worker data structure.
 */
typedef struct worker {
    /**
     * @brief Poller instance.
     */
    poller_t poller;

    /**
     * @brief Private data, do not touch this.
     */
    void* data;
} worker_t;

/**
 * @brief Initialize the worker instance.
 *
 * The configuration data and job manager instance will not be freed during the
 * cleanup process.
 *
 * @param[out] worker  The worker instance.
 * @param[in]  config  Configuration data.
 * @param[in]  manager The job manager instance.
 */
void worker_init(worker_t* worker, worker_config_t* config, manager_t* manager);

/**
 * @brief Setup the worker instance.
 *
 * @param[out] worker The worker instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int worker_setup(worker_t* worker);

/**
 * @brief Run the worker on a separate process.
 *
 * @param[in] worker The worker instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int worker_run(worker_t* worker);

/**
 * @brief Wait for the worker process to stop.
 *
 * @param[in] worker The worker instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int worker_wait(worker_t* worker);

/**
 * @brief Stop the worker process.
 *
 * @param[in] worker The worker instance.
 * 
 * @return Result code, 0 for success or -1 for errors.
 */
int worker_stop(worker_t* worker);

/**
 * @brief Assign a job to the worker.
 *
 * @param[in] worker The worker instance.
 * @param[in] job    The job instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int worker_assign(worker_t* worker, job_t* job);

/**
 * @brief Clean all related stuff from the worker instance.
 *
 * @param[out] worker The worker instance.
 */
void worker_cleanup(worker_t* worker);

#endif
