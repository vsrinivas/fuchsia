// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_WAIT_QUEUE_INTERNAL_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_WAIT_QUEUE_INTERNAL_H_

#include <lib/ktrace.h>
#include <platform.h>
#include <zircon/errors.h>

#include <kernel/scheduler.h>
#include <kernel/thread.h>
#include <kernel/wait.h>

// Notes for WaitQueue::BlockEtcPreamble and BlockEtcPostamble.
//
// Currently, there are two variants of WaitQueues in Zircon.  The standard
// WaitQueue (used for most tasks) and the specialized
// OwnedWaitQueues (used for mutexes/futexes/brwlocks, and anything else which
// needs to have a concept of priority inheritance).
//
// The "Block" operation for these two versions are _almost_ identical.  The
// only real difference between the two is that the OWQ implementation needs to
// stop after we have decided that we are actually going to block the thread,
// but before the timeout timer is armed and the thread is actually blocked, in
// order to update it's PI chain bookkeeping.
//
// Instead of duplicating the code, or exposing a code-injection
// mechanism into the public API, we split the code into two private
// inline member functions that we hide in instead.  The first
// (BlockEtcPreamble) performs all of the checks and bookkeeping up-to
// the point of arming the timer and blocking, the second
// (BlockEtcPostamble) finishes the job.
//
// The traditional WaitQueue implementation of
// WaitQueue::BlockEtc just calls these two functions back to back, relying on
// the inlining to generate the original function.  The OwnedWaitQueue
// implementation does the same, but injects its bookkeeping at the appropriate
// point.
//
// Nothing but these two specific pieces of code should *ever* need to
// call these functions.  Users should *always* be using either
// WaitQueue::BlockEtc/Block, or OwnedWaitQueue::BlockAndAssignOwner
// instead.
//
inline zx_status_t WaitQueue::BlockEtcPreamble(const Deadline& deadline, uint signal_mask,
                                               ResourceOwnership reason,
                                               Interruptible interruptible) TA_REQ(thread_lock) {
  Thread* current_thread = Thread::Current::Get();

  if (deadline.when() != ZX_TIME_INFINITE && deadline.when() <= current_time()) {
    return ZX_ERR_TIMED_OUT;
  }

  if (interruptible == Interruptible::Yes && (unlikely(current_thread->signals() & ~signal_mask))) {
    zx_status_t status = current_thread->CheckKillOrSuspendSignal();
    if (status != ZX_OK) {
      return status;
    }
  }

  WaitQueueState& state = current_thread->wait_queue_state();

  state.interruptible_ = interruptible;

  collection_.Insert(current_thread);
  if (reason == ResourceOwnership::Normal) {
    current_thread->set_blocked();
  } else {
    current_thread->set_blocked_read_lock();
  }
  state.blocking_wait_queue_ = this;
  state.blocked_status_ = ZX_OK;

  return ZX_OK;
}

inline zx_status_t WaitQueue::BlockEtcPostamble(const Deadline& deadline) TA_REQ(thread_lock) {
  Thread* current_thread = Thread::Current::Get();
  Timer timer;

  // if the deadline is nonzero or noninfinite, set a callback to yank us out of the queue
  if (deadline.when() != ZX_TIME_INFINITE) {
    timer.Set(deadline, &WaitQueue::TimeoutHandler, (void*)current_thread);
  }

  ktrace_ptr(TAG_KWAIT_BLOCK, this, 0, 0);

  Scheduler::Block();

  ktrace_ptr(TAG_KWAIT_UNBLOCK, this, current_thread->wait_queue_state().blocked_status_, 0);

  // we don't really know if the timer fired or not, so it's better safe to try to cancel it
  if (deadline.when() != ZX_TIME_INFINITE) {
    timer.Cancel();
  }

  current_thread->wait_queue_state().interruptible_ = Interruptible::No;

  return current_thread->wait_queue_state().blocked_status_;
}

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_WAIT_QUEUE_INTERNAL_H_
