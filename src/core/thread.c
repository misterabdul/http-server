#include "thread.h"

#include <contracts/thread.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "log.h"

/**
 * Run the thread in a separate thread.
 */
int thread_run(thread_t *thread) {
    if (pthread_create(&thread->id, &thread->attr, platform_thread_runner, (void *)thread) < 0) {
        LOG_ERROR("failed to create pthread: %s (%d)", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Wait until the thread stopped running.
 */
int thread_wait(thread_t *thread) {
    if (pthread_join(thread->id, NULL) < 0) {
        LOG_ERROR("failed to join pthread: %s (%d)", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Try to stop the thread.
 */
int thread_stop(thread_t *thread, int signum) {
    size_t _data = signum;

    pipe_message_t _message = (pipe_message_t){
        .type = MESSAGE_SIGNAL,
        .data_size = sizeof(int),
        .data = (void *)_data,
    };

    if (write(thread->pipe_fds[1], (const void *)&_message, sizeof(pipe_message_t)) < 0) {
        LOG_ERROR("failed to send pipe message: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

/**
 * Send custom data to the thread.
 */
int thread_send(thread_t *thread, void *data, size_t size) {
    pipe_message_t _message = (pipe_message_t){
        .type = MESSAGE_DATA,
        .data_size = size,
        .data = data,
    };

    if (write(thread->pipe_fds[1], (const void *)&_message, sizeof(pipe_message_t)) < 0) {
        LOG_ERROR("failed to send pipe message: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

