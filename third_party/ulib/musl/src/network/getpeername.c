#include "syscall.h"
#include <sys/socket.h>

int getpeername(int fd, struct sockaddr* restrict addr, socklen_t* restrict len) {
    return syscall(SYS_getpeername, fd, addr, len, 0, 0, 0);
}
