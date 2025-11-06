#include "listener.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <misc/logger.h>
#include <misc/macro.h>

#include "job.h"

/**
 * @brief All the private data for listener related operation.
 */
typedef struct data {
    /**
     * @brief Connection instance buffer for rejecting new connection.
     */
    connection_t connection;

    /**
     * @brief Raw buffer for closing a rejected connection.
     */
    char* buffer;

    /**
     * @brief Size of the raw buffer.
     */
    size_t buffer_size;

    /**
     * @brief Job manager instance.
     */
    manager_t* manager;

    /**
     * @brief Array of workers instance.
     */
    worker_t* workers;

    /**
     * @brief Number of workers.
     */
    int worker_count;

    /**
     * @brief Current cycle for worker.
     */
    int worker_cycle;

    /**
     * @brief Configuration data.
     */
    listener_config_t* config;

    /**
     * @brief Next job instance.
     */
    job_t* next_job;
} data_t;

/**
 * @brief The event handler for the listener's poller.
 *
 * @param[in] poller The poller instance.
 * @param[in] code   The received poll's code.
 * @param[in] data   The received poll's data .
 */
static void on_event(poller_t* poller, int code, void* data);

/**
 * @brief The stop handler for the listener's poller.
 *
 * @param[in] poller The poller instance.
 */
static void on_stop(poller_t* poller);

/**
 * @brief Accept the new incoming connection from the transport server.
 *
 * After this function call, the job instance is assigned to the worker. Don't
 * use it again.
 *
 * @param[in] listener The listener instance.
 * @param[in] job      The job instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
static inline int accept_conn(server_t* server, data_t* data, job_t* job);

/**
 * @brief Reject the new incoming connection from the transport server.
 *
 * @param[in] listener The listener instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
static inline int reject_conn(server_t* server, data_t* data);

/**
 * @copydoc listener_init
 */
void listener_init(
    listener_t* listener,
    listener_config_t* config,
    manager_t* manager,
    worker_t* workers,
    int worker_count
) {
    /* Allocate private data. */
    data_t* _data = malloc(sizeof(data_t));
    *_data = (data_t){
        .buffer = malloc(config->buffer_size * sizeof(char)),
        .next_job = manager_get_job(manager),
        .buffer_size = config->buffer_size,
        .connection = (connection_t){0},
        .worker_count = worker_count,
        .manager = manager,
        .workers = workers,
        .worker_cycle = 0,
        .config = config,
    };

    /* Initialize listener instance. */
    *listener = (listener_t){
        .poller = (poller_t){0},
        .server = (server_t){0},
        .data = _data,
    };

    /* Initialize other related instances. */
    poller_init(&listener->poller, on_event, on_stop, 2, listener);
    server_init(
        &listener->server,
        config->family,
        config->address,
        config->port,
        config->max
    );
    connection_init(&_data->connection, &listener->server);
}

/**
 * @copydoc listener_setup
 */
int listener_setup(listener_t* listener) {
    /* Setup the poller and transport server. */
    poller_t* _poller = &listener->poller;
    if (poller_setup(_poller) == -1) {
        return -1;
    }
    server_t* _server = &listener->server;
    if (server_setup(_server) == -1) {
        return -1;
    }

    /* Enable SSL if needed. */
    data_t* _data = (data_t*)listener->data;
    listener_config_t* _config = _data->config;
    if (_config->secure) {
        int _ret = server_enable_ssl(
            _server, _config->certificate, _config->private_key
        );
        if (_ret == -1) {
            return -1;
        }
    }

    /* Register the transport server to the poller. */
    int _code = POLL_CODE_READ | POLL_CODE_ET;
    if (poller_add(_poller, _server->socket, _code, _server) == -1) {
        return -1;
    }

    return 0;
}

/**
 * @copydoc listener_run
 */
int listener_run(listener_t* listener) {
    return poller_run(&listener->poller);
}

/**
 * @copydoc listener_wait
 */
int listener_wait(listener_t* listener) {
    return poller_wait(&listener->poller);
}

/**
 * @copydoc listener_stop
 */
int listener_stop(listener_t* listener) {
    return poller_stop(&listener->poller);
}

/**
 * @copydoc listener_cleanup
 */
void listener_cleanup(listener_t* listener) {
    /* Clean the transport server and the poller. */
    server_cleanup(&listener->server);
    poller_cleanup(&listener->poller);

    /* Return the next job buffer. */
    data_t* _data = (data_t*)listener->data;
    manager_return_job(_data->manager, _data->next_job);

    /* Free the private memory. */
    free(_data->buffer);
    free(_data);
}

/**
 * @copydoc on_event
 */
static void on_event(poller_t* poller, int code, void* data) {
    listener_t* _listener = (listener_t*)poller->context;
    data_t* _data = (data_t*)_listener->data;
    server_t* _server = (server_t*)data;
    job_t* _job = _data->next_job;

    for (;;) {
        /* Not interested on other code but read. */
        if ((code & POLL_CODE_READ) == 0) {
            break;
        }

        /* Accept or reject new connection based on the availability of job. */
        if (_job) {
            if (accept_conn(_server, _data, _job) == 0) {
                _job = manager_get_job(_data->manager);
                continue;
            }
        } else {
            if (reject_conn(_server, _data) == 0) {
                _job = manager_get_job(_data->manager);
                continue;
            }
        }

        /* No more new connection. */
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("unhandled error: %s (%d)\n", strerror(errno), errno);
        }
        break;
    }

#if POLL_ENGINE == POLL_ENGINE_EVPORT
    /* Event port: always re-register the server to the poller. */
    poller_add(poller, _server->socket, code, _server);
#endif

    _data->next_job = _job;
}

/**
 * @copydoc on_stop
 */
static void on_stop(poller_t* poller) {
    /* Close the transport server. */
    listener_t* _listener = (listener_t*)poller->context;
    server_close(&_listener->server);

    /* Clean the connection buffer. */
    data_t* _data = (data_t*)_listener->data;
    connection_cleanup(&_data->connection);
}

/**
 * @copydoc accept_conn
 */
static inline int accept_conn(server_t* server, data_t* data, job_t* job) {
    /* Start the job. */
    job_init(job, server, data->config->root, data->config->root_length);
    if (server_accept(server, &job->connection) == -1) {
        job_cleanup(job, data->buffer, data->buffer_size);
        return -1;
    }
    if (job_setup(job) == -1) {
        job_cleanup(job, data->buffer, data->buffer_size);
        return -1;
    }

    /* Assign job to the worker in a Round-Robin fashion. */
    worker_t* _worker;
    for (int _cycle = data->worker_cycle;;) {
        _worker = &data->workers[_cycle];
        _cycle = (_cycle + 1) % data->worker_count;
        if (worker_assign(_worker, job) == -1) {
            continue;
        }
        data->worker_cycle = _cycle;
        break;
    }

    return 0;
}

/**
 * @copydoc reject_conn
 */
static inline int reject_conn(server_t* server, data_t* data) {
    /* Accept and quickly close new connection. */
    int _ret = server_accept(server, &data->connection);
    if (_ret == 0) {
        connection_close(&data->connection, data->buffer, data->buffer_size);
    }

    /* Clean up and initialize the connection buffer for the next rejection. */
    connection_cleanup(&data->connection);
    connection_init(&data->connection, server);

    return _ret;
}
