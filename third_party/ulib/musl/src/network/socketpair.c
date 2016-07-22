#include "syscall.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>

int socketpair(int domain, int type, int protocol, int fd[2]) {
    int r = syscall(SYS_socketpair, domain, type, protocol, fd, 0, 0);
    if (r < 0 && (errno == EINVAL || errno == EPROTONOSUPPORT) &&
        (type & (SOCK_CLOEXEC | SOCK_NONBLOCK))) {
        r = syscall(SYS_socketpair, domain, type & ~(SOCK_CLOEXEC | SOCK_NONBLOCK), protocol, fd, 0,
                    0);
        if (r < 0)
            return r;
        if (type & SOCK_CLOEXEC) {
            fcntl(fd[0], F_SETFD, FD_CLOEXEC);
            fcntl(fd[1], F_SETFD, FD_CLOEXEC);
        }
        if (type & SOCK_NONBLOCK) {
            fcntl(fd[0], F_SETFL, O_NONBLOCK);
            fcntl(fd[1], F_SETFL, O_NONBLOCK);
        }
    }
    return r;
}
