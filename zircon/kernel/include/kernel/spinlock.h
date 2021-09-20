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
#include <fbl/enum_bits.h>
#include <lockdep/lock_policy.h>
#include <lockdep/lock_traits.h>

enum class SpinLockOptions : uint32_t {
  None = 0,

  // Enable integration with the lockup_detector to monitor spinlock critical sections.
  //
  // See //zircon/kernel/lib/lockup_detector/README.md.
  Monitored = (1 << 0),
};
FBL_ENABLE_ENUM_BITS(SpinLockOptions)

template <SpinLockOptions Options>
class TA_CAP("mutex") SpinLockBase {
 public:
  constexpr SpinLockBase() = default;

  // Acquire the spinlock.
  //
  // Interrupts must already be disabled.
  void Acquire() TA_ACQ() {
    static_assert(!kIsMonitored, "spinlock is monitored, use Acquire(const char* name) instead");
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(!arch_spin_lock_held(&spinlock_));
    arch_spin_lock(&spinlock_);
  }
  // See |Acquire| above.
  //
  // |name| is the name of the critical section protected by this spinlock and
  // must have static lifetime.
  void Acquire(const char* name) TA_ACQ() {
    static_assert(kIsMonitored, "spinlock is unmonitored, use Acquire() instead");
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(!arch_spin_lock_held(&spinlock_));
    LOCKUP_BEGIN(name);
    arch_spin_lock(&spinlock_);
  }

  // Attempt to acquire the spinlock without waiting.
  //
  // Interrupts must already be disabled.
  //
  // Returns false when the lock is acquired, and true when the lock is not
  // acquired.
  //
  // TryAcquire operations are not permitted to fail spuriously, even on
  // architectures with weak memory ordering.  If a TryAcquire operation fails,
  // it must be because the lock was actually observed to be held by another
  // thread the attempt.
  bool TryAcquire() TA_TRY_ACQ(false) {
    static_assert(!kIsMonitored, "spinlock is monitored, use TryAcquire(const char* name) instead");
    return arch_spin_trylock(&spinlock_);
  }
  // See |TryAcquire| above.
  bool TryAcquire(const char* name) TA_TRY_ACQ(false) {
    static_assert(kIsMonitored, "spinlock is unmonitored, use TryAcquire() instead");
    bool failed_to_acquire = arch_spin_trylock(&spinlock_);
    if (!failed_to_acquire) {
      LOCKUP_BEGIN(name);
    }
    return failed_to_acquire;
  }

  // Release the spinlock
  //
  // Interrupts must already be disabled.
  void Release() TA_REL() {
    arch_spin_unlock(&spinlock_);
    if constexpr (kIsMonitored) {
      LOCKUP_END();
    }
  }

  // Returns true if held by the calling CPU.
  //
  // Interrupts must be disabled before calling this method, otherwise it could return true when it
  // should return false.
  bool IsHeld() const { return arch_spin_lock_held(&spinlock_); }

  // Just like |Acquire|, but saves interrupt state and disables interrupts.
  void AcquireIrqSave(interrupt_saved_state_t& state) TA_ACQ() {
    state = arch_interrupt_save();
    Acquire();
  }
  void AcquireIrqSave(interrupt_saved_state_t& state, const char* name) TA_ACQ() {
    state = arch_interrupt_save();
    Acquire(name);
  }

  // Just like |Release|, but restores interrupt state before unlocking.
  void ReleaseIrqRestore(interrupt_saved_state_t state) TA_REL() {
    Release();
    arch_interrupt_restore(state);
  }

  void AssertHeld() const TA_ASSERT() { DEBUG_ASSERT(IsHeld()); }

  // Returns which cpu currently holds the spin lock, or INVALID_CPU if not held.
  cpu_num_t HolderCpu() const { return arch_spin_lock_holder_cpu(&spinlock_); }

  // SpinLocks cannot be copied or moved.
  SpinLockBase(const SpinLockBase& am) = delete;
  SpinLockBase& operator=(const SpinLockBase& am) = delete;
  SpinLockBase(SpinLockBase&& c) = delete;
  SpinLockBase& operator=(SpinLockBase&& c) = delete;

 private:
  static constexpr bool kIsMonitored =
      (Options & SpinLockOptions::Monitored) != SpinLockOptions::None;
  arch_spin_lock_t spinlock_ = ARCH_SPIN_LOCK_INITIAL_VALUE;
};

using SpinLock = SpinLockBase<SpinLockOptions::None>;

// MonitoredSpinLock in a SpinLock variant that's integrated with the lockup_detector.
//
// When used with |Guard|, the last argument passed to Guard's constructor should be a const char*
// C-string with static lifetime that describes the critical section protected by the guard.
//
// Example usage:
//
// DECLARE_SINGLETON_SPINLOCK_WITH_TYPE(MonitoredSpinLock) gLock;
// ...
// {
//   Guard<MonitoredSpinLock, IrqSave> guard{gLock::Get(), SOURCE_TAG};
//   ...
// }
//
using MonitoredSpinLock = SpinLockBase<SpinLockOptions::Monitored>;

// Declares a member of type |spinlock_type| in the struct or class |containing_type| with
// instrumentation for runtime lock validation.
//
// Example usage:
//
// struct MyType {
//     DECLARE_SPINLOCK_WITH_TYPE(MyType, SpinLock [, LockFlags]) lock;
// };
//
#define DECLARE_SPINLOCK_WITH_TYPE(containing_type, spinlock_type, ...) \
  LOCK_DEP_INSTRUMENT(containing_type, spinlock_type, ##__VA_ARGS__)
// Just like |DECLARE_SPINLOCK_WITH_TYPE| except the type SpinLock is implied.
#define DECLARE_SPINLOCK(containing_type, ...) \
  DECLARE_SPINLOCK_WITH_TYPE(containing_type, SpinLock, ##__VA_ARGS__)

// Declares a singleton of type |spinlock_type| with the name |name|.
//
// Example usage:
//
//  DECLARE_SINGLETON_SPINLOCK_WITH_TYPE(MyGlobalLock, SpinLock [, LockFlags]);
//
#define DECLARE_SINGLETON_SPINLOCK_WITH_TYPE(name, spinlock_type, ...) \
  LOCK_DEP_SINGLETON_LOCK(name, spinlock_type, ##__VA_ARGS__)
// Just like |DECLARE_SINGLETON_SPINLOCK_WITH_TYPE| except the type SpinLock is implied.
#define DECLARE_SINGLETON_SPINLOCK(name, ...) \
  DECLARE_SINGLETON_SPINLOCK_WITH_TYPE(name, SpinLock, ##__VA_ARGS__)

//
// Configure lockdep flags and wrappers for SpinLock and MonitoredSpinLock.
//

// Configure lockdep to check irq-safety rules.
LOCK_DEP_TRAITS(SpinLock, lockdep::LockFlagsIrqSafe);
LOCK_DEP_TRAITS(MonitoredSpinLock, lockdep::LockFlagsIrqSafe);

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

  static bool Acquire(SpinLock* lock, State* state) TA_ACQ(lock) {
    lock->Acquire();
    return true;
  }
  static void Release(SpinLock* lock, State*) TA_REL(lock) { lock->Release(); }
  static void AssertHeld(const SpinLock& lock) TA_ASSERT(lock) { lock.AssertHeld(); }
};

// Configure Guard<SpinLock, NoIrqSave> to use the above policy to acquire and
// release a SpinLock.
LOCK_DEP_POLICY_OPTION(SpinLock, NoIrqSave, NoIrqSavePolicy);

// Lock policy for acquiring a MonitoredSpinLock WITHOUT saving irq state.
struct NoIrqSaveMonitoredPolicy {
  // No extra state required when not saving irq state.
  struct State {
    State() = delete;
    explicit State(const char* name) : name(name) {}
    const char* const name;
  };

  static bool Acquire(MonitoredSpinLock* lock, State* state) TA_ACQ(lock) {
    lock->Acquire(state->name);
    return true;
  }
  static void Release(MonitoredSpinLock* lock, State*) TA_REL(lock) { lock->Release(); }
  static void AssertHeld(const MonitoredSpinLock& lock) TA_ASSERT(lock) { lock.AssertHeld(); }
};

// Configure Guard<MonitoredSpinLock, NoIrqSave> to use the above policy to
// acquire and release a MonitoredSpinLock.
LOCK_DEP_POLICY_OPTION(MonitoredSpinLock, NoIrqSave, NoIrqSaveMonitoredPolicy);

// Lock policy for acquiring a SpinLock WITH saving irq state.
struct IrqSavePolicy {
  // State and flags required to save irq state.
  struct State {
    interrupt_saved_state_t interrupt_state;
  };

  static bool Acquire(SpinLock* lock, State* state) TA_ACQ(lock) {
    lock->AcquireIrqSave(state->interrupt_state);
    return true;
  }
  static void Release(SpinLock* lock, State* state) TA_REL(lock) {
    lock->ReleaseIrqRestore(state->interrupt_state);
  }
  static void AssertHeld(const SpinLock& lock) TA_ASSERT(lock) { lock.AssertHeld(); }
};

// Configure Guard<SpinLock, IrqSave> to use the above policy to acquire and
// release a SpinLock.
LOCK_DEP_POLICY_OPTION(SpinLock, IrqSave, IrqSavePolicy);

// Lock policy for acquiring a MonitoredSpinLock WITH saving irq state.
struct IrqSaveMonitoredPolicy {
  // State and flags required to save irq state.
  struct State {
    State() = delete;
    explicit State(const char* name) : name(name) {}
    interrupt_saved_state_t interrupt_state;
    const char* const name;
  };

  static bool Acquire(MonitoredSpinLock* lock, State* state) TA_ACQ(lock) {
    lock->AcquireIrqSave(state->interrupt_state, state->name);
    return true;
  }
  static void Release(MonitoredSpinLock* lock, State* state) TA_REL(lock) {
    lock->ReleaseIrqRestore(state->interrupt_state);
  }
  static void AssertHeld(const MonitoredSpinLock& lock) TA_ASSERT(lock) { lock.AssertHeld(); }
};

// Configure Guard<MonitoredSpinLock, IrqSave> to use the above policy to
// acquire and release a MonitoredSpinLock.
LOCK_DEP_POLICY_OPTION(MonitoredSpinLock, IrqSave, IrqSaveMonitoredPolicy);

// Lock policy for try-acquiring a SpinLock WITHOUT saving irq state.
struct TryLockNoIrqSavePolicy {
  // No extra state required when not saving irq state.
  struct State {};

  static bool Acquire(SpinLock* lock, State* state) TA_TRY_ACQ(true, lock) {
    const bool failed = lock->TryAcquire();
    return !failed;  // Guard uses true to indicate success.
  }
  static void Release(SpinLock* lock, State*) TA_REL(lock) { lock->Release(); }
};

// Configure Guard<SpinLock, TryLockNoIrqSave> to use the above policy to
// acquire and release a SpinLock.
LOCK_DEP_POLICY_OPTION(SpinLock, TryLockNoIrqSave, TryLockNoIrqSavePolicy);

// Lock policy for try-acquiring a MonitoredSpinLock WITHOUT saving irq state.
struct TryLockNoIrqSaveMonitoredPolicy {
  struct State {
    State() = delete;
    explicit State(const char* name) : name(name) {}
    const char* const name;
  };

  static bool Acquire(MonitoredSpinLock* lock, State* state) TA_TRY_ACQ(true, lock) {
    const bool failed = lock->TryAcquire(state->name);
    return !failed;  // Guard uses true to indicate success.
  }
  static void Release(MonitoredSpinLock* lock, State*) TA_REL(lock) { lock->Release(); }
};

// Configure Guard<MonitoredSpinLock, TryLockNoIrqSave> to use the above policy
// to acquire and release a MonitoredSpinLock.
LOCK_DEP_POLICY_OPTION(MonitoredSpinLock, TryLockNoIrqSave, TryLockNoIrqSaveMonitoredPolicy);

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_SPINLOCK_H_
