// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARCH_INTERRUPT_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARCH_INTERRUPT_H_

#include <zircon/compiler.h>

#include <arch/arm64/interrupt.h>

// Note: still pulled in from some C code, remove when the last C code is gone.
__BEGIN_CDECLS

// Implementation of arm64 specific routines to disable and reenable
// local interrupts on the current cpu.

typedef bool interrupt_saved_state_t;

__WARN_UNUSED_RESULT
static inline interrupt_saved_state_t arch_interrupt_save(void) {
  interrupt_saved_state_t state = false;
  if (!arch_ints_disabled()) {
    state = true;
    arch_disable_ints();
  }
  return state;
}

static inline void arch_interrupt_restore(interrupt_saved_state_t old_state) {
  if (old_state) {
    arch_enable_ints();
  }
}

__END_CDECLS

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARCH_INTERRUPT_H_
