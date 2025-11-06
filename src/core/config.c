#include "config.h"

#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <misc/logger.h>

#define DEFAULT_WORKER_CNT 1
#define DEFAULT_MAX_CONN   255
#define DEFAULT_BUFF_SIZE  1048576 /* 1MB */
#define DEFAULT_ADDR4      "0.0.0.0"
#define DEFAULT_ADDR6      "::"
#define DEFAULT_IP6_ENABLE false
#define DEFAULT_SSL_ENABLE false
#define DEFAULT_PORT_HTTP  8080
#define DEFAULT_PORT_HTTPS 8443
#define DEFAULT_ROOT       "www"
#define DEFAULT_SSL_CERT   "fullchain.pem"
#define DEFAULT_SSL_PKEY   "privkey.pem"

/**
 * @brief Code used to represent the short code of long command line argument.
 */
typedef enum longopt {
    LONGOPT_WORKER_CNT = 0x100,
    LONGOPT_MAX_CONN = 0x101,
    LONGOPT_BUFF_SIZE = 0x102,
    LONGOPT_ADDR4 = 0x103,
    LONGOPT_ADDR6 = 0x104,
    LONGOPT_IP6_ENABLE = 0x105,
    LONGOPT_SSL_ENABLE = 0x106,
    LONGOPT_PORT_HTTP = 0x107,
    LONGOPT_PORT_HTTPS = 0x108,
    LONGOPT_ROOT = 0x109,
    LONGOPT_SSL_CERT = 0x10A,
    LONGOPT_SSL_PKEY = 0x10B,
} longopt_t;

/**
 * @brief Parsed value of all available command line argument.
 */
typedef struct opts {
    int worker_cnt;
    int max_conn;
    size_t buff_size;
    char addr4[CONFIG_ADDRESS_SIZE];
    char addr6[CONFIG_ADDRESS_SIZE];
    bool ip6_enable;
    bool ssl_enable;
    int port_http;
    int port_https;
    char root[CONFIG_PATH_SIZE];
    char ssl_cert[CONFIG_PATH_SIZE];
    char ssl_pkey[CONFIG_PATH_SIZE];
} opts_t;

/**
 * @brief Set the default value for each options.
 *
 * @param[out] opts The options instances.
 */
static inline void opts_default(opts_t* opts);

/**
 * @brief Parse the command line arguments for each options.
 *
 * @param[out] opts The options instance.
 * @param[in]  argc The number of arguments.
 * @param[in]  argv The array of command line arguments.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
static inline int opts_parse(opts_t* opts, int argc, char* argv[]);

/**
 * @brief Parse the string input to the integer output.
 *
 * @param[in]  str    The string input.
 * @param[out] result The resulting parsed integer.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
static inline int int_parse(const char* str, int* result);

/**
 * @brief Display the help information and exit the program.
 */
static inline void display_help(void);

/**
 * @copydoc config_get
 */
int config_get(config_t* config, int argc, char* argv[]) {
    opts_t _opts = (opts_t){0};
    opts_default(&_opts);

    int _ret = opts_parse(&_opts, argc, argv);
    if (_ret == -1) {
        return -1;
    }

    char _real_root[CONFIG_PATH_SIZE];
    if (realpath(_opts.root, _real_root) == NULL) {
        LOG_ERROR("realpath: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    /* Init config. */
    *config = (config_t){
        .listeners = malloc(2 * sizeof(listener_config_t)),
        .worker_count = _opts.worker_cnt,
        .worker = (worker_config_t){0},
        .max_job = _opts.max_conn + 2,
        .listener_count = 1,
    };

    /* Init worker config. */
    config->worker = (worker_config_t){
        .max_job = (_opts.max_conn / _opts.worker_cnt) + 1,
        .buffer_size = _opts.buff_size,
    };

    /** Init HTTP listener config. */
    listener_config_t* _http_lt = &config->listeners[0];
    *_http_lt = (listener_config_t){
        .buffer_size = _opts.buff_size,
        .port = _opts.port_http,
        .max = _opts.max_conn,
        .certificate = {0},
        .private_key = {0},
        .secure = 0,
    };
    _ret = snprintf(_http_lt->root, CONFIG_PATH_SIZE, "%s", _real_root);
    if (_ret < 0) {
        LOG_ERROR("snprintf: %s (%d)\n", strerror(errno), errno);
        return -1;
    } else {
        _http_lt->root_length = _ret;
    }

    if (_opts.ip6_enable) {
        _http_lt->family = AF_INET6;
        _ret =
            snprintf(_http_lt->address, CONFIG_ADDRESS_SIZE, "%s", _opts.addr6);
        if (_ret < 0) {
            LOG_ERROR("snprintf: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
    } else {
        _http_lt->family = AF_INET;
        _ret =
            snprintf(_http_lt->address, CONFIG_ADDRESS_SIZE, "%s", _opts.addr4);
        if (_ret < 0) {
            LOG_ERROR("snprintf: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
    }

    if (!_opts.ssl_enable) {
        return 0;
    } else {
        config->listener_count++;
    }

    /** Init HTTPS listener config. */
    listener_config_t* _https_lt = &config->listeners[1];
    *_https_lt = (listener_config_t){
        .buffer_size = _opts.buff_size,
        .port = _opts.port_https,
        .max = _opts.max_conn,
        .certificate = {0},
        .private_key = {0},
        .secure = 1,
    };
    _ret = snprintf(_https_lt->root, CONFIG_PATH_SIZE, "%s", _real_root);
    if (_ret < 0) {
        LOG_ERROR("snprintf: %s (%d)\n", strerror(errno), errno);
        return -1;
    } else {
        _https_lt->root_length = _ret;
    }
    _ret = snprintf(
        _https_lt->certificate, CONFIG_PATH_SIZE, "%s", _opts.ssl_cert
    );
    if (_ret < 0) {
        LOG_ERROR("snprintf: %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    _ret = snprintf(
        _https_lt->private_key, CONFIG_PATH_SIZE, "%s", _opts.ssl_pkey
    );
    if (_ret < 0) {
        LOG_ERROR("snprintf: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    if (_opts.ip6_enable) {
        _https_lt->family = AF_INET6;
        _ret = snprintf(
            _https_lt->address, CONFIG_ADDRESS_SIZE, "%s", _opts.addr6
        );
        if (_ret < 0) {
            LOG_ERROR("snprintf: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
    } else {
        _https_lt->family = AF_INET;
        _ret = snprintf(
            _https_lt->address, CONFIG_ADDRESS_SIZE, "%s", _opts.addr4
        );
        if (_ret < 0) {
            LOG_ERROR("snprintf: %s (%d)\n", strerror(errno), errno);
            return -1;
        }
    }

    return 0;
}

/**
 * @copydoc config_cleanup
 */
void config_cleanup(config_t* config) {
    free(config->listeners);
}

/**
 * @copydoc opts_default
 */
static inline void opts_default(opts_t* opts) {
    /* Integer defaults. */
    *opts = (opts_t){
        .worker_cnt = DEFAULT_WORKER_CNT,
        .max_conn = DEFAULT_MAX_CONN,
        .buff_size = DEFAULT_BUFF_SIZE,
        .ip6_enable = DEFAULT_IP6_ENABLE,
        .ssl_enable = DEFAULT_SSL_ENABLE,
        .port_http = DEFAULT_PORT_HTTP,
        .port_https = DEFAULT_PORT_HTTPS,
    };

    /* String defaults. */
    strncpy(opts->addr4, DEFAULT_ADDR4, CONFIG_ADDRESS_SIZE);
    strncpy(opts->addr6, DEFAULT_ADDR6, CONFIG_ADDRESS_SIZE);
    strncpy(opts->root, DEFAULT_ROOT, CONFIG_PATH_SIZE);
    strncpy(opts->ssl_cert, DEFAULT_SSL_CERT, CONFIG_PATH_SIZE);
    strncpy(opts->ssl_pkey, DEFAULT_SSL_PKEY, CONFIG_PATH_SIZE);
}

/**
 * @copydoc opts_parse
 */
static inline int opts_parse(opts_t* opts, int argc, char* argv[]) {
    /* Describe the long command line options and its corresponding short code.
     * This array should be terminated with a zeroed struct. */
    struct option _longopts[] = {
        {"help", no_argument, NULL, 'h'},
        {"worker", required_argument, NULL, LONGOPT_WORKER_CNT},
        {"connection", required_argument, NULL, LONGOPT_MAX_CONN},
        {"buffer", required_argument, NULL, LONGOPT_BUFF_SIZE},
        {"ip4-address", required_argument, NULL, LONGOPT_ADDR4},
        {"ip6-address", required_argument, NULL, LONGOPT_ADDR6},
        {"ip6-enable", no_argument, NULL, LONGOPT_IP6_ENABLE},
        {"ssl-enable", no_argument, NULL, LONGOPT_SSL_ENABLE},
        {"http-port", required_argument, NULL, LONGOPT_PORT_HTTP},
        {"https-port", required_argument, NULL, LONGOPT_PORT_HTTPS},
        {"root-path", required_argument, NULL, LONGOPT_ROOT},
        {"ssl-certificate-path", required_argument, NULL, LONGOPT_SSL_CERT},
        {"ssl-private-key-path", required_argument, NULL, LONGOPT_SSL_PKEY},
        {0},
    };

    /* Iterate to parse each argument into its corresponding option. */
    for (int _ret = 1, _parsed; _ret != -1;) {
        _ret = getopt_long(argc, argv, "h", _longopts, NULL);

        switch (_ret) {
            default:
                break;
            case 'h':
                display_help();
                errno = EINVAL;
                return -1;

            case LONGOPT_WORKER_CNT:
                if (int_parse(optarg, &_parsed) == -1) {
                    fprintf(stderr, "Invalid number of workers: %s\n", optarg);
                    errno = EINVAL;
                    return -1;
                }
                opts->worker_cnt = _parsed;
                break;

            case LONGOPT_MAX_CONN:
                if (int_parse(optarg, &_parsed) == -1) {
                    fprintf(stderr, "Invalid max connections: %s\n", optarg);
                    errno = EINVAL;
                    return -1;
                }
                opts->max_conn = _parsed;
                break;

            case LONGOPT_BUFF_SIZE:
                if (int_parse(optarg, &_parsed) == -1) {
                    fprintf(stderr, "Invalid buffer size: %s\n", optarg);
                    errno = EINVAL;
                    return -1;
                }
                opts->buff_size = (size_t)_parsed;
                break;

            case LONGOPT_ADDR4:
                _ret = snprintf(opts->addr4, CONFIG_ADDRESS_SIZE, "%s", optarg);
                if (_ret < 0) {
                    fprintf(stderr, "Invalid IPv4 address: %s\n", optarg);
                    errno = EINVAL;
                    return -1;
                }
                break;

            case LONGOPT_ADDR6:
                _ret = snprintf(opts->addr6, CONFIG_ADDRESS_SIZE, "%s", optarg);
                if (_ret < 0) {
                    fprintf(stderr, "Invalid IPv6 address: %s\n", optarg);
                    errno = EINVAL;
                    return -1;
                }
                break;

            case LONGOPT_IP6_ENABLE:
                opts->ip6_enable = true;
                break;

            case LONGOPT_SSL_ENABLE:
                opts->ssl_enable = true;
                break;

            case LONGOPT_PORT_HTTP:
                if (int_parse(optarg, &_parsed) == -1) {
                    fprintf(stderr, "Invalid HTTP port: %s\n", optarg);
                    errno = EINVAL;
                    return -1;
                }
                opts->port_http = _parsed;
                break;
            case LONGOPT_PORT_HTTPS:
                if (int_parse(optarg, &_parsed) == -1) {
                    fprintf(stderr, "Invalid HTTPS port: %s\n", optarg);
                    errno = EINVAL;
                    return -1;
                }
                opts->port_https = _parsed;
                break;

            case LONGOPT_ROOT:
                _ret = snprintf(opts->root, CONFIG_PATH_SIZE, "%s", optarg);
                if (_ret < 0) {
                    fprintf(stderr, "Invalid root path: %s\n", optarg);
                    errno = EINVAL;
                    return -1;
                }
                break;

            case LONGOPT_SSL_CERT:
                _ret = snprintf(opts->ssl_cert, CONFIG_PATH_SIZE, "%s", optarg);
                if (_ret < 0) {
                    fprintf(
                        stderr, "Invalid SSL certificate path: %s\n", optarg
                    );
                    errno = EINVAL;
                    return -1;
                }
                break;

            case LONGOPT_SSL_PKEY:
                _ret = snprintf(opts->ssl_pkey, CONFIG_PATH_SIZE, "%s", optarg);
                if (_ret < 0) {
                    fprintf(
                        stderr, "Invalid SSL private key path: %s\n", optarg
                    );
                    errno = EINVAL;
                    return -1;
                }
                break;
        }
    }

    return 0;
}

/**
 * @copydoc int_parse
 */
static inline int int_parse(const char* str, int* result) {
    char* _endptr;
    int _parsed = strtol(str, &_endptr, 10);
    if (_endptr == str || *_endptr != '\0' || _parsed <= 0) {
        return -1;
    }
    *result = _parsed;
    return 0;
}

/**
 * @copydoc display_help
 */
static inline void display_help(void) {
    FILE* _output = stderr;

    fprintf(_output, "Usage: http-server [options]\n");
    fprintf(_output, "Options:\n");
    fprintf(
        _output,
        "  --help, -h              "
        "Display this help message\n"
    );
    fprintf(
        _output,
        "  --worker                "
        "Set the number of worker thread, default: %d\n",
        DEFAULT_WORKER_CNT
    );
    fprintf(
        _output,
        "  --connection            "
        "Set the maximum number of connections, default: %d\n",
        DEFAULT_MAX_CONN
    );
    fprintf(
        _output,
        "  --buffer                "
        "Set the buffer size for the request, default: %d\n",
        DEFAULT_BUFF_SIZE
    );
    fprintf(
        _output,
        "  --ip4-address           "
        "Set the IPv4 address, default: \"%s\"\n",
        DEFAULT_ADDR4
    );
    fprintf(
        _output,
        "  --ip6-address           "
        "Set the IPv6 address, default: \"%s\"\n",
        DEFAULT_ADDR6
    );
    fprintf(
        _output,
        "  --ip6-enable            "
        "Enable the IPv6 mode, default: disabled\n"
    );
    fprintf(
        _output,
        "  --http-port             "
        "Set the HTTP port, default: %d\n",
        DEFAULT_PORT_HTTP
    );
    fprintf(
        _output,
        "  --https-port            "
        "Set the HTTPS port, default: %d\n",
        DEFAULT_PORT_HTTPS
    );
    fprintf(
        _output,
        "  --root-path             "
        "Set the root path directory, default: \"%s\"\n",
        DEFAULT_ROOT
    );
    fprintf(
        _output,
        "  --ssl-certificate-path  "
        "Set the SSL certificate file path, default: \"%s\"\n",
        DEFAULT_SSL_CERT
    );
    fprintf(
        _output,
        "  --ssl-private-key-path  "
        "Set the SSL private key file path, default: \"%s\"\n",
        DEFAULT_SSL_PKEY
    );
}
