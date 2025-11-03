#ifndef misc_mime_h
#define misc_mime_h

#include <stddef.h>

/**
 * @brief Get the mime for the given file path string.
 *
 * @param[out] buffer The string buffer.
 * @param[in]  size   The size of the buffer.
 * @param[in]  path   The file path.
 */
void mime_get(char* buffer, size_t size, const char* path);

#endif
