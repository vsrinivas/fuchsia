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
#include <kernel/scheduler.h>
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
#define DEBUG_ASSERT_MAGIC_CHECK(_queue)                                                 \
  DEBUG_ASSERT_MSG(                                                                      \
      ((_queue)->magic_ == kMagic) || ((_queue)->magic_ == OwnedWaitQueue::kOwnedMagic), \
      "magic 0x%08x", ((_queue)->magic_));

// Wait queues are building blocks that other locking primitives use to
// handle blocking threads.
//
// Implemented as a simple structure that contains a count of the number of threads
// blocked and a list of Threads acting as individual queue heads, one per priority.

// +----------------+
// |                |
// |   WaitQueue    |
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

void wait_queue_timeout_handler(Timer* timer, zx_time_t now, void* arg) {
  Thread* thread = (Thread*)arg;

  DEBUG_ASSERT(thread->magic_ == THREAD_MAGIC);

  // spin trylocking on the thread lock since the routine that set up the callback,
  // wait_queue_block, may be trying to simultaneously cancel this timer while holding the
  // thread_lock.
  if (timer->TrylockOrCancel(&thread_lock)) {
    return;
  }

  WaitQueue::UnblockThread(thread, ZX_ERR_TIMED_OUT);

  thread_lock.Release();
}

// Deal with the consequences of a change of maximum priority across the set of
// waiters in a wait queue.
bool wait_queue_waiters_priority_changed(WaitQueue* wq, int old_prio) TA_REQ(thread_lock) {
  // If this is an owned wait queue, and the maximum priority of its set of
  // waiters has changed, make sure to apply any needed priority inheritance.
  if ((wq->magic_ == OwnedWaitQueue::kOwnedMagic) && (old_prio != wq->BlockedPriority())) {
    return static_cast<OwnedWaitQueue*>(wq)->WaitersPriorityChanged(old_prio);
  }

  return false;
}

// Remove a thread from a wait queue, maintain the wait queue's internal count,
// and update the wait_queue specific bookkeeping in the thread in the process.
void wait_queue_dequeue_thread_internal(WaitQueue* wait, Thread* t, zx_status_t wait_queue_error)
    TA_REQ(thread_lock) {
  DEBUG_ASSERT(t != nullptr);
  DEBUG_ASSERT(t->wait_queue_state_.InWaitQueue());
  DEBUG_ASSERT(t->state_ == THREAD_BLOCKED || t->state_ == THREAD_BLOCKED_READ_LOCK);
  DEBUG_ASSERT(t->blocking_wait_queue_ == wait);

  wait->collection_.Remove(t);
  t->blocked_status_ = wait_queue_error;
  t->blocking_wait_queue_ = NULL;
}

}  // namespace internal

Thread* WaitQueueCollection::Peek() {
  return list_peek_head_type(&private_heads_, Thread, wait_queue_state_.wait_queue_heads_node_);
}

const Thread* WaitQueueCollection::Peek() const {
  return list_peek_head_type(&private_heads_, Thread, wait_queue_state_.wait_queue_heads_node_);
}

void WaitQueueCollection::Insert(Thread* thread) {
  // Regardless of the state of the collection, the count goes up one.
  ++private_count_;

  if (likely(list_is_empty(&private_heads_))) {
    // We're the first thread.
    list_initialize(&thread->wait_queue_state_.queue_node_);
    list_add_head(&private_heads_, &thread->wait_queue_state_.wait_queue_heads_node_);
  } else {
    const int pri = thread->scheduler_state_.effective_priority();

    // Walk through the sorted list of wait queue heads.
    Thread* temp;
    list_for_every_entry (&private_heads_, temp, Thread, wait_queue_state_.wait_queue_heads_node_) {
      if (pri > temp->scheduler_state_.effective_priority()) {
        // Insert ourself here as a new queue head.
        list_initialize(&thread->wait_queue_state_.queue_node_);
        list_add_before(&temp->wait_queue_state_.wait_queue_heads_node_,
                        &thread->wait_queue_state_.wait_queue_heads_node_);
        return;
      } else if (temp->scheduler_state_.effective_priority() == pri) {
        // Same priority, add ourself to the tail of this queue.
        list_add_tail(&temp->wait_queue_state_.queue_node_, &thread->wait_queue_state_.queue_node_);
        list_clear_node(&thread->wait_queue_state_.wait_queue_heads_node_);
        return;
      }
    }

    // We walked off the end, so add ourself as a new queue head at the end.
    list_initialize(&thread->wait_queue_state_.queue_node_);
    list_add_tail(&private_heads_, &thread->wait_queue_state_.wait_queue_heads_node_);
  }
}

void WaitQueueCollection::Remove(Thread* thread) {
  // Either way, the count goes down one.
  --private_count_;

  if (!list_in_list(&thread->wait_queue_state_.wait_queue_heads_node_)) {
    // We're just in a queue, not a head.
    list_delete(&thread->wait_queue_state_.queue_node_);
  } else {
    // We're the head of a queue.

    // Are there any nodes in the queue for this priority?
    if (list_is_empty(&thread->wait_queue_state_.queue_node_)) {
      // No, remove ourself from the queue list.
      list_delete(&thread->wait_queue_state_.wait_queue_heads_node_);
      list_clear_node(&thread->wait_queue_state_.queue_node_);
    } else {
      // There are other threads in this list, make the next thread in the queue the head.
      Thread* newhead = list_peek_head_type(&thread->wait_queue_state_.queue_node_, Thread,
                                            wait_queue_state_.queue_node_);
      list_delete(&thread->wait_queue_state_.queue_node_);

      // Patch in the new head into the queue head list.
      list_replace_node(&thread->wait_queue_state_.wait_queue_heads_node_,
                        &newhead->wait_queue_state_.wait_queue_heads_node_);
    }
  }
}

void WaitQueueCollection::Validate() const {
  // Validate that the queue is sorted properly
  Thread* last = nullptr;
  Thread* temp;
  list_for_every_entry (&private_heads_, temp, Thread, wait_queue_state_.wait_queue_heads_node_) {
    DEBUG_ASSERT(temp->magic_ == THREAD_MAGIC);

    // Validate that the queue is sorted high to low priority.
    if (last) {
      DEBUG_ASSERT_MSG(
          last->scheduler_state_.effective_priority() > temp->scheduler_state_.effective_priority(),
          "%p:%d  %p:%d", last, last->scheduler_state_.effective_priority(), temp,
          temp->scheduler_state_.effective_priority());
    }

    // Walk any threads linked to this head, validating that they're the same priority.
    Thread* temp2;
    list_for_every_entry (&temp->wait_queue_state_.queue_node_, temp2, Thread,
                          wait_queue_state_.queue_node_) {
      DEBUG_ASSERT(temp2->magic_ == THREAD_MAGIC);
      DEBUG_ASSERT_MSG(temp->scheduler_state_.effective_priority() ==
                           temp2->scheduler_state_.effective_priority(),
                       "%p:%d  %p:%d", temp, temp->scheduler_state_.effective_priority(), temp2,
                       temp2->scheduler_state_.effective_priority());
    }

    last = temp;
  }
}

////////////////////////////////////////////////////////////////////////////////
//
// End internal::
// Begin user facing API
//
////////////////////////////////////////////////////////////////////////////////

void WaitQueue::ValidateQueue() TA_REQ(thread_lock) {
  DEBUG_ASSERT_MAGIC_CHECK(this);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  collection_.Validate();
}

// return the numeric priority of the highest priority thread queued
int WaitQueue::BlockedPriority() const {
  const Thread* t = Peek();
  if (!t) {
    return -1;
  }

  return t->scheduler_state_.effective_priority();
}

// returns a reference to the highest priority thread queued
Thread* WaitQueue::Peek() { return collection_.Peek(); }

const Thread* WaitQueue::Peek() const { return collection_.Peek(); }

/**
 * @brief  Block until a wait queue is notified, ignoring existing signals
 *         in |signal_mask|.
 *
 * This function puts the current thread at the end of a wait
 * queue and then blocks until some other thread wakes the queue
 * up again.
 *
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
zx_status_t WaitQueue::BlockEtc(const Deadline& deadline, uint signal_mask,
                                ResourceOwnership reason) TA_REQ(thread_lock) {
  Thread* current_thread = Thread::Current::Get();

  DEBUG_ASSERT_MAGIC_CHECK(this);
  DEBUG_ASSERT(current_thread->state_ == THREAD_RUNNING);
  DEBUG_ASSERT(arch_ints_disabled());

  // Any time a thread blocks, it should be holding exactly one spinlock, and it
  // should be the thread lock.  If a thread blocks while holding another spin
  // lock, something has gone very wrong.
  DEBUG_ASSERT(thread_lock.IsHeld());
  DEBUG_ASSERT(arch_num_spinlocks_held() == 1);

  if (WAIT_QUEUE_VALIDATION) {
    ValidateQueue();
  }

  zx_status_t res = internal::wait_queue_block_etc_pre(this, deadline, signal_mask, reason);
  if (res != ZX_OK) {
    return res;
  }

  return internal::wait_queue_block_etc_post(this, deadline);
}

/**
 * @brief  Wake up one thread sleeping on a wait queue
 *
 * This function removes one thread (if any) from the head of the wait queue and
 * makes it executable.  The new thread will be placed in the run queue.
 *
 * @param reschedule  If true, the newly-woken thread will run immediately.
 * @param wait_queue_error  The return value which the new thread will receive
 * from wait_queue_block().
 *
 * @return  The number of threads woken (zero or one)
 */
int WaitQueue::WakeOne(bool reschedule, zx_status_t wait_queue_error) {
  Thread* t;
  int ret = 0;

  // Note(johngro): No one should ever calling wait_queue_wake_one on an
  // instance of an OwnedWaitQueue.  OwnedWaitQueues need to deal with
  // priority inheritance, and all wake operations on an OwnedWaitQueue should
  // be going through their interface instead.
  DEBUG_ASSERT(magic_ == kMagic);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  if (WAIT_QUEUE_VALIDATION) {
    ValidateQueue();
  }

  t = Peek();
  if (t) {
    internal::wait_queue_dequeue_thread_internal(this, t, wait_queue_error);

    ktrace_ptr(TAG_KWAIT_WAKE, this, 0, 0);

    // wake up the new thread, putting it in a run queue on a cpu. reschedule if the local
    // cpu run queue was modified
    bool local_resched = Scheduler::Unblock(t);
    if (reschedule && local_resched) {
      Scheduler::Reschedule();
    }

    ret = 1;
  }

  return ret;
}

Thread* WaitQueue::DequeueOne(zx_status_t wait_queue_error) {
  DEBUG_ASSERT_MAGIC_CHECK(this);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  if (WAIT_QUEUE_VALIDATION) {
    ValidateQueue();
  }

  Thread* t = Peek();
  if (t) {
    internal::wait_queue_dequeue_thread_internal(this, t, wait_queue_error);
  }

  return t;
}

void WaitQueue::DequeueThread(Thread* t, zx_status_t wait_queue_error) {
  DEBUG_ASSERT_MAGIC_CHECK(this);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  if (WAIT_QUEUE_VALIDATION) {
    ValidateQueue();
  }

  internal::wait_queue_dequeue_thread_internal(this, t, wait_queue_error);
}

void WaitQueue::MoveThread(WaitQueue* source, WaitQueue* dest, Thread* t) {
  DEBUG_ASSERT_MAGIC_CHECK(source);
  DEBUG_ASSERT_MAGIC_CHECK(dest);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  if (WAIT_QUEUE_VALIDATION) {
    source->ValidateQueue();
    dest->ValidateQueue();
  }

  DEBUG_ASSERT(t != nullptr);
  DEBUG_ASSERT(t->wait_queue_state_.InWaitQueue());
  DEBUG_ASSERT(t->state_ == THREAD_BLOCKED || t->state_ == THREAD_BLOCKED_READ_LOCK);
  DEBUG_ASSERT(t->blocking_wait_queue_ == source);
  DEBUG_ASSERT(source->collection_.Count() > 0);

  source->collection_.Remove(t);
  dest->collection_.Insert(t);
  t->blocking_wait_queue_ = dest;
}

/**
 * @brief  Wake all threads sleeping on a wait queue
 *
 * This function removes all threads (if any) from the wait queue and
 * makes them executable.  The new threads will be placed at the head of the
 * run queue.
 *
 * @param reschedule  If true, the newly-woken threads will run immediately.
 * @param wait_queue_error  The return value which the new thread will receive
 * from wait_queue_block().
 *
 * @return  The number of threads woken
 */
int WaitQueue::WakeAll(bool reschedule, zx_status_t wait_queue_error) {
  Thread* t;
  int ret = 0;

  // Note(johngro): See the note in wake_one.  On one should ever be calling
  // this method on an OwnedWaitQueue
  DEBUG_ASSERT(magic_ == kMagic);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  if (WAIT_QUEUE_VALIDATION) {
    ValidateQueue();
  }

  if (collection_.Count() == 0) {
    return 0;
  }

  struct list_node list = LIST_INITIAL_VALUE(list);

  // pop all the threads off the wait queue into the run queue
  // TODO: optimize with custom pop all routine
  while ((t = Peek())) {
    internal::wait_queue_dequeue_thread_internal(this, t, wait_queue_error);
    list_add_tail(&list, &t->wait_queue_state_.queue_node_);
    ret++;
  }

  DEBUG_ASSERT(ret > 0);
  DEBUG_ASSERT(collection_.Count() == 0);

  ktrace_ptr(TAG_KWAIT_WAKE, this, 0, 0);

  // wake up the new thread(s), putting it in a run queue on a cpu. reschedule if the local
  // cpu run queue was modified
  bool local_resched = Scheduler::Unblock(&list);
  if (reschedule && local_resched) {
    Scheduler::Reschedule();
  }

  return ret;
}

bool WaitQueue::IsEmpty() const {
  DEBUG_ASSERT_MAGIC_CHECK(this);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  return collection_.Count() == 0;
}

/**
 * @brief  Tear down a wait queue
 *
 * This panics if any threads were waiting on this queue, because that
 * would indicate a race condition for most uses of wait queues.  If a
 * thread is currently waiting, it could have been scheduled later, in
 * which case it would have called Block() on an invalid wait
 * queue.
 */
WaitQueue::~WaitQueue() {
  DEBUG_ASSERT_MAGIC_CHECK(this);

  if (collection_.Count() != 0) {
    panic("~WaitQueue() called on non-empty WaitQueue\n");
  }

  magic_ = 0;
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
zx_status_t WaitQueue::UnblockThread(Thread* t, zx_status_t wait_queue_error) {
  DEBUG_ASSERT(t->magic_ == THREAD_MAGIC);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  if (t->state_ != THREAD_BLOCKED && t->state_ != THREAD_BLOCKED_READ_LOCK) {
    return ZX_ERR_BAD_STATE;
  }

  WaitQueue* wq = t->blocking_wait_queue_;
  DEBUG_ASSERT(wq != nullptr);
  DEBUG_ASSERT_MAGIC_CHECK(wq);
  DEBUG_ASSERT(t->wait_queue_state_.InWaitQueue());

  if (WAIT_QUEUE_VALIDATION) {
    wq->ValidateQueue();
  }

  int old_wq_prio = wq->BlockedPriority();
  internal::wait_queue_dequeue_thread_internal(wq, t, wait_queue_error);
  internal::wait_queue_waiters_priority_changed(wq, old_wq_prio);

  if (Scheduler::Unblock(t)) {
    Scheduler::Reschedule();
  }

  return ZX_OK;
}

bool WaitQueue::PriorityChanged(Thread* t, int old_prio, PropagatePI propagate) {
  DEBUG_ASSERT(t->magic_ == THREAD_MAGIC);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());
  DEBUG_ASSERT(t->state_ == THREAD_BLOCKED || t->state_ == THREAD_BLOCKED_READ_LOCK);

  WaitQueue* wq = t->blocking_wait_queue_;
  DEBUG_ASSERT(wq != NULL);
  DEBUG_ASSERT_MAGIC_CHECK(wq);

  LTRACEF("%p %d -> %d\n", t, old_prio, t->scheduler_state_.effective_priority());

  // |t|'s effective priority has already been re-calculated.  If |t| is
  // currently at the head of the wait queue, then |t|'s old priority is the
  // previous priority of the wait queue.  Otherwise, it is the priority of
  // the wait queue as it stands before we re-insert |t|
  int old_wq_prio = (wq->Peek() == t) ? old_prio : wq->BlockedPriority();

  // simple algorithm: remove the thread from the queue and add it back
  // TODO: implement optimal algorithm depending on all the different edge
  // cases of how the thread was previously queued and what priority its
  // switching to.
  wq->collection_.Remove(t);
  wq->collection_.Insert(t);

  bool ret = (propagate == PropagatePI::Yes)
                 ? internal::wait_queue_waiters_priority_changed(wq, old_wq_prio)
                 : false;

  if (WAIT_QUEUE_VALIDATION) {
    t->blocking_wait_queue_->ValidateQueue();
  }
  return ret;
}
