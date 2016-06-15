#include "pthread_impl.h"
#include "syscall.h"
#include <signal.h>
#include <stdint.h>

int raise(int sig) {
    int tid, ret;
    sigset_t set;
    __block_app_sigs(&set);
    tid = __syscall(SYS_gettid);
    ret = syscall(SYS_tkill, tid, sig);
    __restore_sigs(&set);
    return ret;
}
