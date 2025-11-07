#include "worker.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <misc/logger.h>
#include <misc/macro.h>

/**
 * @brief All the private data for worker related operation.
 */
typedef struct data {
    /**
     * @brief Configuration data.
     */
    worker_config_t* config;

    /**
     * @brief Job manager instance.
     */
    manager_t* manager;

    /**
     * @brief Raw buffer.
     */
    char* buffer;

    /**
     * @brief Size of the raw buffer.
     */
    size_t buffer_size;
} data_t;

/**
 * @brief The event handler for the worker's poller.
 *
 * @param[in] poller The poller instance.
 * @param[in] code   The received poll code.
 * @param[in] data   The incoming poll data.
 */
static void on_event(poller_t* poller, int code, void* data);

/**
 * @brief Continue working on job.
 *
 * @param[out] worker The worker instance.
 * @param[out] job    The job instance.
 * @param[in]  code   The received poll code.
 */
static inline void continue_job(worker_t* worker, job_t* job, int code);

/**
 * @brief Finish the job, remove it from the poller and return it back to the
 * job manager.
 *
 * @param[out] worker The worker instance.
 * @param[out] job    The job instance.
 * @param[in]  code   The received poll code.
 */
static inline void finish_job(worker_t* worker, job_t* job, int code);

/**
 * @copydoc worker_init
 */
void worker_init(
    worker_t* worker, worker_config_t* config, manager_t* manager
) {
    /* Allocate private data. */
    data_t* _data = malloc(sizeof(data_t));
    *_data = (data_t){
        .buffer = malloc(config->buffer_size * sizeof(char)),
        .buffer_size = config->buffer_size,
        .manager = manager,
        .config = config,
    };

    /* Initialize the worker instance. */
    *worker = (worker_t){
        .poller = (poller_t){0},
        .data = _data,
    };

    /* Initialize the poller instance. */
    poller_init(&worker->poller, on_event, NULL, config->max_job, worker);
}

/**
 * @copydoc worker_setup
 */
int worker_setup(worker_t* worker) {
    return poller_setup(&worker->poller);
}

/**
 * @copydoc worker_run
 */
int worker_run(worker_t* worker) {
    return poller_run(&worker->poller);
}

/**
 * @copydoc worker_wait
 */
int worker_wait(worker_t* worker) {
    return poller_wait(&worker->poller);
}

/**
 * @copydoc worker_stop
 */
int worker_stop(worker_t* worker) {
    return poller_stop(&worker->poller);
}

/**
 * @copydoc worker_assign
 */
int worker_assign(worker_t* worker, job_t* job) {
#if POLL_ENGINE == POLL_ENGINE_KQUEUE
    job->poll_code = POLL_CODE_READ;
#endif

    return poller_add(
        &worker->poller,
        job->connection.socket,
        POLL_CODE_READ | POLL_CODE_ET,
        job
    );
}

/**
 * @copydoc worker_destroy
 */
void worker_cleanup(worker_t* worker) {
    /* Cleanup the poller instance. */
    poller_cleanup(&worker->poller);

    /* Free the private data. */
    data_t* _data = (data_t*)worker->data;
    free(_data->buffer);
    free(_data);
}

/**
 * @copydoc on_event
 */
static void on_event(poller_t* poller, int code, void* data) {
    worker_t* _worker = (worker_t*)poller->context;
    data_t* _data = (data_t*)_worker->data;
    job_t* _job = (job_t*)data;

    /* The job is done. */
    if (code & POLL_CODE_CLOSE) {
        finish_job(_worker, _job, code);
        return;
    }

    /* There's an issue. */
    if (code & POLL_CODE_ERROR) {
        job_handle_issue(_job);
        finish_job(_worker, _job, code);
        return;
    }

    /* Perform the write process. */
    if (code & POLL_CODE_WRITE) {
        if (job_write(_job, _data->buffer, _data->buffer_size) == -1) {
            finish_job(_worker, _job, code);
            return;
        }
        continue_job(_worker, _job, code);
        return;
    }

    /* Perform the read process. */
    if (code & POLL_CODE_READ) {
        if (job_read(_job, _data->buffer, _data->buffer_size) == -1) {
            finish_job(_worker, _job, code);
            return;
        }
        if (_job->state != JOB_STATE_WRITE) {
#if POLL_ENGINE == POLL_ENGINE_EVPORT
            /* Event port: always re-register the connection to the poller. */
            int _socket = _job->connection.socket;
            poller_add(poller, _socket, POLL_CODE_READ | POLL_CODE_ET, _job);
#endif
            return;
        }
        if (job_write(_job, _data->buffer, _data->buffer_size) == -1) {
            finish_job(_worker, _job, code);
            return;
        }
        continue_job(_worker, _job, code);
        return;
    }

    /* Log any unhandled code. */
    LOG_ERROR("unhandled poll code: %x\n", code);
}

/**
 * @copydoc continue_job
 */
static inline void continue_job(worker_t* worker, job_t* job, int code) {
    int _socket = job->connection.socket;

#if POLL_ENGINE == POLL_ENGINE_KQUEUE
    /* Kqueue: unused parameter. */
    (void)code;
#endif

    /* Is the write process done? */
    if (job_has_more_write(job)) {
#if POLL_ENGINE == POLL_ENGINE_UPOLL || POLL_ENGINE == POLL_ENGINE_EPOLL
        /* Only modify if needed. */
        if ((code & POLL_CODE_WRITE)) {
            return;
        }
        /* Unix poll & Epoll: modify for the next write to the poller. */
        code = POLL_CODE_READ | POLL_CODE_WRITE;
        poller_modify(&worker->poller, _socket, code | POLL_CODE_ET, job);

#elif POLL_ENGINE == POLL_ENGINE_KQUEUE
        /* Only register if needed. */
        if ((job->poll_code & POLL_CODE_WRITE)) {
            return;
        }
        /* Kqueue: register for the next write to the poller. */
        job->poll_code = POLL_CODE_READ | POLL_CODE_WRITE;
        poller_add(
            &worker->poller, _socket, POLL_CODE_WRITE | POLL_CODE_ET, job
        );

#elif POLL_ENGINE == POLL_ENGINE_EVPORT
        /* Event port: always re-register the connection to the poller. */
        code = POLL_CODE_READ | POLL_CODE_WRITE;
        poller_add(&worker->poller, _socket, code | POLL_CODE_ET, job);

#endif

        return;
    }

    /* Prepare for the next read process. */
    job_reset(job);

#if POLL_ENGINE == POLL_ENGINE_UPOLL || POLL_ENGINE == POLL_ENGINE_EPOLL
    /* Only modify if needed. */
    if ((code & POLL_CODE_WRITE) == 0) {
        return;
    }
    /* Epoll: modify for the next read only process to the poller. */
    poller_modify(&worker->poller, _socket, POLL_CODE_READ | POLL_CODE_ET, job);

#elif POLL_ENGINE == POLL_ENGINE_KQUEUE
    /* Only remove if needed. */
    if ((job->poll_code & POLL_CODE_WRITE) == 0) {
        return;
    }
    /* Kqueue: remove the write code from the poller. */
    job->poll_code = POLL_CODE_READ;
    poller_remove(&worker->poller, _socket, POLL_CODE_WRITE);

#elif POLL_ENGINE == POLL_ENGINE_EVPORT
    /* Event port: always re-register the connection to the poller. */
    poller_add(&worker->poller, _socket, POLL_CODE_READ | POLL_CODE_ET, job);

#endif
}

/**
 * @copydoc handle_job_done
 */
static inline void finish_job(worker_t* worker, job_t* job, int code) {
    data_t* _data = (data_t*)worker->data;

#if POLL_ENGINE == POLL_ENGINE_UPOLL || POLL_ENGINE == POLL_ENGINE_EPOLL
    /* Unix poll & Epoll: always remove the connection from the poller. */
    poller_remove(&worker->poller, job->connection.socket, code);

#elif POLL_ENGINE == POLL_ENGINE_KQUEUE
    /* Kqueue: unused parameter. */
    (void)code;

    /* Kqueue: remove each read and write code. */
    if (job->poll_code & POLL_CODE_READ) {
        poller_remove(&worker->poller, job->connection.socket, POLL_CODE_READ);
    }
    if (job->poll_code & POLL_CODE_WRITE) {
        poller_remove(&worker->poller, job->connection.socket, POLL_CODE_WRITE);
    }

#elif POLL_ENGINE == POLL_ENGINE_EVPORT
    /* Event ports: unused parameter. */
    (void)code;

#endif

    /* Cleanup and return the job back to the manager. */
    job_cleanup(job, _data->buffer, _data->buffer_size);
    manager_return_job(_data->manager, job);
}
