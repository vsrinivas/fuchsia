// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/macros.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>

// Various lock guard wrappers for kernel only locks
// NOTE: wrapper for mutex_t is in fbl/auto_lock.h

class TA_SCOPED_CAP AutoSpinLockNoIrqSave {
public:
    explicit AutoSpinLockNoIrqSave(spin_lock_t* lock) TA_ACQ(lock)
        : spinlock_(lock) {
        DEBUG_ASSERT(lock);
        spin_lock(spinlock_);
    }
    explicit AutoSpinLockNoIrqSave(SpinLock* lock) TA_ACQ(lock)
        : AutoSpinLockNoIrqSave(lock->GetInternal()) { }
    ~AutoSpinLockNoIrqSave() TA_REL() { release(); }

    void release() TA_REL() {
        if (spinlock_) {
            spin_unlock(spinlock_);
            spinlock_ = nullptr;
        }
    }

    // suppress default constructors
    DISALLOW_COPY_ASSIGN_AND_MOVE(AutoSpinLockNoIrqSave);

private:
    spin_lock_t* spinlock_;
};

class TA_SCOPED_CAP AutoSpinLock {
public:
    explicit AutoSpinLock(spin_lock_t* lock) TA_ACQ(lock)
        : spinlock_(lock) {
        DEBUG_ASSERT(lock);
        spin_lock_irqsave(spinlock_, state_);
    }
    explicit AutoSpinLock(SpinLock* lock) TA_ACQ(lock)
        : AutoSpinLock(lock->GetInternal()) { }
    ~AutoSpinLock() TA_REL() { release(); }

    void release() TA_REL() {
        if (spinlock_) {
            spin_unlock_irqrestore(spinlock_, state_);
            spinlock_ = nullptr;
        }
    }

    // suppress default constructors
    DISALLOW_COPY_ASSIGN_AND_MOVE(AutoSpinLock);

private:
    spin_lock_t* spinlock_;
    spin_lock_saved_state_t state_;
};
