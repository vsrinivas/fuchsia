#include "libc.h"
#include "syscall.h"
#include <signal.h>
#include <stdint.h>
#include <sys/select.h>

int pselect(int n, fd_set* restrict rfds, fd_set* restrict wfds, fd_set* restrict efds,
            const struct timespec* restrict ts, const sigset_t* restrict mask) {
    syscall_arg_t data[2] = {(uintptr_t)mask, _NSIG / 8};
    struct timespec ts_tmp;
    if (ts)
        ts_tmp = *ts;
    return syscall(SYS_pselect6, n, rfds, wfds, efds, ts ? &ts_tmp : 0, data);
}
