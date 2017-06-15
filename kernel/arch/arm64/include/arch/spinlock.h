// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdbool.h>
#include <sys/types.h>
#include <magenta/compiler.h>
#include <arch/arm64/interrupt.h>

__BEGIN_CDECLS

#define SPIN_LOCK_INITIAL_VALUE (0)

typedef unsigned long spin_lock_t;

typedef unsigned int spin_lock_saved_state_t;
typedef unsigned int spin_lock_save_flags_t;

void arch_spin_lock(spin_lock_t *lock);
int arch_spin_trylock(spin_lock_t *lock);
void arch_spin_unlock(spin_lock_t *lock);

static inline void arch_spin_lock_init(spin_lock_t *lock)
{
    *lock = SPIN_LOCK_INITIAL_VALUE;
}

static inline bool arch_spin_lock_held(spin_lock_t *lock)
{
    return __atomic_load_n(lock, __ATOMIC_RELAXED) != 0;
}

static inline uint arch_spin_lock_holder_cpu(spin_lock_t *lock)
{
    return (uint)__atomic_load_n(lock, __ATOMIC_RELAXED) - 1;
}

enum {
    /* Possible future flags:
     * SPIN_LOCK_FLAG_PMR_MASK         = 0x000000ff,
     * SPIN_LOCK_FLAG_PREEMPTION       = 0x10000000,
     * SPIN_LOCK_FLAG_SET_PMR          = 0x20000000,
     */

    /* ARM specific flags */
    SPIN_LOCK_FLAG_IRQ              = 0x40000000,
    SPIN_LOCK_FLAG_FIQ              = 0x80000000, /* Do not use unless IRQs are already disabled */
    SPIN_LOCK_FLAG_IRQ_FIQ          = SPIN_LOCK_FLAG_IRQ | SPIN_LOCK_FLAG_FIQ,

    /* default arm flag is to just disable plain irqs */
    ARCH_DEFAULT_SPIN_LOCK_FLAG_INTERRUPTS = SPIN_LOCK_FLAG_IRQ
};

enum {
    /* private */
    SPIN_LOCK_STATE_RESTORE_IRQ = 1,
    SPIN_LOCK_STATE_RESTORE_FIQ = 2,
};

static inline void
arch_interrupt_save(spin_lock_saved_state_t *statep, spin_lock_save_flags_t flags)
{
    spin_lock_saved_state_t state = 0;
    if ((flags & SPIN_LOCK_FLAG_IRQ) && !arch_ints_disabled()) {
        state |= SPIN_LOCK_STATE_RESTORE_IRQ;
        arch_disable_ints();
    }
    if ((flags & SPIN_LOCK_FLAG_FIQ) && !arch_fiqs_disabled()) {
        state |= SPIN_LOCK_STATE_RESTORE_FIQ;
        arch_disable_fiqs();
    }
    *statep = state;
}

static inline void
arch_interrupt_restore(spin_lock_saved_state_t old_state, spin_lock_save_flags_t flags)
{
    if ((flags & SPIN_LOCK_FLAG_FIQ) && (old_state & SPIN_LOCK_STATE_RESTORE_FIQ))
        arch_enable_fiqs();
    if ((flags & SPIN_LOCK_FLAG_IRQ) && (old_state & SPIN_LOCK_STATE_RESTORE_IRQ))
        arch_enable_ints();
}

__END_CDECLS


