// Copyright 2017 The Fuchsia Authors
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

void arch_spin_lock(spin_lock_t* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
  unsigned long val = arch_curr_cpu_num() + 1;
  uint64_t temp;

  __asm__ volatile(
      "sevl;"
      "1: wfe;"
      "ldaxr   %[temp], [%[lock]];"
      "cbnz    %[temp], 1b;"
      "stxr    %w[temp], %[val], [%[lock]];"
      "cbnz    %w[temp], 1b;"
      : [ temp ] "=&r"(temp)
      : [ lock ] "r"(&lock->value), [ val ] "r"(val)
      : "cc", "memory");
  WRITE_PERCPU_FIELD32(num_spinlocks, READ_PERCPU_FIELD32(num_spinlocks) + 1);
}

int arch_spin_trylock(spin_lock_t* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
  unsigned long val = arch_curr_cpu_num() + 1;
  uint64_t out;

  __asm__ volatile(
      "ldaxr   %[out], [%[lock]];"
      "cbnz    %[out], 1f;"
      "stxr    %w[out], %[val], [%[lock]];"
      "1:"
      : [ out ] "=&r"(out)
      : [ lock ] "r"(&lock->value), [ val ] "r"(val)
      : "cc", "memory");

  if (out == 0) {
    WRITE_PERCPU_FIELD32(num_spinlocks, READ_PERCPU_FIELD32(num_spinlocks) + 1);
  }
  return (int)out;
}

void arch_spin_unlock(spin_lock_t* lock) TA_NO_THREAD_SAFETY_ANALYSIS {
  WRITE_PERCPU_FIELD32(num_spinlocks, READ_PERCPU_FIELD32(num_spinlocks) - 1);
  __atomic_store_n(&lock->value, 0UL, __ATOMIC_SEQ_CST);
}
