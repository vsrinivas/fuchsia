#include "libc.h"
#include "syscall.h"
#include <errno.h>
#include <signal.h>

int sigtimedwait(const sigset_t* restrict mask, siginfo_t* restrict si,
                 const struct timespec* restrict timeout) {
    int ret;
    do
        ret = syscall(SYS_rt_sigtimedwait, mask, si, timeout, _NSIG / 8);
    while (ret < 0 && errno == EINTR);
    return ret;
}
