// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_INTERRUPT_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_INTERRUPT_H_

#ifndef __ASSEMBLER__

#include <ktl/atomic.h>

__BEGIN_CDECLS

// override of some routines
static inline void arch_enable_ints(void) {
  ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
  __asm__ volatile("msr daifclr, #2" ::: "memory");
}

static inline void arch_disable_ints(void) {
  __asm__ volatile("msr daifset, #2" ::: "memory");
  ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
}

static inline bool arch_ints_disabled(void) {
  unsigned long state;

  __asm__ volatile("mrs %0, daif" : "=r"(state));
  state &= (1 << 7);

  return !!state;
}

static inline void arch_enable_fiqs(void) {
  ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
  __asm__ volatile("msr daifclr, #1" ::: "memory");
}

static inline void arch_disable_fiqs(void) {
  __asm__ volatile("msr daifset, #1" ::: "memory");
  ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
}

// XXX
static inline bool arch_fiqs_disabled(void) {
  unsigned long state;

  __asm__ volatile("mrs %0, daif" : "=r"(state));
  state &= (1 << 6);

  return !!state;
}

__END_CDECLS

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_INTERRUPT_H_
