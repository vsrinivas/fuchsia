// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/ops.h>
#include <arch/spinlock.h>
#include <kernel/atomic.h>

// We need to disable thread safety analysis in this file, since we're
// implementing the locks themselves.  Without this, the header-level
// annotations cause Clang to detect violations.

void arch_spin_lock(arch_spin_lock_t* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
  uint64_t new_value = arch_curr_cpu_num() + 1, existing_value, sc_failed;

  __asm__ volatile (
    "0: lr.d.aq  %0, %2;"      // Load the current lock value
    "   bne      %0, x0, 0b;"  // Spin if it is already locked (non-zero value)
    "   sc.d.rl  %1, %z3, %2;" // Try to store new_value (cpu_num+1) in the lock
    "   bnez     %1, 0b"       // If the lock had changed behind our back, retry
    : "=&r"(existing_value), "=&r" (sc_failed), "+A"(lock->value)
    : "r"(new_value)
    : "memory");

  WRITE_PERCPU_FIELD32(num_spinlocks, READ_PERCPU_FIELD32(num_spinlocks) + 1);
}

bool arch_spin_trylock(arch_spin_lock_t* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
  uint64_t new_value = arch_curr_cpu_num() + 1, existing_value, sc_failed;

  __asm__ volatile (
    "0: lr.d.aq  %0, %2;"      // Load the current lock value
    "   bne      %0, x0, 1f;"  // Bail out if already locked (non-zero value)
    "   sc.d.rl  %1, %z3, %2;" // Try to store new_value (cpu_num+1) in the lock
    "   bnez     %1, 0b;"      // If the lock had changed behind our back, retry
    "1:"
    : "=&r"(existing_value), "=&r" (sc_failed), "+A"(lock->value)
    : "r"(new_value)
    : "memory");

  if (existing_value == 0) {
    WRITE_PERCPU_FIELD32(num_spinlocks, READ_PERCPU_FIELD32(num_spinlocks) + 1);
  }
  return existing_value;
}

void arch_spin_unlock(arch_spin_lock_t* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
  WRITE_PERCPU_FIELD32(num_spinlocks, READ_PERCPU_FIELD32(num_spinlocks) - 1);
  __atomic_store_n(&lock->value, 0UL, __ATOMIC_RELEASE);
}
