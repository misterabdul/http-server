#ifndef worker_h
#define worker_h

#include <sys/epoll.h>

#include "config.h"

typedef struct worker {
    int epoll_fd, pipe_fds[2];
    struct epoll_event events[MAX_CONNECTION];
    char buffer[REQUEST_BUFFER_SIZE];
    size_t buffer_size, event_size, event_count;
} worker_t;

int worker_init(worker_t *worker);

void *worker_runner(void *data);

#endif
