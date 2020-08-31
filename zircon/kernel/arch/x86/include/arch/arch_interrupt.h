// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_ARCH_INTERRUPT_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_ARCH_INTERRUPT_H_

#include <zircon/compiler.h>

#include <arch/x86.h>
#include <kernel/atomic.h>

// Note: still pulled in from some C code, remove when the last C code is gone.
__BEGIN_CDECLS

// Implementation of x86 specific routines to disable and reenable
// local interrupts on the current cpu.

typedef x86_flags_t interrupt_saved_state_t;

__WARN_UNUSED_RESULT
static inline interrupt_saved_state_t arch_interrupt_save(void) {
  interrupt_saved_state_t state = x86_save_flags();
  if ((state & X86_FLAGS_IF) != 0) {
    x86_cli();
  }

  // Prevent the compiler from moving code into or out of the "interrupts
  // disabled" region.
  atomic_signal_fence();

  return state;
}

static inline void arch_interrupt_restore(interrupt_saved_state_t old_state) {
  // Prevent the compiler from moving code into or out of the "interrupts
  // disabled" region.
  atomic_signal_fence();

  if ((old_state & X86_FLAGS_IF) != 0) {
    x86_restore_flags(old_state);
  }
}

__END_CDECLS

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_ARCH_INTERRUPT_H_
