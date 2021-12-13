// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_KCONCURRENT_INCLUDE_LIB_KCONCURRENT_SEQLOCK_H_
#define ZIRCON_KERNEL_LIB_KCONCURRENT_INCLUDE_LIB_KCONCURRENT_SEQLOCK_H_

#include <lib/concurrent/seqlock.h>

#include <kernel/spinlock.h>
#include <lockdep/lockdep.h>

namespace internal {
struct FuchsiaKernelOsal;
}

using SeqLock = ::concurrent::internal::SeqLock<::internal::FuchsiaKernelOsal>;

namespace SeqLockPolicy {
struct ExclusiveIrqSave {
  struct State {
    interrupt_saved_state_t interrupt_state;
    bool blocking_disallow_state;
  };

  static void PreValidate(SeqLock* lock, State* state) {
    state->interrupt_state = arch_interrupt_save();
    state->blocking_disallow_state = arch_blocking_disallowed();
    arch_set_blocking_disallowed(true);
  }

  static bool Acquire(SeqLock* lock, State*) TA_ACQ(lock) {
    lock->Acquire();
    return true;
  }

  static void Release(SeqLock* lock, State* state) TA_REL(lock) {
    lock->Release();
    arch_set_blocking_disallowed(state->blocking_disallow_state);
    arch_interrupt_restore(state->interrupt_state);
  }
};

struct ExclusiveNoIrqSave {
  struct State {
    bool blocking_disallow_state;
  };

  static void PreValidate(SeqLock* lock, State* state) {
    DEBUG_ASSERT(arch_ints_disabled());
    state->blocking_disallow_state = arch_blocking_disallowed();
    arch_set_blocking_disallowed(true);
  }

  static bool Acquire(SeqLock* lock, State*) TA_ACQ(lock) {
    lock->Acquire();
    return true;
  }

  static void Release(SeqLock* lock, State* state) TA_REL(lock) {
    lock->Release();
    arch_set_blocking_disallowed(state->blocking_disallow_state);
  }
};

struct SharedIrqSave {
  struct Shared {};  // Type tag indicating that this policy gives shared (not exclusive) access.
  struct State {
    State(bool& tgt) : result_target(tgt) { result_target = false; }
    bool& result_target;
    SeqLock::ReadTransactionToken token;
    interrupt_saved_state_t interrupt_state;
  };

  static void PreValidate(SeqLock* lock, State* state) {
    state->interrupt_state = arch_interrupt_save();
  }

  static bool Acquire(SeqLock* lock, State* state) TA_ACQ_SHARED(lock) {
    state->token = lock->BeginReadTransaction();
    return true;
  }

  static void Release(SeqLock* lock, State* state) TA_REL_SHARED(lock) {
    state->result_target = lock->EndReadTransaction(state->token);
    arch_interrupt_restore(state->interrupt_state);
  }
};

struct SharedNoIrqSave {
  struct Shared {};  // Type tag indicating that this policy gives shared (not exclusive) access.
  struct State {
    State(bool& tgt) : result_target(tgt) { result_target = false; }
    bool& result_target;
    SeqLock::ReadTransactionToken token;
  };

  static void PreValidate(SeqLock* lock, State* state) {}

  static bool Acquire(SeqLock* lock, State* state) TA_ACQ_SHARED(lock) {
    state->token = lock->BeginReadTransaction();
    return true;
  }

  static void Release(SeqLock* lock, State* state) TA_REL_SHARED(lock) {
    state->result_target = lock->EndReadTransaction(state->token);
  }
};
}  // namespace SeqLockPolicy

struct ExclusiveIrqSave {};
struct ExclusiveNoIrqSave {};
struct SharedIrqSave {};
struct SharedNoIrqSave {};

LOCK_DEP_TRAITS(SeqLock, lockdep::LockFlagsIrqSafe);
LOCK_DEP_POLICY_OPTION(SeqLock, ExclusiveIrqSave, SeqLockPolicy::ExclusiveIrqSave);
LOCK_DEP_POLICY_OPTION(SeqLock, ExclusiveNoIrqSave, SeqLockPolicy::ExclusiveNoIrqSave);
LOCK_DEP_POLICY_OPTION(SeqLock, SharedIrqSave, SeqLockPolicy::SharedIrqSave);
LOCK_DEP_POLICY_OPTION(SeqLock, SharedNoIrqSave, SeqLockPolicy::SharedNoIrqSave);

#define DECLARE_SEQLOCK(containing_type, ...) \
  LOCK_DEP_INSTRUMENT(containing_type, SeqLock, ##__VA_ARGS__)

#define DECLARE_SINGLETON_SEQLOCK(name, ...) LOCK_DEP_SINGLETON_LOCK(name, SeqLock, ##__VA_ARGS__)

#endif  // ZIRCON_KERNEL_LIB_KCONCURRENT_INCLUDE_LIB_KCONCURRENT_SEQLOCK_H_
