#ifndef config_h
#define config_h

#include <stddef.h>

#define CONFIG_ADDRESS_SIZE 40
#define CONFIG_PATH_SIZE    1024

/**
 * @brief Essentials configuration values for listener.
 */
typedef struct listener_config {
    /**
     * @brief Secured listener via TLS.
     */
    int secure;

    /**
     * @brief Address family (e.g.: AF_INET for IPv4 or AF_INET6 for IPv6).
     */
    int family;

    /**
     * @brief Address port.
     */
    int port;

    /**
     * @brief Maximum number of connection that could be handled.
     */
    int max;

    /**
     * @brief The actual address of the listener (e.g.: 0.0.0.0 or ::).
     */
    char address[CONFIG_ADDRESS_SIZE];

    /**
     * @brief The root path directory.
     */
    char root[CONFIG_PATH_SIZE];

    /**
     * @brief The certificate file path.
     */
    char certificate[CONFIG_PATH_SIZE];

    /**
     * @brief The private key file path.
     */
    char private_key[CONFIG_PATH_SIZE];

    /**
     * @brief The length of the root path directory.
     */
    size_t root_length;

    /**
     * @brief The size of the buffer for the listener.
     */
    size_t buffer_size;
} listener_config_t;

/**
 * @brief Essentials configuration values for worker.
 */
typedef struct worker_config {
    /**
     * @brief Maximum number of job that could be handled.
     */
    int max_job;

    /**
     * @brief The size of the buffer for the worker.
     */
    size_t buffer_size;
} worker_config_t;

/**
 * @brief Main configuration values.
 */
typedef struct config {
    /**
     * @brief Array of listener configs.
     */
    struct listener_config* listeners;

    /**
     * @brief Worker config.
     */
    struct worker_config worker;

    /**
     * @brief Number of listeners.
     */
    int listener_count;

    /**
     * @brief Number of workers.
     */
    int worker_count;

    /**
     * @brief Maximum number of jobs for the job manager.
     */
    int max_job;
} config_t;

/**
 * @brief Get the configuration data from the command line arguments.
 *
 * @param[out] config The configuration data instance
 * @param[in]  argc   The number of command line arguments.
 * @param[in]  argv   The array of command line arguments.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int config_get(config_t* config, int argc, char* argv[]);

/**
 * @brief Clean all the related stuff from the configuration data.
 *
 * @param[out] config The configuration data instance.
 */
void config_cleanup(config_t* config);

#endif
