// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/wait.h"

#include <err.h>
#include <lib/ktrace.h>
#include <platform.h>
#include <trace.h>

#include <kernel/owned_wait_queue.h>
#include <kernel/sched.h>
#include <kernel/thread.h>
#include <kernel/timer.h>

#include "kernel/wait_queue_internal.h"

#define LOCAL_TRACE 0

// add expensive code to do a full validation of the wait queue at various entry points
// to this module.
#define WAIT_QUEUE_VALIDATION (0 || (LK_DEBUGLEVEL > 2))

// Wait queues come in 2 flavors (traditional and owned) which are distinguished
// using the magic number.  When DEBUG_ASSERT checking the magic number, check
// against both of the possible valid magic numbers.
#define DEBUG_ASSERT_MAGIC_CHECK(_queue)                                                   \
  DEBUG_ASSERT_MSG(                                                                        \
      ((_queue)->magic == WAIT_QUEUE_MAGIC) || ((_queue)->magic == OwnedWaitQueue::MAGIC), \
      "magic 0x%08x", static_cast<uint32_t>((_queue)->magic));

// Wait queues are building blocks that other locking primitives use to
// handle blocking threads.
//
// Implemented as a simple structure that contains a count of the number of threads
// blocked and a list of Threads acting as individual queue heads, one per priority.

// +----------------+
// |                |
// |  wait_queue_t  |
// |                |
// +-------+--------+
//         |
//         |
//   +-----v-------+    +-------------+   +-------------+
//   |             +---->             +--->             |
//   |   Thread    |    |   Thread    |   |   Thread    |
//   |   pri 31    |    |   pri 17    |   |   pri 8     |
//   |             <----+             <---+             |
//   +---+----^----+    +-------------+   +----+---^----+
//       |    |                                |   |
//   +---v----+----+                      +----v---+----+
//   |             |                      |             |
//   |   Thread    |                      |   Thread    |
//   |   pri 31    |                      |   pri 8     |
//   |             |                      |             |
//   +---+----^----+                      +-------------+
//       |    |
//   +---v----+----+
//   |             |
//   |   Thread    |
//   |   pri 31    |
//   |             |
//   +-------------+
//

// Utility functions meant to be used *only* by wait queue internal code.
// Normally, these would be static and restricted to this translation unit, but
// there are reasons that some of this code needs to be accessible outside of
// this TU (see wait_queue_interal.h and wait_queue_block_etc_(pre|post).)
//
// Instead of having two styles, one where some of these functions are static
// and others are members of internal::, all of the previously static functions
// have been moved into the internal:: namespace instead (just for consistency's
// sake).
namespace internal {

// add a thread to the tail of a wait queue, sorted by priority
void wait_queue_insert(wait_queue_t* wait, Thread* t) TA_REQ(thread_lock) {
  if (likely(list_is_empty(&wait->heads))) {
    // we're the first thread
    list_initialize(&t->queue_node_);
    list_add_head(&wait->heads, &t->wait_queue_heads_node_);
  } else {
    int pri = t->effec_priority_;

    // walk through the sorted list of wait queue heads
    Thread* temp;
    list_for_every_entry (&wait->heads, temp, Thread, wait_queue_heads_node_) {
      if (pri > temp->effec_priority_) {
        // insert ourself here as a new queue head
        list_initialize(&t->queue_node_);
        list_add_before(&temp->wait_queue_heads_node_, &t->wait_queue_heads_node_);
        return;
      } else if (temp->effec_priority_ == pri) {
        // same priority, add ourself to the tail of this queue
        list_add_tail(&temp->queue_node_, &t->queue_node_);
        list_clear_node(&t->wait_queue_heads_node_);
        return;
      }
    }

    // we walked off the end, add ourself as a new queue head at the end
    list_initialize(&t->queue_node_);
    list_add_tail(&wait->heads, &t->wait_queue_heads_node_);
  }
}

// remove a thread from whatever wait queue its in
// thread must be the head of a queue
void wait_queue_remove_head(Thread* t) TA_REQ(thread_lock) {
  // are there any nodes in the queue for this priority?
  if (list_is_empty(&t->queue_node_)) {
    // no, remove ourself from the queue list
    list_delete(&t->wait_queue_heads_node_);
    list_clear_node(&t->queue_node_);
  } else {
    // there are other threads in this list, make the next thread in the queue the head
    Thread* newhead = list_peek_head_type(&t->queue_node_, Thread, queue_node_);
    list_delete(&t->queue_node_);

    // patch in the new head into the queue head list
    list_replace_node(&t->wait_queue_heads_node_, &newhead->wait_queue_heads_node_);
  }
}

// remove the thread from whatever wait queue its in
void wait_queue_remove_thread(Thread* t) TA_REQ(thread_lock) {
  if (!list_in_list(&t->wait_queue_heads_node_)) {
    // we're just in a queue, not a head
    list_delete(&t->queue_node_);
  } else {
    // we're the head of a queue
    wait_queue_remove_head(t);
  }
}

void wait_queue_timeout_handler(timer_t* timer, zx_time_t now, void* arg) {
  Thread* thread = (Thread*)arg;

  DEBUG_ASSERT(thread->magic_ == THREAD_MAGIC);

  // spin trylocking on the thread lock since the routine that set up the callback,
  // wait_queue_block, may be trying to simultaneously cancel this timer while holding the
  // thread_lock.
  if (timer_trylock_or_cancel(timer, &thread_lock)) {
    return;
  }

  wait_queue_unblock_thread(thread, ZX_ERR_TIMED_OUT);

  spin_unlock(&thread_lock);
}

// Deal with the consequences of a change of maximum priority across the set of
// waiters in a wait queue.
bool wait_queue_waiters_priority_changed(wait_queue_t* wq, int old_prio) TA_REQ(thread_lock) {
  // If this is an owned wait queue, and the maximum priority of its set of
  // waiters has changed, make sure to apply any needed priority inheritance.
  if ((wq->magic == OwnedWaitQueue::MAGIC) && (old_prio != wait_queue_blocked_priority(wq))) {
    return static_cast<OwnedWaitQueue*>(wq)->WaitersPriorityChanged(old_prio);
  }

  return false;
}

}  // namespace internal

////////////////////////////////////////////////////////////////////////////////
//
// End internal::
// Begin user facing API
//
////////////////////////////////////////////////////////////////////////////////

void wait_queue_init(wait_queue_t* wait) { *wait = (wait_queue_t)WAIT_QUEUE_INITIAL_VALUE(*wait); }

void wait_queue_validate_queue(wait_queue_t* wait) TA_REQ(thread_lock) {
  DEBUG_ASSERT_MAGIC_CHECK(wait);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  // validate that the queue is sorted properly
  Thread* last = NULL;
  Thread* temp;
  list_for_every_entry (&wait->heads, temp, Thread, wait_queue_heads_node_) {
    DEBUG_ASSERT(temp->magic_ == THREAD_MAGIC);

    // validate that the queue is sorted high to low priority
    if (last) {
      DEBUG_ASSERT_MSG(last->effec_priority_ > temp->effec_priority_, "%p:%d  %p:%d", last,
                       last->effec_priority_, temp, temp->effec_priority_);
    }

    // walk any threads linked to this head, validating that they're the same priority
    Thread* temp2;
    list_for_every_entry (&temp->queue_node_, temp2, Thread, queue_node_) {
      DEBUG_ASSERT(temp2->magic_ == THREAD_MAGIC);
      DEBUG_ASSERT_MSG(temp->effec_priority_ == temp2->effec_priority_, "%p:%d  %p:%d", temp,
                       temp->effec_priority_, temp2, temp2->effec_priority_);
    }

    last = temp;
  }
}

// return the numeric priority of the highest priority thread queued
int wait_queue_blocked_priority(const wait_queue_t* wait) {
  Thread* t = list_peek_head_type(&wait->heads, Thread, wait_queue_heads_node_);
  if (!t) {
    return -1;
  }

  return t->effec_priority_;
}

// returns a reference to the highest priority thread queued
Thread* wait_queue_peek(wait_queue_t* wait) {
  return list_peek_head_type(&wait->heads, Thread, wait_queue_heads_node_);
}

/**
 * @brief  Block until a wait queue is notified, ignoring existing signals
 *         in |signal_mask|.
 *
 * This function puts the current thread at the end of a wait
 * queue and then blocks until some other thread wakes the queue
 * up again.
 *
 * @param  wait        The wait queue to enter
 * @param  deadline    The time at which to abort the wait
 * @param  slack       The amount of time it is acceptable to deviate from deadline
 * @param  signal_mask Mask of existing signals to ignore
 * @param  read_lock   True if blocking for a shared resource
 *
 * If the deadline is zero, this function returns immediately with
 * ZX_ERR_TIMED_OUT.  If the deadline is ZX_TIME_INFINITE, this function
 * waits indefinitely.  Otherwise, this function returns with
 * ZX_ERR_TIMED_OUT when the deadline elapses.
 *
 * @return ZX_ERR_TIMED_OUT on timeout, else returns the return
 * value specified when the queue was woken by wait_queue_wake_one().
 */
zx_status_t wait_queue_block_etc(wait_queue_t* wait, const Deadline& deadline, uint signal_mask,
                                 ResourceOwnership reason) TA_REQ(thread_lock) {
  Thread* current_thread = get_current_thread();

  DEBUG_ASSERT_MAGIC_CHECK(wait);
  DEBUG_ASSERT(current_thread->state_ == THREAD_RUNNING);
  DEBUG_ASSERT(arch_ints_disabled());

  // Any time a thread blocks, it should be holding exactly one spinlock, and it
  // should be the thread lock.  If a thread blocks while holding another spin
  // lock, something has gone very wrong.
  DEBUG_ASSERT(spin_lock_held(&thread_lock));
  DEBUG_ASSERT(arch_num_spinlocks_held() == 1);

  if (WAIT_QUEUE_VALIDATION) {
    wait_queue_validate_queue(wait);
  }

  zx_status_t res = internal::wait_queue_block_etc_pre(wait, deadline, signal_mask, reason);
  if (res != ZX_OK) {
    return res;
  }

  return internal::wait_queue_block_etc_post(wait, deadline);
}

/**
 * @brief  Block until a wait queue is notified.
 *
 * This function puts the current thread at the end of a wait
 * queue and then blocks until some other thread wakes the queue
 * up again.
 *
 * @param  wait     The wait queue to enter
 * @param  deadline The time at which to abort the wait
 *
 * If the deadline is zero, this function returns immediately with
 * ZX_ERR_TIMED_OUT.  If the deadline is ZX_TIME_INFINITE, this function
 * waits indefinitely.  Otherwise, this function returns with
 * ZX_ERR_TIMED_OUT when the deadline occurs.
 *
 * @return ZX_ERR_TIMED_OUT on timeout, else returns the return
 * value specified when the queue was woken by wait_queue_wake_one().
 */
zx_status_t wait_queue_block(wait_queue_t* wait, zx_time_t deadline) {
  return wait_queue_block_etc(wait, Deadline::no_slack(deadline), 0, ResourceOwnership::Normal);
}

/**
 * @brief  Wake up one thread sleeping on a wait queue
 *
 * This function removes one thread (if any) from the head of the wait queue and
 * makes it executable.  The new thread will be placed in the run queue.
 *
 * @param wait  The wait queue to wake
 * @param reschedule  If true, the newly-woken thread will run immediately.
 * @param wait_queue_error  The return value which the new thread will receive
 * from wait_queue_block().
 *
 * @return  The number of threads woken (zero or one)
 */
int wait_queue_wake_one(wait_queue_t* wait, bool reschedule, zx_status_t wait_queue_error) {
  Thread* t;
  int ret = 0;

  // Note(johngro): No one should ever calling wait_queue_wake_one on an
  // instance of an OwnedWaitQueue.  OwnedWaitQueues need to deal with
  // priority inheritance, and all wake operations on an OwnedWaitQueue should
  // be going through their interface instead.
  DEBUG_ASSERT(wait->magic == WAIT_QUEUE_MAGIC);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  if (WAIT_QUEUE_VALIDATION) {
    wait_queue_validate_queue(wait);
  }

  t = wait_queue_peek(wait);
  if (t) {
    internal::wait_queue_dequeue_thread_internal(wait, t, wait_queue_error);

    ktrace_ptr(TAG_KWAIT_WAKE, wait, 0, 0);

    // wake up the new thread, putting it in a run queue on a cpu. reschedule if the local
    // cpu run queue was modified
    bool local_resched = sched_unblock(t);
    if (reschedule && local_resched) {
      sched_reschedule();
    }

    ret = 1;
  }

  return ret;
}

Thread* wait_queue_dequeue_one(wait_queue_t* wait, zx_status_t wait_queue_error) {
  DEBUG_ASSERT_MAGIC_CHECK(wait);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  if (WAIT_QUEUE_VALIDATION) {
    wait_queue_validate_queue(wait);
  }

  Thread* t = wait_queue_peek(wait);
  if (t) {
    internal::wait_queue_dequeue_thread_internal(wait, t, wait_queue_error);
  }

  return t;
}

void wait_queue_dequeue_thread(wait_queue_t* wait, Thread* t, zx_status_t wait_queue_error) {
  DEBUG_ASSERT_MAGIC_CHECK(wait);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  if (WAIT_QUEUE_VALIDATION) {
    wait_queue_validate_queue(wait);
  }

  internal::wait_queue_dequeue_thread_internal(wait, t, wait_queue_error);
}

void wait_queue_move_thread(wait_queue_t* source, wait_queue_t* dest, Thread* t) {
  DEBUG_ASSERT_MAGIC_CHECK(source);
  DEBUG_ASSERT_MAGIC_CHECK(dest);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  if (WAIT_QUEUE_VALIDATION) {
    wait_queue_validate_queue(source);
    wait_queue_validate_queue(dest);
  }

  DEBUG_ASSERT(t != nullptr);
  DEBUG_ASSERT(list_in_list(&t->queue_node_));
  DEBUG_ASSERT(t->state_ == THREAD_BLOCKED || t->state_ == THREAD_BLOCKED_READ_LOCK);
  DEBUG_ASSERT(t->blocking_wait_queue_ == source);
  DEBUG_ASSERT(source->count > 0);

  internal::wait_queue_remove_thread(t);
  internal::wait_queue_insert(dest, t);
  --source->count;
  ++dest->count;
  t->blocking_wait_queue_ = dest;
}

/**
 * @brief  Wake all threads sleeping on a wait queue
 *
 * This function removes all threads (if any) from the wait queue and
 * makes them executable.  The new threads will be placed at the head of the
 * run queue.
 *
 * @param wait  The wait queue to wake
 * @param reschedule  If true, the newly-woken threads will run immediately.
 * @param wait_queue_error  The return value which the new thread will receive
 * from wait_queue_block().
 *
 * @return  The number of threads woken
 */
int wait_queue_wake_all(wait_queue_t* wait, bool reschedule, zx_status_t wait_queue_error) {
  Thread* t;
  int ret = 0;

  // Note(johngro): See the note in wake_one.  On one should ever be calling
  // this method on an OwnedWaitQueue
  DEBUG_ASSERT(wait->magic == WAIT_QUEUE_MAGIC);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  if (WAIT_QUEUE_VALIDATION) {
    wait_queue_validate_queue(wait);
  }

  if (wait->count == 0) {
    return 0;
  }

  struct list_node list = LIST_INITIAL_VALUE(list);

  // pop all the threads off the wait queue into the run queue
  // TODO: optimize with custom pop all routine
  while ((t = wait_queue_peek(wait))) {
    internal::wait_queue_dequeue_thread_internal(wait, t, wait_queue_error);
    list_add_tail(&list, &t->queue_node_);
    ret++;
  }

  DEBUG_ASSERT(ret > 0);
  DEBUG_ASSERT(wait->count == 0);

  ktrace_ptr(TAG_KWAIT_WAKE, wait, 0, 0);

  // wake up the new thread(s), putting it in a run queue on a cpu. reschedule if the local
  // cpu run queue was modified
  bool local_resched = sched_unblock_list(&list);
  if (reschedule && local_resched) {
    sched_reschedule();
  }

  return ret;
}

bool wait_queue_is_empty(const wait_queue_t* wait) {
  DEBUG_ASSERT_MAGIC_CHECK(wait);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  return wait->count == 0;
}

/**
 * @brief  Tear down a wait queue
 *
 * This panics if any threads were waiting on this queue, because that
 * would indicate a race condition for most uses of wait queues.  If a
 * thread is currently waiting, it could have been scheduled later, in
 * which case it would have called wait_queue_block() on an invalid wait
 * queue.
 */
void wait_queue_destroy(wait_queue_t* wait) {
  DEBUG_ASSERT_MAGIC_CHECK(wait);

  if (wait->count != 0) {
    panic("wait_queue_destroy() called on non-empty wait_queue_t\n");
  }

  wait->magic = 0;
}

/**
 * @brief  Wake a specific thread in a wait queue
 *
 * This function extracts a specific thread from a wait queue, wakes it,
 * puts it at the head of the run queue, and does a reschedule if
 * necessary.
 *
 * @param t  The thread to wake
 * @param wait_queue_error  The return value which the new thread will receive from
 * wait_queue_block().
 *
 * @return ZX_ERR_BAD_STATE if thread was not in any wait queue.
 */
zx_status_t wait_queue_unblock_thread(Thread* t, zx_status_t wait_queue_error) {
  DEBUG_ASSERT(t->magic_ == THREAD_MAGIC);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  if (t->state_ != THREAD_BLOCKED && t->state_ != THREAD_BLOCKED_READ_LOCK) {
    return ZX_ERR_BAD_STATE;
  }

  wait_queue_t* wq = t->blocking_wait_queue_;
  DEBUG_ASSERT(wq != NULL);
  DEBUG_ASSERT_MAGIC_CHECK(wq);
  DEBUG_ASSERT(list_in_list(&t->queue_node_));

  if (WAIT_QUEUE_VALIDATION) {
    wait_queue_validate_queue(wq);
  }

  int old_wq_prio = wait_queue_blocked_priority(wq);
  internal::wait_queue_dequeue_thread_internal(wq, t, wait_queue_error);
  internal::wait_queue_waiters_priority_changed(wq, old_wq_prio);

  if (sched_unblock(t)) {
    sched_reschedule();
  }

  return ZX_OK;
}

bool wait_queue_priority_changed(Thread* t, int old_prio, PropagatePI propagate) {
  DEBUG_ASSERT(t->magic_ == THREAD_MAGIC);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));
  DEBUG_ASSERT(t->state_ == THREAD_BLOCKED || t->state_ == THREAD_BLOCKED_READ_LOCK);

  wait_queue_t* wq = t->blocking_wait_queue_;
  DEBUG_ASSERT(wq != NULL);
  DEBUG_ASSERT_MAGIC_CHECK(wq);

  LTRACEF("%p %d -> %d\n", t, old_prio, t->effec_priority_);

  // |t|'s effective priority has already been re-calculated.  If |t| is
  // currently at the head of the wait queue, then |t|'s old priority is the
  // previous priority of the wait queue.  Otherwise, it is the priority of
  // the wait queue as it stands before we re-insert |t|
  int old_wq_prio = (wait_queue_peek(wq) == t) ? old_prio : wait_queue_blocked_priority(wq);

  // simple algorithm: remove the thread from the queue and add it back
  // TODO: implement optimal algorithm depending on all the different edge
  // cases of how the thread was previously queued and what priority its
  // switching to.
  internal::wait_queue_remove_thread(t);
  internal::wait_queue_insert(wq, t);

  bool ret = (propagate == PropagatePI::Yes)
                 ? internal::wait_queue_waiters_priority_changed(wq, old_wq_prio)
                 : false;

  if (WAIT_QUEUE_VALIDATION) {
    wait_queue_validate_queue(t->blocking_wait_queue_);
  }
  return ret;
}
