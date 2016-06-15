#include "syscall.h"
#include <sys/socket.h>

int setsockopt(int fd, int level, int optname, const void* optval, socklen_t optlen) {
    return syscall(SYS_setsockopt, fd, level, optname, optval, optlen, 0);
}
