// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/spinlock.h>

template <unsigned int flags = SPIN_LOCK_FLAG_INTERRUPTS>
class AutoSpinLock {
public:
    inline explicit AutoSpinLock(spin_lock_t* lock) : lock_(lock) {
        arch_interrupt_save(&state_, flags);
        spin_lock(lock_);
    }

    inline ~AutoSpinLock() {
        spin_unlock(lock_);
        arch_interrupt_restore(state_, flags);
    }

    AutoSpinLock(const AutoSpinLock&) = delete;
    AutoSpinLock& operator=(const AutoSpinLock&) = delete;
    AutoSpinLock(AutoSpinLock&&) = delete;
    AutoSpinLock& operator=(AutoSpinLock&&) = delete;

private:
    spin_lock_t* lock_;
    spin_lock_saved_state_t state_;
};
