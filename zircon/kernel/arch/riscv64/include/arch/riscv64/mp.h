// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_MP_H_
#define ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_MP_H_

#include <zircon/compiler.h>

#include <arch/riscv64.h>
#include <kernel/align.h>
#include <kernel/cpu.h>

__BEGIN_CDECLS

struct riscv64_percpu {
  // The CPU number is used internally in Zircon
  cpu_num_t cpu_num;

  // The hart id is used by other components (SBI/PLIC etc...)
  uint hart_id;

  // Whether blocking is disallowed.  See arch_blocking_disallowed().
  uint32_t blocking_disallowed;

  // Number of spinlocks currently held.
  uint32_t num_spinlocks;
} __ALIGNED(MAX_CACHE_LINE);

register struct riscv64_percpu *__riscv64_percpu __asm("x31");

static inline void riscv64_set_percpu(struct riscv64_percpu *ptr) {
  __riscv64_percpu = ptr;
}

static inline struct riscv64_percpu *riscv64_get_percpu(void) {
  return __riscv64_percpu;
}

static inline cpu_num_t arch_curr_cpu_num(void) {
  return riscv64_get_percpu()->cpu_num;
}

static inline cpu_num_t riscv64_curr_hart_id(void) {
  return riscv64_get_percpu()->hart_id;
}

// TODO(ZX-3068) get num_cpus from topology.
// This needs to be set very early (before arch_init).
static inline void arch_set_num_cpus(uint cpu_count) {
  extern uint riscv64_num_cpus;
  riscv64_num_cpus = cpu_count;
}

static inline uint arch_max_num_cpus(void) {
  extern uint riscv64_num_cpus;

  return riscv64_num_cpus;
}

#define READ_PERCPU_FIELD32(field) riscv64_get_percpu()->field
#define WRITE_PERCPU_FIELD32(field, value) riscv64_get_percpu()->field = value

__END_CDECLS

#endif  // ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_MP_H_
