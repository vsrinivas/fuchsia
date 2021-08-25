// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_INTERRUPT_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_INTERRUPT_H_

#include <kernel/cpu.h>
#include <kernel/thread.h>

struct int_handler_saved_state_t {};

// Start the main part of handling an interrupt in which preemption and
// blocking are disabled.  This must be matched by a later call to
// int_handler_finish().
inline void int_handler_start(int_handler_saved_state_t* state) {
  arch_set_blocking_disallowed(true);
  PreemptionState& preemption_state = Thread::Current::preemption_state();

  // Disable all reschedules at least until the interrupt is finishing up.
  // Although eager resched disable implies preempt disable, the nesting here is
  // not redundant and is necessary to defer the local preemption to the safe
  // point in the calling frame.
  preemption_state.PreemptDisable();
  preemption_state.EagerReschedDisable();
}

// Leave the main part of handling an interrupt, following a call to
// int_handler_start().
//
// If this function returns true, it means that there was a local preempt
// pending at the time the exception handler finished, and that the current
// thread does not have preemption disabled.  In this case, callers *must*
// arrange to have preemption take place (typically via
// Thread::Current()::Preempt()) _before_ completely unwinding from the
// exception.
[[nodiscard]] inline bool int_handler_finish(int_handler_saved_state_t* state) {
  PreemptionState& preemption_state = Thread::Current::preemption_state();

  // Flush any pending remote preemptions if eager reschedules are enabled.
  preemption_state.EagerReschedReenable();

  // Drop the preempt disable count that we added at the start of the interrupt
  // handler, but do not trigger any local preemption if the disabled count has
  // hit zero, and there is a local preempt pending.
  //
  // Instead, if a local preemption became pending during the interrupt handler and
  // preemption is now enabled, indicate that the caller should perform the
  // preemption.
  const bool do_preempt = preemption_state.PreemptReenableDelayFlush();
  arch_set_blocking_disallowed(false);
  return do_preempt;
}

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_INTERRUPT_H_
