// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <mxtl/auto_lock.h>

class AutoSpinLock {
public:
    explicit AutoSpinLock(spin_lock_t& lock) : spinlock_(&lock) { acquire(); }
    explicit AutoSpinLock(SpinLock& lock) : spinlock_(lock.GetInternal()) { acquire(); }
    ~AutoSpinLock() { release(); }

    void release() {
        if (spinlock_) {
            spin_unlock(spinlock_);
            spinlock_ = nullptr;
        }
    }

    // suppress default constructors
    AutoSpinLock(const AutoSpinLock& am) = delete;
    AutoSpinLock(AutoSpinLock&& c) = delete;
    AutoSpinLock& operator=(const AutoSpinLock& am) = delete;
    AutoSpinLock& operator=(AutoSpinLock&& c) = delete;

private:
    void acquire() { spin_lock(spinlock_); }
    spin_lock_t* spinlock_;
};

class AutoSpinLockIrqSave {
public:
    explicit AutoSpinLockIrqSave(spin_lock_t& lock) : spinlock_(&lock) { acquire(); }
    explicit AutoSpinLockIrqSave(SpinLock& lock) : spinlock_(lock.GetInternal()) { acquire(); }
    ~AutoSpinLockIrqSave() { release(); }

    void release() {
        if (spinlock_) {
            spin_unlock_irqrestore(spinlock_, state_);
            spinlock_ = nullptr;
        }
    }

    // suppress default constructors
    AutoSpinLockIrqSave(const AutoSpinLockIrqSave& am) = delete;
    AutoSpinLockIrqSave(AutoSpinLockIrqSave&& c) = delete;
    AutoSpinLockIrqSave& operator=(const AutoSpinLockIrqSave& am) = delete;
    AutoSpinLockIrqSave& operator=(AutoSpinLockIrqSave&& c) = delete;

private:
    void acquire() { spin_lock_irqsave(spinlock_, state_); }
    spin_lock_t* spinlock_;
    spin_lock_saved_state_t state_;
};
