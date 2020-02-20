// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_INTERRUPT_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_INTERRUPT_H_

#include <kernel/thread.h>

typedef struct int_handler_saved_state {
  bool old_preempt_pending;
} int_handler_saved_state_t;

// Start the main part of handling an interrupt in which preemption and
// blocking are disabled.  This must be matched by a later call to
// int_handler_finish().
static inline void int_handler_start(int_handler_saved_state_t* state) {
  arch_set_blocking_disallowed(true);
  Thread* current_thread = get_current_thread();
  // Save the value of preempt_pending for restoring later.
  state->old_preempt_pending = current_thread->preempt_pending_;
  // Clear preempt_pending so that we can later detect whether a
  // reschedule is made pending during the interrupt handler.
  current_thread->preempt_pending_ = false;
  CurrentThread::PreemptDisable();
}

// Leave the main part of handling an interrupt, following a call to
// int_handler_start().
//
// This returns whether the caller should call thread_preempt().
static inline bool int_handler_finish(int_handler_saved_state_t* state) {
  Thread* current_thread = get_current_thread();
  CurrentThread::PreemptReenableNoResched();
  bool do_preempt = false;
  if (current_thread->preempt_pending_) {
    // A preemption became pending during the interrupt handler.  If
    // preemption is now enabled, indicate that the caller should now
    // do the preemption.
    if (CurrentThread::PreemptDisableCount() == 0) {
      do_preempt = true;
    }
  } else {
    // No preemption became pending during the interrupt handler.
    //
    // Restore the old value of preempt_pending.  This may be true if
    // resched_disable is non-zero.
    current_thread->preempt_pending_ = state->old_preempt_pending;
  }
  arch_set_blocking_disallowed(false);
  return do_preempt;
}

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_INTERRUPT_H_
