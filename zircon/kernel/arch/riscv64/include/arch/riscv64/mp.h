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
} __ALIGNED(MAX_CACHE_LINE);

register struct riscv64_percpu *__riscv64_percpu __asm("x31");

static inline void riscv64_set_percpu(struct riscv64_percpu *ptr) {
  __riscv64_percpu = ptr;
}

static inline struct riscv64_percpu *riscv64_get_percpu(void) {
  return __riscv64_percpu;
}

static inline cpu_num_t arch_curr_cpu_num(void) {
  return 0;
}

// TODO(ZX-3068) get num_cpus from topology.
// This needs to be set very early (before arch_init).
static inline void arch_set_num_cpus(uint cpu_count) {
}

static inline uint arch_max_num_cpus(void) {
  return 1;
}

#define READ_PERCPU_FIELD32(field) 0

#define WRITE_PERCPU_FIELD32(field, value)

__END_CDECLS

#endif  // ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_MP_H_
