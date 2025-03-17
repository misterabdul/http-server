#ifndef listener_h
#define listener_h

#include <sys/epoll.h>

#include "config.h"

typedef struct listener {
    int epoll_fd, pipe_fds[2], worker_fds[WORKER_COUNT], listen_sock, worker_cycle;
    struct epoll_event events[2];
} listener_t;

int listener_init(listener_t *listener);

void *listener_runner(void *data);

#endif
