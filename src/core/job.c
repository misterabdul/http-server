#include "job.h"

#include <stdbool.h>
#include <string.h>

#include <misc/logger.h>

#define DEFAULT_TIMEOUT_RECEIVE       30
#define DEFAULT_TIMEOUT_SEND          30
#define DEFAULT_KERNEL_BUFFER_RECEIVE 1048576 /* 1MB */
#define DEFAULT_KERNEL_BUFFER_SEND    1048576 /* 1MB */

/**
 * @copydoc manager_init
 */
void manager_init(manager_t* manager, int max_job) {
    /* Initialize the job manager instance. */
    *manager = (manager_t){.objpool = (objpool_t){0}, .max_job = max_job};

    /* Initialize the object pool instance. */
    objpool_init(&manager->objpool);
}

/**
 * @copydoc manager_setup
 */
int manager_setup(manager_t* manager) {
    /* Setup and allocate the object pool instance. */
    objpool_t* _objpool = &manager->objpool;
    if (objpool_setup(_objpool) == -1) {
        return -1;
    }
    if (objpool_allocate(_objpool, manager->max_job, sizeof(job_t)) == -1) {
        return -1;
    }

    return 0;
}

/**
 * @copydoc manager_get_job
 */
job_t* manager_get_job(manager_t* manager) {
    return objpool_acquire(&manager->objpool);
}

/**
 * @copydoc manager_return_job
 */
void manager_return_job(manager_t* manager, job_t* job) {
    objpool_release(&manager->objpool, job);
}

/**
 * @copydoc manager_cleanup
 */
void manager_cleanup(manager_t* manager) {
    objpool_cleanup(&manager->objpool);
}

/**
 * @copydoc job_init
 */
void job_init(job_t* job, server_t* server, const char* root, size_t length) {
    /* Initial job instance's state. */
    *job = (job_t){
        .connection = (connection_t){0},
        .state = JOB_STATE_READ,
        .root_length = length,
        .http = (http_t){0},
        .sent_head = 0,
        .sent_body = 0,
        .sent_file = 0,
        .root = root,
    };

#if POLL_ENGINE == POLL_ENGINE_KQUEUE
    job->poll_code = 0x00;
#endif

    /* Initialize all the related instances. */
    connection_init(&job->connection, server);
    http_init(&job->http, root, length);
}

/**
 * @copydoc job_setup
 */
int job_setup(job_t* job) {
    return connection_setup(
        &job->connection,
        DEFAULT_TIMEOUT_RECEIVE,
        DEFAULT_KERNEL_BUFFER_RECEIVE,
        DEFAULT_TIMEOUT_SEND,
        DEFAULT_KERNEL_BUFFER_SEND
    );
}

/**
 * @copydoc job_handle_issue
 */
int job_handle_issue(job_t* job) {
    int _error = connection_get_error(&job->connection);
    LOG_ERROR("connection_get_error: %s (%d)\n", strerror(_error), _error);

    return _error;
}

/**
 * @copydoc job_read
 */
int job_read(job_t* job, char* buffer, size_t size) {
    bool _err_empty_recv = true;

    /* Establish the TLS connection if not established yet. */
    if (job->connection.ssl && !job->connection.tls_established) {
        if (connection_establish_tls(&job->connection) == -1) {
            return -1;
        }
        if (!job->connection.tls_established) {
            return 0;
        }
        /* Supress the error on empty receive just after the TLS established. */
        _err_empty_recv = false;
    }

    /* Reset the job instance if the previous write process wasn't done yet. */
    if (job->state == JOB_STATE_WRITE) {
        job_reset(job);
    }

    /* Get the raw request. */
    size_t _received = 0;
    if (connection_receive(&job->connection, buffer, size, &_received) == -1) {
        return -1;
    }
    if (_received == 0) {
        return _err_empty_recv ? -1 : 0;
    }

    /* Do the HTTP operation and prepare the HTTP response. */
    http_process(&job->http, buffer, _received);
    job->state = JOB_STATE_WRITE;

    return 0;
}

/**
 * @copydoc job_write
 */
int job_write(job_t* job, char* buffer, size_t size) {
    response_t* _res = &job->http.response;
    connection_t* _conn = &job->connection;

    /* Send the head first. */
    int _ret = connection_send(
        _conn, _res->head_buffer, _res->head_length, &job->sent_head
    );
    if (_ret == -1 || job->sent_head == 0) {
        return -1;
    }

    /* Send the corresponding body then. */
    if (job->http.response.type == RESPONSE_TYPE_FILE) {
        _ret = connection_sendfile(
            _conn,
            _res->file_fd,
            _res->file_stat.st_size,
            buffer,
            size,
            &job->sent_file
        );
        if (_ret == -1 || job->sent_file == 0) {
            return -1;
        }
    } else {
        _ret = connection_send(
            _conn, _res->body_buffer, _res->body_length, &job->sent_body
        );
        if (_ret == -1 || job->sent_body == 0) {
            return -1;
        }
    }

    /* Whether to close the connection after this function call or not. */
    if (job->http.should_close) {
        return -1;
    }

    return 0;
}

/**
 * @copydoc job_has_more_write
 */
bool job_has_more_write(job_t* job) {
    response_t* _res = &job->http.response;

    /* Count remaining data to send. */
    size_t _headrem = _res->head_length - job->sent_head;
    size_t _bodyrem = _res->body_length - job->sent_body;
    off_t _filerem = _res->file_stat.st_size - job->sent_file;

    switch (_res->type) {
        default:
            break;
        case RESPONSE_TYPE_STRING:
            return _headrem > 0 || _bodyrem > 0;
        case RESPONSE_TYPE_FILE:
            return _headrem > 0 || _filerem > 0;
    }

    return _headrem > 0;
}

/**
 * @copydoc job_reset
 */
void job_reset(job_t* job) {
    /* Cleanup the HTTP instance for the next HTTP process. */
    http_cleanup(&job->http);
    http_init(&job->http, job->root, job->root_length);

    /* Reset the state and counter. */
    job->state = JOB_STATE_READ;
    job->sent_head = 0;
    job->sent_body = 0;
    job->sent_file = 0;
}

/**
 * @copydoc job_cleanup
 */
void job_cleanup(job_t* job, char* buffer, size_t size) {
    /* Cleanup all the related instances. */
    connection_close(&job->connection, buffer, size);
    connection_cleanup(&job->connection);
    http_cleanup(&job->http);
}
