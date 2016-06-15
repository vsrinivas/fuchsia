#include "libc.h"
#include "syscall.h"
#include <signal.h>

int sigsuspend(const sigset_t* mask) {
    return syscall(SYS_rt_sigsuspend, mask, _NSIG / 8);
}
