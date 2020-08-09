// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_ARCH_OPS_H_
#define ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_ARCH_OPS_H_

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <zircon/compiler.h>

#include <arch/riscv64.h>
#include <arch/riscv64/interrupt.h>
#include <arch/riscv64/mp.h>

__BEGIN_CDECLS

#define ENABLE_CYCLE_COUNTER 1

static inline uint32_t arch_cpu_features(void) { return 0; }

static inline uint32_t arch_dcache_line_size(void) { return 0; }

static inline uint32_t arch_icache_line_size(void) { return 0; }

// Log architecture-specific data for process creation.
// This can only be called after the process has been created and before
// it is running. Alas we can't use zx_koid_t here as the arch layer is at a
// lower level than zircon.
static inline void arch_trace_process_create(uint64_t pid, paddr_t tt_phys) {
  // nothing to do
}

#define mb()        __asm__ volatile("fence iorw,iorw" ::: "memory");
#define wmb()       __asm__ volatile("fence ow,ow" ::: "memory");
#define rmb()       __asm__ volatile("fence ir,ir" ::: "memory");

#define smp_mb()    __asm__ volatile("fence rw,rw" ::: "memory");
#define smp_wmb()   __asm__ volatile("fence w,w" ::: "memory");
#define smp_rmb()   __asm__ volatile("fence r,r" ::: "memory");

__END_CDECLS

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_ARCH_OPS_H_
