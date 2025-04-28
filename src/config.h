#ifndef config_h
#define config_h

#define LISTEN_PORT    8000
#define MAX_CONNECTION 65535
#define MAX_HEADERS    128
#define ROOT_PATH      "www"

#define PATH_BUFFER_SIZE 1024

#define WORKER_COUNT       24
#define WORKER_BUFFER_SIZE 4096

#define EPOLL_TIMEOUT 1000

#endif
