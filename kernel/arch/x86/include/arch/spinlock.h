// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/x86.h>
#include <kernel/atomic.h>
#include <stdbool.h>
#include <zircon/compiler.h>
#include <zircon/thread_annotations.h>

__BEGIN_CDECLS

#define SPIN_LOCK_INITIAL_VALUE (spin_lock_t){0}

typedef struct TA_CAP("mutex") spin_lock {
    unsigned long value;
} spin_lock_t;

typedef x86_flags_t spin_lock_saved_state_t;
typedef uint spin_lock_save_flags_t;

void arch_spin_lock(spin_lock_t *lock) TA_ACQ(lock);
int arch_spin_trylock(spin_lock_t *lock) TA_TRY_ACQ(false, lock);
void arch_spin_unlock(spin_lock_t *lock) TA_REL(lock);

static inline void arch_spin_lock_init(spin_lock_t *lock)
{
    *lock = SPIN_LOCK_INITIAL_VALUE;
}

static inline bool arch_spin_lock_held(spin_lock_t *lock)
{
    return __atomic_load_n(&lock->value, __ATOMIC_RELAXED) != 0;
}

static inline uint arch_spin_lock_holder_cpu(spin_lock_t *lock)
{
    return (uint)__atomic_load_n(&lock->value, __ATOMIC_RELAXED) - 1;
}

/* flags are unused on x86 */
#define ARCH_DEFAULT_SPIN_LOCK_FLAG_INTERRUPTS  0

static inline void
arch_interrupt_save(spin_lock_saved_state_t *statep, spin_lock_save_flags_t flags)
{
    *statep = x86_save_flags();
    __asm__ volatile("cli");
    atomic_signal_fence();
}

static inline void
arch_interrupt_restore(spin_lock_saved_state_t old_state, spin_lock_save_flags_t flags)
{
    x86_restore_flags(old_state);
}

__END_CDECLS
