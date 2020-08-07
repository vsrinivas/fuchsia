// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_INTERRUPT_H_
#define ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_INTERRUPT_H_

#ifndef __ASSEMBLER__

#include <kernel/atomic.h>
#include <arch/riscv64.h>

__BEGIN_CDECLS

// override of some routines
static inline void arch_enable_ints(void) {
}

static inline void arch_disable_ints(void) {
}

static inline bool arch_ints_disabled(void) {
  return false;
}

__END_CDECLS

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_INTERRUPT_H_
