#include "hashmap.h"

#include <stdlib.h>
#include <string.h>

#include <misc/logger.h>

#include "objpool.h"

#define FNV1A_HASH32_PRIME        0x01000193
#define FNV1A_HASH32_OFFSET_BASIS 0x811c9dc5

/**
 * @brief The hash generator function.
 *
 * The hash implementation is based on FNV hash algorithm.
 * Reference:
 * https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
 *
 * @param[in] key      The key to be hashed.
 * @param[in] key_size The size of the key.
 *
 * @return The hash value of the key.
 */
static inline size_t hash(const void* key, size_t key_size);

/**
 * @brief Default key comparator.
 */
static bool default_key_comparator(
    const void* key_a, size_t key_size_a, const void* key_b, size_t key_size_b
);

/**
 * @copydoc map_init
 */
void map_init(map_t* map, int size) {
    /* Allocate and initialize the internal object pool instance. */
    objpool_t* _item_pool = malloc(sizeof(objpool_t));
    *_item_pool = (objpool_t){0};
    objpool_init(_item_pool);

    /* Initialize the map instance. */
    *map = (map_t){
        .items = calloc(size, sizeof(map_item_t*)),
        .data = _item_pool,
        .size = size,
        .count = 0,
    };
}

/**
 * @copydoc map_setup
 */
int map_setup(map_t* map) {
    /* Setup and allocate the internal object pool instance. */
    objpool_t* _item_pool = (objpool_t*)map->data;
    if (objpool_setup(_item_pool) == -1) {
        return -1;
    }
    if (objpool_allocate(_item_pool, map->size, sizeof(map_item_t))) {
        return -1;
    }

    return 0;
}

/**
 * @copydoc map_add
 */
int map_add(map_t* map, void* key, size_t key_size, void* value) {
    /* Parameter validation. */
    if (map->count >= map->size) {
        return -1;
    }

    /* Get the hash of the key and then get the item with the hash. */
    size_t _hash = hash(key, key_size) % map->size;
    map_item_t** _tracer = &map->items[_hash];

    /* Handle existing item (hash collision). */
    for (; *_tracer; _tracer = &(*_tracer)->next);

    /* Acquire new object from the pool. */
    objpool_t* _item_pool = (objpool_t*)map->data;
    *_tracer = objpool_acquire(_item_pool);
    if (*_tracer == NULL) {
        return -1;
    }

    /* Set the value of the newly acquired map item. */
    **_tracer = (map_item_t){
        .key_size = key_size,
        .value = value,
        .next = NULL,
        .key = key,
    };
    map->count++;

    return 0;
}

/**
 * @copydoc map_get
 */
void* map_get(
    map_t* map, void* key, size_t key_size, key_comparator_t key_comparator
) {
    /* Parameter validation. */
    if (map->count <= 0) {
        return NULL;
    }
    if (key_comparator == NULL) {
        key_comparator = default_key_comparator;
    }

    /* Get the hash of the key and then get the item with the hash. */
    size_t _hash = hash(key, key_size) % map->size;
    map_item_t* _item = map->items[_hash];

    /* Iterate through duplicate item with the same hash and return the item
     * with matched key. */
    for (; _item;) {
        if (key_comparator(_item->key, key_size, key, key_size)) {
            break;
        }
        _item = _item->next;
    }

    /* No item found with the given key. */
    return _item ? (void*)_item->value : NULL;
}

/**
 * @copydoc map_remove
 */
void map_remove(
    map_t* map, void* key, size_t key_size, key_comparator_t key_comparator
) {
    /* Parameter validation. */
    if (map->count <= 0) {
        return;
    }
    if (key_comparator == NULL) {
        key_comparator = default_key_comparator;
    }

    /* Get the hash of the key and then get the item with the hash. */
    size_t _hash = hash(key, key_size) % map->size;
    map_item_t** _tracer = &map->items[_hash];

    /* Iterate through duplicate item with the same hash to find the item
     * with the given key. */
    for (bool _ret; *_tracer; _tracer = &(*_tracer)->next) {
        _ret = key_comparator(
            (*_tracer)->key, (*_tracer)->key_size, key, key_size
        );
        if (_ret) {
            break;
        }
    }

    /* Proceed to delete if the item is found. */
    if (*_tracer == NULL) {
        return;
    }

    /* Release the object back to the pool. */
    objpool_t* _item_pool = (objpool_t*)map->data;
    map_item_t* _next = (*_tracer)->next;
    **_tracer = (map_item_t){0};
    objpool_release(_item_pool, *_tracer);
    *_tracer = _next;
    map->count--;
}

/**
 * @copydoc map_destroy
 */
void map_cleanup(map_t* map) {
    /* Cleanup and free all the internal data. */
    objpool_t* _item_pool = (objpool_t*)map->data;
    objpool_cleanup(_item_pool);
    free(_item_pool);
    free(map->items);
}

/**
 * @copydoc hash
 */
static inline size_t hash(const void* key, size_t key_size) {
    size_t _hash = FNV1A_HASH32_OFFSET_BASIS;
    char* _bytes = (char*)key;
    for (size_t _i = 0; _i < key_size; _i++) {
        _hash = (_hash ^ _bytes[_i]) * FNV1A_HASH32_PRIME;
    }

    return _hash;
}

/**
 * @copydoc default_key_comparator
 */
static bool default_key_comparator(
    const void* key_a, size_t key_size_a, const void* key_b, size_t key_size_b
) {
    if (key_size_a != key_size_b) {
        return false;
    }

    return memcmp(key_a, key_b, key_size_a) == 0;
}
