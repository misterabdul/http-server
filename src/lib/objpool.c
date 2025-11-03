#include "objpool.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <misc/logger.h>

/**
 * @brief Linked list object node representation.
 */
typedef struct node {
    /**
     * @brief The next object node in the list.
     */
    struct node* next;
} node_t;

/**
 * @copydoc objpool_init
 */
void objpool_init(objpool_t* objpool) {
    /* Initialize the object pool instance. */
    *objpool = (objpool_t){
        .block_memory = NULL,
        .next_free = NULL,
        .object_count = 0,
        .object_size = 0,
        .lock = (pthread_mutex_t){0},
    };
}

/**
 * @copydoc objpool_setup
 */
int objpool_setup(objpool_t* objpool) {
    /* Initialize the POSIX thread mutex. */
    int _ret = pthread_mutex_init(&objpool->lock, NULL);
    if (_ret != 0) {
        LOG_ERROR("pthread_mutex_init: %s (%d)\n", strerror(_ret), _ret);
        return -1;
    }

    return 0;
}

/**
 * @copydoc objpool_init
 */
int objpool_allocate(objpool_t* objpool, int count, size_t size) {
    /* Parameter validation. */
    if (count <= 0) {
        errno = EPERM;
        return -1;
    }
    if (size < sizeof(node_t)) {
        errno = EPERM;
        return -1;
    }

    /* Allocate a large continuous block of memory. */
    objpool->block_memory = malloc(count * size);
    if (objpool->block_memory == NULL) {
        LOG_ERROR("malloc: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    /* Initial head of the nodes. */
    objpool->next_free = objpool->block_memory;

    /* Arrange the large block of memory into pieces of node of singly
     * linked-list. Be careful with the pointer arithmetic. */
    node_t* _node;
    char *_current_block = objpool->block_memory, *_next_block;
    for (int _i = 0; _i < count - 1; _i++) {
        _node = (node_t*)_current_block;
        _next_block = _current_block + size;
        _node->next = (node_t*)_next_block;
        _current_block = _next_block;
    }
    _node = (node_t*)_current_block;
    _node->next = NULL;

    return 0;
}

/**
 * @copydoc objpool_acquire
 */
void* objpool_acquire(objpool_t* objpool) {
    /* Get a node for the object from the singly linked list. */
    pthread_mutex_lock(&objpool->lock);
    node_t* _node = (node_t*)objpool->next_free;
    if (_node != NULL) {
        objpool->next_free = _node->next;
    }
    pthread_mutex_unlock(&objpool->lock);
    return _node;
}

/**
 * @copydoc objpool_release
 */
void objpool_release(objpool_t* objpool, void* object) {
    /* Parameter validation. */
    if (object == NULL) {
        return;
    }

    /* Put the object as node back into the singly linked list. */
    pthread_mutex_lock(&objpool->lock);
    node_t* _node = (node_t*)object;
    _node->next = objpool->next_free;
    objpool->next_free = _node;
    pthread_mutex_unlock(&objpool->lock);
}

/**
 * @copydoc objpool_cleanup
 */
void objpool_cleanup(objpool_t* objpool) {
    int _ret = pthread_mutex_destroy(&objpool->lock);
    if (_ret != 0) {
        LOG_ERROR("pthread_mutex_destroy: %s (%d)\n", strerror(_ret), _ret);
    }
    free(objpool->block_memory);
}
