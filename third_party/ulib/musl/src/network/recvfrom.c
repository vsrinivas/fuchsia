#include "libc.h"
#include "syscall.h"
#include <sys/socket.h>

ssize_t recvfrom(int fd, void* restrict buf, size_t len, int flags, struct sockaddr* restrict addr,
                 socklen_t* restrict alen) {
    return syscall(SYS_recvfrom, fd, buf, len, flags, addr, alen);
}
