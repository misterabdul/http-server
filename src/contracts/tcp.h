#ifndef contract_tcp_h
#define contract_tcp_h

#include <sys/types.h>

ssize_t platform_sendfile(int out_fd, int in_fd, off_t *offset, size_t count);

#endif
