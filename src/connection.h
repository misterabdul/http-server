#ifndef connection_h
#define connection_h

#include <netinet/in.h>

#include "config.h"

typedef struct connection {
    struct sockaddr_in address;
    int socket, response_status, is_done;
    char response_file[PATH_BUFFER_SIZE];
} connection_t;

int connection_accept(connection_t **connection, int listen_socket);

int connection_receive(connection_t *connection, char *buffer, size_t buffer_size, int flags);

int connection_respond(connection_t *connection, char *buffer, size_t buffer_size, int flags);

int connection_error(connection_t *connection);

void connection_close(connection_t **connection, char *buffer, size_t buffer_size);

#endif
