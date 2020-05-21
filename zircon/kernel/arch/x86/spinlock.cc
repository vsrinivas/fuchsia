// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/intrin.h>

#include <arch/arch_ops.h>
#include <arch/spinlock.h>

void arch_spin_lock(arch_spin_lock_t *lock) TA_NO_THREAD_SAFETY_ANALYSIS {
  struct x86_percpu *percpu = x86_get_percpu();
  unsigned long val = percpu->cpu_num + 1;

  unsigned long expected = 0;
  while (unlikely(!__atomic_compare_exchange_n(&lock->value, &expected, val, false,
                                               __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))) {
    expected = 0;
    do {
      arch::Yield();
    } while (unlikely(__atomic_load_n(&lock->value, __ATOMIC_RELAXED) != 0));
  }
  percpu->num_spinlocks++;
}

bool arch_spin_trylock(arch_spin_lock_t *lock) TA_NO_THREAD_SAFETY_ANALYSIS {
  struct x86_percpu *percpu = x86_get_percpu();
  unsigned long val = percpu->cpu_num + 1;

  unsigned long expected = 0;

  __atomic_compare_exchange_n(&lock->value, &expected, val, false, __ATOMIC_ACQUIRE,
                              __ATOMIC_RELAXED);
  if (expected == 0) {
    percpu->num_spinlocks++;
  }

  return expected != 0;
}

void arch_spin_unlock(arch_spin_lock_t *lock) TA_NO_THREAD_SAFETY_ANALYSIS {
  x86_get_percpu()->num_spinlocks--;
  __atomic_store_n(&lock->value, 0UL, __ATOMIC_RELEASE);
}
