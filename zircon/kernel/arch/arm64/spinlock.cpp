// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/ops.h>
#include <arch/spinlock.h>
#include <kernel/atomic.h>

// We need to disable thread safety analysis in this file, since we're
// implementing the locks themselves.  Without this, the header-level
// annotations cause Clang to detect violations.

// ARM-specific exclusive operations.

// Load-Acquire Exclusive. Generates LDAXR.
// Load a 64-bit value with an acquire memory barrier (DMBLD) and take the exclusive monitor.
// Ready for matching Store Exclusive or WFE.
static inline uint64_t arm64_load_acquire_exclusive(uint64_t* target) {
#if __has_builtin(__builtin_arm_ldaex)
    return __builtin_arm_ldaex(target);
#else
    uint64_t value;
    __asm__ volatile("ldaxr %[value], [%[target]]"
                     : [value] "=&r"(value) : [target] "r"(target) : "memory");
    return value;
#endif
}

// Store Exclusive. Generates STXR.
// Attempt to store a 64-bit value to an address with an exclusive monitor held.
// Return value is the status code from stxr: 0 = success, 1 = failure.
static inline uint32_t arm64_store_exclusive(uint64_t* target, uint64_t value) {
#if __has_builtin(__builtin_arm_strex)
    return __builtin_arm_strex(value, target);
#else
    uint32_t status;
    __asm__ volatile("stxr  %w[status], %[value], [%[target]]"
                     : [status] "=r"(status)
                     : [target] "r"(target), [value] "r"(value) : "cc", "memory");
    return status;
#endif
}

void arch_spin_lock(spin_lock_t* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
    unsigned long val = arch_curr_cpu_num() + 1;
    __sevl();
    for ( ; ; ) {
        __wfe();
        uint64_t data = arm64_load_acquire_exclusive(&lock->value);
        if (unlikely(data != 0)) {
            continue;
        }
        uint32_t status = arm64_store_exclusive(&lock->value, val);
        if (likely(status == 0)) {
            return;
        }
    }
}

int arch_spin_trylock(spin_lock_t* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
    unsigned long val = arch_curr_cpu_num() + 1;
    uint64_t data = arm64_load_acquire_exclusive(&lock->value);
    if (unlikely(data != 0)) {
        return (int)data;
    }
    return arm64_store_exclusive(&lock->value, val);
}

void arch_spin_unlock(spin_lock_t* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
    __atomic_store_n(&lock->value, 0UL, __ATOMIC_SEQ_CST);
}
