#ifndef pipe_h
#define pipe_h

#include <stddef.h>

typedef enum pipe_message_type {
    PIPE_SIGNAL = 0,
    PIPE_CONNECTION = 1,
} pipe_message_type_t;

typedef struct pipe_message {
    pipe_message_type_t type;
    void *data;
    size_t data_size;
} pipe_message_t;

#endif
