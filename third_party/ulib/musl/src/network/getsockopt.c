#include "syscall.h"
#include <sys/socket.h>

int getsockopt(int fd, int level, int optname, void* restrict optval, socklen_t* restrict optlen) {
    return syscall(SYS_getsockopt, fd, level, optname, optval, optlen, 0);
}
