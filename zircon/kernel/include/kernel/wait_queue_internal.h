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

#include <kernel/sched.h>
#include <kernel/thread.h>
#include <kernel/wait.h>

namespace internal {

void wait_queue_insert(wait_queue_t* wait, Thread* t) TA_REQ(thread_lock);
void wait_queue_remove_head(Thread* t) TA_REQ(thread_lock);
void wait_queue_remove_thread(Thread* t) TA_REQ(thread_lock);
void wait_queue_timeout_handler(timer_t* timer, zx_time_t now, void* arg);

// Used by wait_queue_t and OwnedWaitQueue to manage changes to the maximum
// priority of a wait queue due to external effects (thread priority change,
// thread timeout, thread killed).  Do not call this function from an external
// site.
bool wait_queue_waiters_priority_changed(wait_queue_t* wq, int old_prio) TA_REQ(thread_lock);

// Remove a thread from a wait queue, maintain the wait queue's internal count,
// and update the wait_queue specific bookkeeping in the thread in the process.
inline void wait_queue_dequeue_thread_internal(wait_queue_t* wait, Thread* t,
                                               zx_status_t wait_queue_error) TA_REQ(thread_lock) {
  DEBUG_ASSERT(t != nullptr);
  DEBUG_ASSERT(list_in_list(&t->queue_node_));
  DEBUG_ASSERT(t->state_ == THREAD_BLOCKED || t->state_ == THREAD_BLOCKED_READ_LOCK);
  DEBUG_ASSERT(t->blocking_wait_queue_ == wait);

  wait_queue_remove_thread(t);
  wait->count--;
  t->blocked_status_ = wait_queue_error;
  t->blocking_wait_queue_ = NULL;
}

// Notes for wait_queue_block_etc_(pre|post).
//
// Currently, there are two variants of wait_queues in Zircon.  The standard
// wait_queue_t/WaitQueue (used for most tasks) and the specialized
// OwnedWaitQueues (used for mutexes/futexes/brwlocks, and anything else which
// needs to have a concept of priority inheritance).
//
// The "Block" operation for these two versions are _almost_ identical.  The
// only real difference between the two is that the OWQ implementation needs to
// stop after we have decided that we are actually going to block the thread,
// but before the timeout timer is armed and the thread is actually blocked, in
// order to update it's PI chain bookkeeping.
//
// Instead of duplicating the code, or exposing a code-injection mechanism into
// the public API, we split the code into two inline functions that we hide in
// internal:: instead.  The first (pre) performs all of the checks and bookkeeping
// up-to the point of arming the timer and blocking, the second (post) finishes
// the job.
//
// The traditional wait_queue_t/WaitQueue implementation of
// wait_queue_block_etc just calls these two functions back to back, relying on
// the inlining to generate the original function.  The OwnedWaitQueue
// implementation does the same, but injects its bookkeeping at the appropriate
// point.
//
// Nothing but these two specific pieces of code should *ever* need to call
// these functions.  Users should *always* be using either
// wait_queue_block_etc/wait_queue_block (or the WaitQueue wrappers of the
// same), or OwnedWaitQueue::BlockAndAssignOwner instead.
//
inline zx_status_t wait_queue_block_etc_pre(wait_queue_t* wait, const Deadline& deadline,
                                            uint signal_mask, ResourceOwnership reason)
    TA_REQ(thread_lock) {
  Thread* current_thread = Thread::Current::Get();

  if (deadline.when() != ZX_TIME_INFINITE && deadline.when() <= current_time()) {
    return ZX_ERR_TIMED_OUT;
  }

  if (current_thread->interruptable_ && (unlikely(current_thread->signals_ & ~signal_mask))) {
    if (current_thread->signals_ & THREAD_SIGNAL_KILL) {
      return ZX_ERR_INTERNAL_INTR_KILLED;
    } else if (current_thread->signals_ & THREAD_SIGNAL_SUSPEND) {
      return ZX_ERR_INTERNAL_INTR_RETRY;
    }
  }

  wait_queue_insert(wait, current_thread);
  wait->count++;
  current_thread->state_ =
      (reason == ResourceOwnership::Normal) ? THREAD_BLOCKED : THREAD_BLOCKED_READ_LOCK;
  current_thread->blocking_wait_queue_ = wait;
  current_thread->blocked_status_ = ZX_OK;

  return ZX_OK;
}

inline zx_status_t wait_queue_block_etc_post(wait_queue_t* wait, const Deadline& deadline)
    TA_REQ(thread_lock) {
  Thread* current_thread = Thread::Current::Get();
  timer_t timer;

  // if the deadline is nonzero or noninfinite, set a callback to yank us out of the queue
  if (deadline.when() != ZX_TIME_INFINITE) {
    timer_init(&timer);
    timer_set(&timer, deadline, wait_queue_timeout_handler, (void*)current_thread);
  }

  ktrace_ptr(TAG_KWAIT_BLOCK, wait, 0, 0);

  sched_block();

  ktrace_ptr(TAG_KWAIT_UNBLOCK, wait, current_thread->blocked_status_, 0);

  // we don't really know if the timer fired or not, so it's better safe to try to cancel it
  if (deadline.when() != ZX_TIME_INFINITE) {
    timer_cancel(&timer);
  }

  return current_thread->blocked_status_;
}

}  // namespace internal

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_WAIT_QUEUE_INTERNAL_H_
