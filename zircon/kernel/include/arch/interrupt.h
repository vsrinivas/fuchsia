// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_ARCH_INTERRUPT_H_
#define ZIRCON_KERNEL_INCLUDE_ARCH_INTERRUPT_H_

#include <arch/arch_interrupt.h>

// Wrapper header for architecturally specific interrupt enable/disable routines.

// Interrupt enable/disable guard
class InterruptDisableGuard {
 public:
  InterruptDisableGuard() : state_(arch_interrupt_save()), disabled_(true) {}
  ~InterruptDisableGuard() { Reenable(); }

  // Short circuit the disable and flip it back to reenabled.
  void Reenable() {
    if (disabled_) {
      arch_interrupt_restore(state_);
      disabled_ = false;
    }
  }

  // InterruptDisableGuard cannot be copied or moved.
  InterruptDisableGuard(const InterruptDisableGuard&) = delete;
  InterruptDisableGuard& operator=(const InterruptDisableGuard&) = delete;
  InterruptDisableGuard(InterruptDisableGuard&&) = delete;
  InterruptDisableGuard& operator=(InterruptDisableGuard&&) = delete;

 private:
  interrupt_saved_state_t state_;
  bool disabled_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_ARCH_INTERRUPT_H_
