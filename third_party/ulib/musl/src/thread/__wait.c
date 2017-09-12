#include "atomic.h"
#include "futex_impl.h"

void __wait(atomic_int* futex, atomic_int* waiters, int current_value) {
    int spins = 100;
    while (spins-- && (!waiters || !atomic_load(waiters))) {
        if (atomic_load(futex) == current_value)
            a_spin();
        else
            return;
    }
    if (waiters)
        atomic_fetch_add(waiters, 1);
    while (atomic_load(futex) == current_value) {
        _zx_futex_wait(futex, current_value, ZX_TIME_INFINITE);
    }
    if (waiters)
        atomic_fetch_sub(waiters, 1);
}
