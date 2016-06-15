#include "libc.h"
#include "syscall.h"
#include <poll.h>
#include <signal.h>
#include <time.h>

int poll(struct pollfd* fds, nfds_t n, int timeout) {
#ifdef SYS_poll
    return syscall(SYS_poll, fds, n, timeout);
#else
    return syscall(
        SYS_ppoll, fds, n,
        timeout >= 0
            ? &((struct timespec){.tv_sec = timeout / 1000, .tv_nsec = timeout % 1000 * 1000000})
            : 0,
        0, _NSIG / 8);
#endif
}
