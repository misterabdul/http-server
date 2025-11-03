#ifndef job_h
#define job_h

#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>

#include <lib/objpool.h>
#include <lib/transport.h>
#include <misc/macro.h>

#include "http.h"

/**
 * @brief The state of the job.
 */
typedef enum job_state {
    JOB_STATE_READ = 0,
    JOB_STATE_WRITE = 1,
} job_state_t;

/**
 * @brief The job manager instance.
 */
typedef struct manager {
    /**
     * @brief Object pool instance to store all the job objects.
     */
    objpool_t objpool;

    /**
     * @brief Maximum number of the jobs to be managed.
     */
    int max_job;
} manager_t;

/**
 * @brief The job instance.
 */
typedef struct job {
    /**
     * @brief The transport connection to receive HTTP request from and send
     * HTTP response to.
     */
    connection_t connection;

    /**
     * @brief The HTTP instance for all HTTP related process.
     */
    http_t http;

    /**
     * @brief The current state of the job.
     */
    job_state_t state;

    /**
     * @brief The amount of HTTP header that already sent.
     */
    size_t sent_head;

    /**
     * @brief The amount of HTTP body that already sent.
     */
    size_t sent_body;

    /**
     * @brief The amount of the content of the file that already sent.
     */
    off_t sent_file;

    /**
     * @brief Root directory for the HTTP process.
     */
    const char* root;

    /**
     * @brief The length of the root directory.
     */
    size_t root_length;

#if POLL_ENGINE == POLL_ENGINE_KQUEUE
    /**
     * @brief Kqueue: the poll code associated with the current job.
     */
    int poll_code;
#endif

} job_t;

/**
 * @brief Initialize the job manager instance.
 *
 * @param[out] manager The job manager instance.
 * @param[in]  max_job The number of maximum jobs to be managed.
 */
void manager_init(manager_t* manager, int max_job);

/**
 * @brief Setup the job manager instance.
 *
 * @param[out] manager The job manager instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int manager_setup(manager_t* manager);

/**
 * @brief Get a new job instance from the job manager.
 *
 * @param[out] manager The job manager instance.
 *
 * @return A new job instance.
 */
job_t* manager_get_job(manager_t* manager);

/**
 * @brief Return the job instance back to the manager.
 *
 * @param[out] manager The job manager instance.
 * @param[in]  job     The job instance.
 */
void manager_return_job(manager_t* manager, job_t* job);

/**
 * @brief Clean all related stuff from the job manager instance.
 *
 * @param[out] manager The job manager instance.
 */
void manager_cleanup(manager_t* manager);

/**
 * @brief Initialize the job instance.
 *
 * The server instance and the root pointer will not be freed during the cleanup
 * process.
 *
 * @param[out] job    The job instance.
 * @param[in]  server The transport server related to the transport connection.
 * @param[in]  root   The root directory for HTTP related prcess.
 * @param[in]  length The length root directory.
 */
void job_init(job_t* job, server_t* server, const char* root, size_t length);

/**
 * @brief Setup the job instance.
 *
 * @param[out] job The job instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int job_setup(job_t* job);

/**
 * @brief Handle any issue related to the job.
 *
 * @param[in] job The job instance.
 *
 * @return Error code from the issue.
 */
int job_handle_issue(job_t* job);

/**
 * @brief Perform the read process of the job.
 *
 * The buffer is used to temporarily contain the received data from the
 * transport connection. After that the data will be parsed and processed for
 * the supposed HTTP response. Ignore the content of the buffer after this
 * function call. If this function returns an error you should finish and
 * cleanup the job.
 *
 * @param[out] job    The job instance.
 * @param[out] buffer The buffer for the operation.
 * @param[in]  size   The size of the buffer.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int job_read(job_t* job, char* buffer, size_t size);

/**
 * @brief Perform the write process of the job.
 *
 * The buffer is used to temporarily contain a portion of the data to send to
 * the transport connection. Ignore the content of the buffer after this
 * function call. If this function returns an error you should finish and
 * cleanup the job.
 *
 * @param[out] job    The job instance.
 * @param[out] buffer The buffer for the operation.
 * @param[in]  size   The size of the buffer.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int job_write(job_t* job, char* buffer, size_t size);

/**
 * @brief Check whether the job has more to write or not.
 *
 * @param[in] job The job instance.
 *
 * @return Check result, 1 if the job has more to write, 0 for not.
 */
bool job_has_more_write(job_t* job);

/**
 * @brief Reset the state of the job for the next work.
 *
 * @param[out] job The job instance.
 */
void job_reset(job_t* job);

/**
 * @brief Clean all related stuff from job instance.
 *
 * The buffer is used for transport connection related operation. Ignore the
 * content of the buffer after this function call. If `NULL` is passed as the
 * buffer, the operation will continue without using it.
 *
 * @param[out] job    The job instance.
 * @param[out] buffer The buffer for the operation.
 * @param[in]  size   The size of the buffer.
 */
void job_cleanup(job_t* job, char* buffer, size_t size);

#endif
