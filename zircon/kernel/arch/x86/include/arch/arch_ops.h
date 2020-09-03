// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_ARCH_OPS_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_ARCH_OPS_H_

#include <zircon/compiler.h>

#ifndef __ASSEMBLER__

#include <arch/x86.h>
#include <arch/x86/mp.h>
#include <ktl/atomic.h>

__BEGIN_CDECLS

/* override of some routines */
static inline void arch_enable_ints(void) {
  ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
  __asm__ volatile("sti");
}

static inline void arch_disable_ints(void) {
  __asm__ volatile("cli");
  ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
}

static inline bool arch_ints_disabled(void) {
  x86_flags_t state;

  __asm__ volatile(
      "pushfq;"
      "popq %%rax"
      : "=a"(state)::"memory");

  return !(state & (1 << 9));
}

static inline uint32_t arch_cpu_features(void) {
  return 0;  // Use cpuid instead.
}

uint32_t arch_dcache_line_size(void);
uint32_t arch_icache_line_size(void);

// Log architecture-specific data for process creation.
// This can only be called after the process has been created and before
// it is running. Alas we can't use zx_koid_t here as the arch layer is at a
// lower level than zircon.
void arch_trace_process_create(uint64_t pid, paddr_t pt_phys);

__END_CDECLS

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_ARCH_OPS_H_
