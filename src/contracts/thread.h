#ifndef contract_thread_h
#define contract_thread_h

#include <stddef.h>

/**
 * @brief Thread's pipe message type.
 */
typedef enum message_type {
    MESSAGE_SIGNAL = 0,
    MESSAGE_DATA = 1,
} message_type_t;

/**
 * @brief Thread's pipe message data structure.
 */
typedef struct pipe_message {
    message_type_t type;
    size_t data_size;
    void *data;
} pipe_message_t;

/**
 * @brief Thread runner function for pthread routine.
 *
 * @param data User data from the pthread.
 *
 * @return Data for pthread return value.
 */
void *platform_thread_runner(void *data);

#endif
