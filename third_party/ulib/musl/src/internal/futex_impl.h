#pragma once

#include <limits.h>
#include <magenta/syscalls.h>

void __wait(volatile int*, volatile int*, int);
static inline void __wake(volatile void* addr, int cnt) {
    if (cnt < 0)
        cnt = INT_MAX;
    _mx_futex_wake((void*)addr, cnt);
}
