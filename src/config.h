#ifndef config_h
#define config_h

#define LISTEN_PORT         8000
#define MAX_CONNECTION      65535
#define REQUEST_BUFFER_SIZE 4096
#define PATH_BUFFER_SIZE    1024
#define MAX_HEADERS         128
#define ROOT_PATH           "www"

#define WORKER_COUNT 12

#define EPOLL_TIMEOUT 1000

#endif
