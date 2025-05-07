#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/**
 * Read the configuration from macro definitions.
 */
void get_config(config_t *config) {
    *config = (config_t){
        .max_connection = MAX_CONNECTION,
        .listener_count = 2,
        .worker_count = WORKER_COUNT,
        .servers = (server_config_t *)(config + sizeof(config_t)),
    };

#if IP6_ENABLE == 0
    /** IPv4 HTTP config */
    config->servers[0] = (server_config_t){.family = AF_INET, .port = HTTP_PORT};
    strcpy(config->servers[0].root_path, ROOT_PATH);
    strcpy(config->servers[0].address, IP4_ADDRESS);

    /** IPv4 HTTPS config */
    config->servers[1] = (server_config_t){.family = AF_INET, .port = HTTPS_PORT};
    strcpy(config->servers[1].root_path, ROOT_PATH);
    strcpy(config->servers[1].address, IP4_ADDRESS);
#else
    /** IPv6 HTTP config */
    config->servers[0] = (server_config_t){.family = AF_INET6, .port = HTTP_PORT};
    strcpy(config->servers[0].root_path, ROOT_PATH);
    strcpy(config->servers[0].address, IP6_ADDRESS);

    /** IPv6 HTTPS config */
    config->servers[1] = (server_config_t){.family = AF_INET6, .port = HTTPS_PORT};
    strcpy(config->servers[1].root_path, ROOT_PATH);
    strcpy(config->servers[1].address, IP6_ADDRESS);
#endif
}
