// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>

// Various lock guard wrappers for kernel only locks
// NOTE: wrapper for mutex_t is in fbl/auto_lock.h

class TA_SCOPED_CAP AutoSpinLock {
public:
    explicit AutoSpinLock(spin_lock_t* lock)
        : spinlock_(lock) {
        DEBUG_ASSERT(lock);
        acquire();
    }
    explicit AutoSpinLock(SpinLock* lock)
        : spinlock_(lock->GetInternal()) {
        DEBUG_ASSERT(lock);
        acquire();
    }
    ~AutoSpinLock() { release(); }

    void release() TA_REL() {
        if (spinlock_) {
            spin_unlock(spinlock_);
            spinlock_ = nullptr;
        }
    }

    // suppress default constructors
    DISALLOW_COPY_ASSIGN_AND_MOVE(AutoSpinLock);

private:
    void acquire() TA_ACQ() { spin_lock(spinlock_); }
    spin_lock_t* spinlock_;
};

class AutoSpinLockIrqSave {
public:
    explicit AutoSpinLockIrqSave(spin_lock_t* lock)
        : spinlock_(lock) {
        DEBUG_ASSERT(lock);
        acquire();
    }
    explicit AutoSpinLockIrqSave(SpinLock* lock)
        : spinlock_(lock->GetInternal()) {
        DEBUG_ASSERT(lock);
        acquire();
    }
    ~AutoSpinLockIrqSave() { release(); }

    void release() {
        if (spinlock_) {
            spin_unlock_irqrestore(spinlock_, state_);
            spinlock_ = nullptr;
        }
    }

    // suppress default constructors
    DISALLOW_COPY_ASSIGN_AND_MOVE(AutoSpinLockIrqSave);

private:
    void acquire() { spin_lock_irqsave(spinlock_, state_); }
    spin_lock_t* spinlock_;
    spin_lock_saved_state_t state_;
};
