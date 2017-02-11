#pragma once

#include <limits.h>
#include <magenta/syscalls.h>
#include <stdatomic.h>

void __wait(atomic_int* futex, atomic_int* waiters, int current_value);
static inline void __wake(atomic_int* futex, int cnt) {
    if (cnt < 0)
        cnt = INT_MAX;
    _mx_futex_wake(futex, cnt);
}
