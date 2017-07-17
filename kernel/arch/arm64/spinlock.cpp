// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/spinlock.h>
#include <arch/ops.h>
#include <kernel/atomic.h>

void arch_spin_lock(spin_lock_t *lock) {
    spin_lock_t val = arch_curr_cpu_num() + 1;
    uint64_t temp;

    __asm__ volatile(
        "sevl;"
        "1: wfe;"
        "ldaxr   %[temp], [%[lock]];"
        "cbnz    %[temp], 1b;"
        "stxr    %w[temp], %[val], [%[lock]];"
        "cbnz    %w[temp], 1b;"
        : [temp]"=&r"(temp)
        : [lock]"r"(lock), [val]"r"(val)
        : "cc", "memory");
}

int arch_spin_trylock(spin_lock_t *lock) {
    spin_lock_t val = arch_curr_cpu_num() + 1;
    uint64_t out;

    __asm__ volatile(
        "ldaxr   %[out], [%[lock]];"
        "cbnz    %[out], 1f;"
        "stxr    %w[out], %[val], [%[lock]];"
        "1:"
        : [out]"=&r"(out)
        : [lock]"r"(lock), [val]"r"(val)
        : "cc", "memory");

    return (int)out;
}

void arch_spin_unlock(spin_lock_t *lock) {
    __atomic_store_n(lock, 0UL, __ATOMIC_SEQ_CST);
}
