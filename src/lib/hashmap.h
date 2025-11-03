#ifndef lib_hashmap_h
#define lib_hashmap_h

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Map item representation.
 */
typedef struct map_item {
    /**
     * @brief The next items having the same hash.
     */
    struct map_item* next;

    /**
     * @brief The key of the item.
     */
    void* key;

    /**
     * @brief The size of the key.
     */
    size_t key_size;

    /**
     * @brief The pointer for the value.
     */
    void* value;
} map_item_t;

/**
 * @brief Map representation.
 */
typedef struct map {
    map_item_t** items;

    /**
     * @brief The number of the items in the map.
     */
    size_t count;

    /**
     * @brief The size of the items.
     */
    size_t size;

    void* data;
} map_t;

/**
 * @brief Type to represent the key comparator function.
 *
 * @param[in] key_a      The first key to be compared.
 * @param[in] key_size_a The size of the first key.
 * @param[in] key_b      The second key to be compared.
 * @param[in] key_size_b The size of the second key.
 *
 * @return Comparation result, true if both keys are equal, false otherwise.
 */
typedef bool (*key_comparator_t)(
    const void* key_a, size_t key_size_a, const void* key_b, size_t key_size_b
);

/**
 * @brief Initialize the map instance.
 *
 * @param[out] map  The map instance.
 * @param[in]  size The number of the map items.
 */
void map_init(map_t* map, int item_size);

/**
 * @brief Setup the map instance.
 *
 * @param[out] map The map instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int map_setup(map_t* map);

/**
 * @brief Add a new item to the map.
 *
 * The value will not be freed during map item removal or during map cleanup.
 *
 * @param[out] map      The map instance.
 * @param[in]  key      The key of the item.
 * @param[in]  key_size The size of the key.
 * @param[in]  value    The value of the item.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int map_add(map_t* map, void* key, size_t key_size, void* value);

/**
 * @brief Get an item from the map.
 *
 * If `NULL` passed as comparator, the default `memcmp` will be used.
 *
 * @param[in] map            The map instance.
 * @param[in] key            The key of the item.
 * @param[in] key_size       The size of the key.
 * @param[in] key_comparator The key comparator function.
 *
 * @return The value of the item, `NULL` if the item not found.
 */
void* map_get(
    map_t* map, void* key, size_t key_size, key_comparator_t key_comparator
);

/**
 * @brief Remove an item from the map.
 *
 * If `NULL` passed as comparator, the default `memcmp` will be used.
 *
 * @param[out] map            The map instance.
 * @param[in]  key            The key of the item.
 * @param[in]  key_size       The size of the key.
 * @param[in]  key_comparator The key comparator function.
 */
void map_remove(
    map_t* map, void* key, size_t key_size, key_comparator_t key_comparator
);

/**
 * @brief Clean all related stuff from the map.
 *
 * @param[out] map The map instance.
 */
void map_cleanup(map_t* map);

#endif
