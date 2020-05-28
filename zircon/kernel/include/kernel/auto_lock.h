// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_AUTO_LOCK_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_AUTO_LOCK_H_

#include <fbl/macros.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>

// Various lock guard wrappers for kernel only locks

class TA_SCOPED_CAP AutoSpinLockNoIrqSave {
 public:
  __WARN_UNUSED_CONSTRUCTOR explicit AutoSpinLockNoIrqSave(SpinLock* lock) TA_ACQ(lock)
      : spinlock_(lock) {
    DEBUG_ASSERT(lock);
    spinlock_->Acquire();
  }

  ~AutoSpinLockNoIrqSave() TA_REL() { release(); }

  void release() TA_REL() {
    if (spinlock_) {
      spinlock_->Release();
      spinlock_ = nullptr;
    }
  }

  // suppress default constructors
  DISALLOW_COPY_ASSIGN_AND_MOVE(AutoSpinLockNoIrqSave);

 private:
  SpinLock* spinlock_;
};

class TA_SCOPED_CAP AutoSpinLock {
 public:
  __WARN_UNUSED_CONSTRUCTOR explicit AutoSpinLock(SpinLock* lock) TA_ACQ(lock) : spinlock_(lock) {
    DEBUG_ASSERT(lock);
    spinlock_->AcquireIrqSave(state_);
  }

  ~AutoSpinLock() TA_REL() { release(); }

  void release() TA_REL() {
    if (spinlock_) {
      spinlock_->ReleaseIrqRestore(state_);
      spinlock_ = nullptr;
    }
  }

  // suppress default constructors
  DISALLOW_COPY_ASSIGN_AND_MOVE(AutoSpinLock);

 private:
  SpinLock* spinlock_;
  interrupt_saved_state_t state_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_AUTO_LOCK_H_
