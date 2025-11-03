#ifndef lib_objpool_h
#define lib_objpool_h

#include <pthread.h>
#include <stddef.h>

/**
 * @brief Object pool data structure.
 */
typedef struct objpool {
    /**
     * @brief Single continguous block of memory that being allocated.
     */
    void* block_memory;

    /**
     * @brief Next available block of memory that ready to use.
     */
    void* next_free;

    /**
     * @brief Maximum number of objects available.
     */
    int object_count;

    /**
     * @brief The memory size of a single object.
     */
    size_t object_size;

    /**
     * @brief Mutex lock for thread safety.
     */
    pthread_mutex_t lock;
} objpool_t;

/**
 * @brief Initialize object pool instance.
 *
 * @param[out] objpool The object pool instance.
 */
void objpool_init(objpool_t* objpool);

/**
 * @brief Setup the object pool instance.
 *
 * @param[out] objpool The object pool instance.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int objpool_setup(objpool_t* objpool);

/**
 * @brief Allocate memory for the object pool.
 *
 * @param[out] objpool The object pool instance.
 * @param[in]  count   The number of objects to be allocated.
 * @param[in]  size    The memory size of a single object.
 *
 * @return Result code, 0 for success or -1 for errors.
 */
int objpool_allocate(objpool_t* objpool, int count, size_t size);

/**
 * @brief Acquire an object from the object pool.
 *
 * @param[out] objpool The object pool instance.
 *
 * @return Pointer address of the object or NULL if no more object available.
 */
void* objpool_acquire(objpool_t* objpool);

/**
 * @brief Release an object back to the object pool.
 *
 * @param[out] objpool The object pool instance.
 * @param[in]  object  The object to be released.
 */
void objpool_release(objpool_t* objpool, void* object);

/**
 * @brief Clean all related stuff from the object pool instance.
 *
 * @param[out] objpool The object pool instance.
 */
void objpool_cleanup(objpool_t* objpool);

#endif
