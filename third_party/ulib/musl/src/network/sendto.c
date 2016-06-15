#include "libc.h"
#include "syscall.h"
#include <sys/socket.h>

ssize_t sendto(int fd, const void* buf, size_t len, int flags, const struct sockaddr* addr,
               socklen_t alen) {
    return syscall(SYS_sendto, fd, buf, len, flags, addr, alen);
}
