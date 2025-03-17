#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "listener.h"
#include "log.h"
#include "pipe.h"
#include "worker.h"

listener_t listener = {0};
worker_t workers[WORKER_COUNT] = {0};

void signal_handler(int signum);

int main(void) {
    pthread_t t_ids[WORKER_COUNT + 1] = {0};
    pthread_attr_t t_attrs[WORKER_COUNT + 1] = {0};

    if (listener_init(&listener) < 0) {
        return -1;
    }

    for (int i = 0; i < WORKER_COUNT; i++) {
        if (worker_init(&workers[i]) < 0) {
            return -1;
        }

        listener.worker_fds[i] = workers[i].pipe_fds[1];
        if (pthread_attr_init(&t_attrs[i]) < 0) {
            LOG_ERROR("failed to initialize worker pthread attribute: %s (%d)", strerror(errno), errno);
            return -1;
        }
        if (pthread_create(&t_ids[i], &t_attrs[i], worker_runner, (void *)&(workers[i])) < 0) {
            LOG_ERROR("failed to create worker pthread: %s (%d)", strerror(errno), errno);
            return -1;
        }
    }
    if (pthread_attr_init(&t_attrs[WORKER_COUNT]) < 0) {
        LOG_ERROR("failed to initialize listener pthread attribute: %s (%d)", strerror(errno), errno);
        return -1;
    }
    if (pthread_create(&t_ids[WORKER_COUNT], &t_attrs[WORKER_COUNT], listener_runner, (void *)&listener) < 0) {
        LOG_ERROR("failed to create listener pthread: %s (%d)", strerror(errno), errno);
        return -1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    for (int i = 0; i < WORKER_COUNT + 1; i++) {
        if (pthread_join(t_ids[i], NULL) < 0) {
            LOG_ERROR("failed to join pthread: %s (%d)", strerror(errno), errno);
            return -1;
        }
    }

    return 0;
}

void signal_handler(int signum) {
    pipe_message_t pipe_message = (pipe_message_t){.type = PIPE_SIGNAL, .data = &signum, .data_size = sizeof(int)};
    if (signum == SIGINT) {
        for (int i = 0; i < WORKER_COUNT; i++) {
            write(workers[i].pipe_fds[1], (const void *)&pipe_message, sizeof(pipe_message_t));
        }
        write(listener.pipe_fds[1], (const void *)&pipe_message, sizeof(pipe_message_t));
    }
}
