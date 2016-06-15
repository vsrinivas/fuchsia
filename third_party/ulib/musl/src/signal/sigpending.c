#include "syscall.h"
#include <signal.h>

int sigpending(sigset_t* set) {
    return syscall(SYS_rt_sigpending, set, _NSIG / 8);
}
