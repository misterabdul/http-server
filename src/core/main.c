#include <errno.h>
#include <signal.h>
#include <stdlib.h>

#include <misc/logger.h>

#include "config.h"
#include "listener.h"
#include "worker.h"

/**
 * @brief Global main configuration.
 */
static config_t g_config;

/**
 * @brief Global listener instances.
 */
static listener_t* g_listeners;

/**
 * @brief Global worker instances.
 */
static worker_t* g_workers;

/**
 * @brief The signal handler function.
 *
 * @param[in] signum The signal number.
 */
static void signal_handler(int signum);

/**
 * @brief The main function.
 *
 * @param[in] argc Number of command line arguments.
 * @param[in] argv The array of command line arguments.
 *
 * @return The result code of the program.
 */
int main(int argc, char* argv[]) {
    /* Initialize all the SSL related stuff. */
    ssl_init();

    /* Parse argument and get the config. */
    if (config_get(&g_config, argc, argv) == -1) {
        return errno;
    }

    /* Initialize job manager instance. */
    manager_t _manager;
    manager_init(&_manager, g_config.max_job);
    if (manager_setup(&_manager) == -1) {
        return errno;
    }

    /* Dispatch the workers. */
    g_workers = malloc(g_config.worker_count * sizeof(worker_t));
    for (int _i = 0; _i < g_config.worker_count; _i++) {
        worker_init(&g_workers[_i], &g_config.worker, &_manager);
        if (worker_setup(&g_workers[_i]) == -1) {
            return errno;
        }
        if (worker_run(&g_workers[_i]) == -1) {
            return errno;
        }
    }

    /* Dispatch the listeners. */
    g_listeners = malloc(g_config.listener_count * sizeof(listener_t));
    for (int _i = 0; _i < g_config.listener_count; _i++) {
        listener_init(
            &g_listeners[_i],
            &g_config.listeners[_i],
            &_manager,
            g_workers,
            g_config.worker_count
        );
        if (listener_setup(&g_listeners[_i]) == -1) {
            return errno;
        }
        if (listener_run(&g_listeners[_i]) == -1) {
            return errno;
        }
    }

    /* Register signal handling. */
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Wait for all listeners to stop. */
    for (int _i = 0; _i < g_config.listener_count; _i++) {
        if (listener_wait(&g_listeners[_i]) == -1) {
            return errno;
        }
        listener_cleanup(&g_listeners[_i]);
    }
    free(g_listeners);

    /* Wait for all workers to stop. */
    for (int _i = 0; _i < g_config.worker_count; _i++) {
        if (worker_wait(&g_workers[_i]) == -1) {
            return errno;
        }
        worker_cleanup(&g_workers[_i]);
    }
    free(g_workers);

    /* Perform cleanups. */
    manager_cleanup(&_manager);
    config_cleanup(&g_config);
    ssl_cleanup();

    return 0;
}

/**
 * @copydoc signal_handler
 */
static void signal_handler(int signum) {
    if (signum == SIGINT) {
        /* Gracefully stop all the listeners. */
        for (int _i = 0; _i < g_config.listener_count; _i++) {
            listener_stop(&g_listeners[_i]);
        }

        /* Gracefully stop all the workers. */
        for (int _i = 0; _i < g_config.worker_count; _i++) {
            worker_stop(&g_workers[_i]);
        }
    }
}
