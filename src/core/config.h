#ifndef config_h
#define config_h

#include <netinet/in.h>

#define HEADER_BUFFER_SIZE 128
#define PATH_BUFFER_SIZE   1024
#define WORKER_BUFFER_SIZE 4096
#define EPOLL_TIMEOUT      1000
#define WORKER_COUNT       12

#define MAX_CONNECTION 65535
#define IP4_ADDRESS    "0.0.0.0"
#define IP6_ADDRESS    "::"
#define IP6_ENABLE     1
#define HTTP_PORT      8080
#define HTTPS_PORT     8443

#define ROOT_PATH "www"

/**
 * @brief Essentials configuration values for TCP server.
 */
typedef struct server_config {
    char root_path[128];
    int family, port;
    char address[16];
} server_config_t;

/**
 * @brief Common configuration values.
 */
typedef struct config {
    int max_connection, listener_count, worker_count;
    struct server_config *servers;
} config_t;

/**
 * @brief Get the configuration data.
 *
 * @param[out] config The configuration data.
 */
void get_config(config_t *config);

#endif
