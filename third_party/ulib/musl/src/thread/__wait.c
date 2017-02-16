#include "atomic.h"
#include "futex_impl.h"

void __wait(volatile int* addr, volatile int* waiters, int val) {
    int spins = 100;
    while (spins-- && (!waiters || !*waiters)) {
        if (*addr == val)
            a_spin();
        else
            return;
    }
    if (waiters)
        a_inc(waiters);
    while (*addr == val) {
        _mx_futex_wait((void*)addr, val, MX_TIME_INFINITE);
    }
    if (waiters)
        a_dec(waiters);
}
