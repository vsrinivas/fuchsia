// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_SPINLOCK_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_SPINLOCK_H_

#include <lib/lockup_detector.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/compiler.h>

#include <arch/arch_ops.h>
#include <arch/interrupt.h>
#include <arch/spinlock.h>
#include <lockdep/lock_policy.h>
#include <lockdep/lock_traits.h>

class TA_CAP("mutex") SpinLock {
 public:
  constexpr SpinLock() = default;

  // Interrupts must already be disabled.
  void Acquire() TA_ACQ() {
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(!arch_spin_lock_held(&spinlock_));
    LOCKUP_BEGIN();
    arch_spin_lock(&spinlock_);
  }

  // Returns false when the lock is acquired, and true when the lock is not acquired.
  bool TryAcquire() TA_TRY_ACQ(false) {
    bool failed_to_acquire = arch_spin_trylock(&spinlock_);
    if (!failed_to_acquire) {
      LOCKUP_BEGIN();
    }
    return failed_to_acquire;
  }

  // Interrupts must already be disabled.
  void Release() TA_REL() {
    arch_spin_unlock(&spinlock_);
    LOCKUP_END();
  }

  // Returns true if held by the calling CPU.
  //
  // Interrupts must be disabled before calling this method, otherwise it could return true when it
  // should return false.
  bool IsHeld() { return arch_spin_lock_held(&spinlock_); }

  // Acquire spin lock, but save disable and save interrupt state first.
  void AcquireIrqSave(interrupt_saved_state_t& state) TA_ACQ() {
    state = arch_interrupt_save();
    Acquire();
  }

  // Restore interrupt state before unlocking.
  void ReleaseIrqRestore(interrupt_saved_state_t state) TA_REL() {
    Release();
    arch_interrupt_restore(state);
  }

  void AssertHeld() TA_ASSERT() { DEBUG_ASSERT(IsHeld()); }

  // Returns which cpu currently holds the spin lock, or INVALID_CPU if not held.
  cpu_num_t HolderCpu() const { return arch_spin_lock_holder_cpu(&spinlock_); }

  // SpinLocks cannot be copied or moved.
  SpinLock(const SpinLock& am) = delete;
  SpinLock& operator=(const SpinLock& am) = delete;
  SpinLock(SpinLock&& c) = delete;
  SpinLock& operator=(SpinLock&& c) = delete;

 private:
  arch_spin_lock_t spinlock_ = ARCH_SPIN_LOCK_INITIAL_VALUE;
};

// Declares a SpinLock member of the struct or class |containing_type|
// with instrumentation for runtime lock validation.
//
// Example usage:
//
// struct MyType {
//     DECLARE_SPINLOCK(MyType [, LockFlags]) lock;
// };
//
#define DECLARE_SPINLOCK(containing_type, ...) \
  LOCK_DEP_INSTRUMENT(containing_type, SpinLock, ##__VA_ARGS__)

// Declares a singleton SpinLock with the name |name|.
//
// Example usage:
//
//  DECLARE_SINGLETON_SPINLOCK(MyGlobalLock [, LockFlags]);
//
#define DECLARE_SINGLETON_SPINLOCK(name, ...) LOCK_DEP_SINGLETON_LOCK(name, SpinLock, ##__VA_ARGS__)

//
// Configure lockdep flags and wrappers for SpinLock.
//

// Configure lockdep to check irq-safety rules for SpinLock.
LOCK_DEP_TRAITS(SpinLock, lockdep::LockFlagsIrqSafe);

// Option tag for acquiring a SpinLock WITHOUT saving irq state.
struct NoIrqSave {};

// Option tag for acquiring a SpinLock WITH saving irq state.
struct IrqSave {};

// Option tag for try-acquiring a SpinLock WITHOUT saving irq state.
struct TryLockNoIrqSave {};

// Lock policy for acquiring a SpinLock WITHOUT saving irq state.
struct NoIrqSavePolicy {
  // No extra state required when not saving irq state.
  struct State {};

  static bool Acquire(SpinLock* lock, State*) TA_ACQ(lock) {
    lock->Acquire();
    return true;
  }
  static void Release(SpinLock* lock, State*) TA_REL(lock) { lock->Release(); }
  static void AssertHeld(const SpinLock& lock) TA_ASSERT(lock) {
    const_cast<SpinLock*>(&lock)->AssertHeld();
  }
};

// Configure Guard<SpinLock, NoIrqSave> to use the above policy to acquire and
// release a SpinLock.
LOCK_DEP_POLICY_OPTION(SpinLock, NoIrqSave, NoIrqSavePolicy);

// Lock policy for acquiring a SpinLock WITH saving irq state.
struct IrqSavePolicy {
  // State and flags required to save irq state.
  struct State {
    interrupt_saved_state_t state;
  };

  static bool Acquire(SpinLock* lock, State* state) TA_ACQ(lock) {
    lock->AcquireIrqSave(state->state);
    return true;
  }
  static void Release(SpinLock* lock, State* state) TA_REL(lock) {
    lock->ReleaseIrqRestore(state->state);
  }
  static void AssertHeld(const SpinLock& lock) TA_ASSERT(lock) {
    const_cast<SpinLock*>(&lock)->AssertHeld();
  }
};

// Configure Guard<SpinLock, IrqSave> to use the above policy to acquire and
// release a SpinLock.
LOCK_DEP_POLICY_OPTION(SpinLock, IrqSave, IrqSavePolicy);

// Lock policy for try-acquiring a SpinLock WITHOUT saving irq state.
struct TryLockNoIrqSavePolicy {
  // No extra state required when not saving irq state.
  struct State {};

  static bool Acquire(SpinLock* lock, State*) TA_TRY_ACQ(true, lock) {
    const bool failed = lock->TryAcquire();
    return !failed;  // Guard uses true to indicate success.
  }
  static void Release(SpinLock* lock, State*) TA_REL(lock) { lock->Release(); }
};

// Configure Guard<SpinLock, TryLockNoIrqSave> to use the above policy to
// acquire and release a SpinLock.
LOCK_DEP_POLICY_OPTION(SpinLock, TryLockNoIrqSave, TryLockNoIrqSavePolicy);

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_SPINLOCK_H_
