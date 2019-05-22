// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/spinlock.h>
#include <arch/arch_ops.h>

void arch_spin_lock(spin_lock_t *lock) TA_NO_THREAD_SAFETY_ANALYSIS {
    unsigned long val = arch_curr_cpu_num() + 1;

    unsigned long expected = 0;
    while(unlikely(!__atomic_compare_exchange_n(&lock->value, &expected, val, false,
                                                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))) {
        expected = 0;
        do {
            arch_spinloop_pause();
        } while (unlikely(__atomic_load_n(&lock->value, __ATOMIC_RELAXED) != 0));
    }
}

int arch_spin_trylock(spin_lock_t* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
    unsigned long val = arch_curr_cpu_num() + 1;

    unsigned long expected = 0;

    __atomic_compare_exchange_n(&lock->value, &expected, val, false, __ATOMIC_ACQUIRE,
                                __ATOMIC_RELAXED);

    return static_cast<int>(expected);
}

void arch_spin_unlock(spin_lock_t* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
    __atomic_store_n(&lock->value, 0UL, __ATOMIC_RELEASE);
}
