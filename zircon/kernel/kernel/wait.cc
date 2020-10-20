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
#include <ktl/move.h>

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

void WaitQueue::TimeoutHandler(Timer* timer, zx_time_t now, void* arg) {
  Thread* thread = (Thread*)arg;

  thread->canary().Assert();

  // spin trylocking on the thread lock since the routine that set up the callback,
  // wait_queue_block, may be trying to simultaneously cancel this timer while holding the
  // thread_lock.
  if (timer->TrylockOrCancel(&thread_lock)) {
    return;
  }

  UnblockThread(thread, ZX_ERR_TIMED_OUT);

  thread_lock.Release();
}

// Deal with the consequences of a change of maximum priority across the set of
// waiters in a wait queue.
bool WaitQueue::UpdatePriority(int old_prio) TA_REQ(thread_lock) {
  // If this is an owned wait queue, and the maximum priority of its set of
  // waiters has changed, make sure to apply any needed priority inheritance.
  if ((magic_ == OwnedWaitQueue::kOwnedMagic) && (old_prio != BlockedPriority())) {
    return static_cast<OwnedWaitQueue*>(this)->WaitersPriorityChanged(old_prio);
  }

  return false;
}

// Remove a thread from a wait queue, maintain the wait queue's internal count,
// and update the WaitQueue specific bookkeeping in the thread in the process.
void WaitQueue::Dequeue(Thread* t, zx_status_t wait_queue_error) TA_REQ(thread_lock) {
  DEBUG_ASSERT(t != nullptr);
  DEBUG_ASSERT(t->wait_queue_state().InWaitQueue());
  DEBUG_ASSERT(t->state() == THREAD_BLOCKED || t->state() == THREAD_BLOCKED_READ_LOCK);
  DEBUG_ASSERT(t->wait_queue_state().blocking_wait_queue_ == this);

  collection_.Remove(t);
  t->wait_queue_state().blocked_status_ = wait_queue_error;
  t->wait_queue_state().blocking_wait_queue_ = nullptr;
}

Thread* WaitQueueCollection::Peek() {
  if (heads_.is_empty()) {
    return nullptr;
  }
  return &heads_.front();
}

const Thread* WaitQueueCollection::Peek() const {
  if (heads_.is_empty()) {
    return nullptr;
  }
  return &heads_.front();
}

void WaitQueueCollection::Insert(Thread* thread) {
  // Regardless of the state of the collection, the count goes up one.
  ++count_;

  if (unlikely(!heads_.is_empty())) {
    const int pri = thread->scheduler_state().effective_priority();

    // Walk through the sorted list of wait queue heads.
    for (Thread& head : heads_) {
      if (pri > head.scheduler_state().effective_priority()) {
        // Insert ourself here as a new queue head, before |head|.
        heads_.insert(head, thread);
        return;
      } else if (head.scheduler_state().effective_priority() == pri) {
        // Same priority, add ourself to the tail of this queue.
        head.wait_queue_state().sublist_.push_back(thread);
        return;
      }
    }
  }

  // We're the first thread, or we walked off the end, so add ourself
  // as a new queue head at the end.
  heads_.push_back(thread);
}

void WaitQueueCollection::Remove(Thread* thread) {
  // Either way, the count goes down one.
  --count_;

  if (!thread->wait_queue_state().IsHead()) {
    // We're just in a queue, not a head.
    thread->wait_queue_state().sublist_node_.RemoveFromContainer<WaitQueueSublistTrait>();
  } else {
    // We're the head of a queue.
    if (thread->wait_queue_state().sublist_.is_empty()) {
      // If there's no new queue head, the only work we have to do is
      // removing |thread| from the heads list.
      heads_.erase(*thread);
    } else {
      // To migrate to the new queue head, we need to:
      // - update the sublist for this priority, by removing |newhead|.
      // - move the sublist from |thread| to |newhead|.
      // - replace |thread| with |newhead| in the heads list.

      // Remove the newhead from its position in the sublist.
      Thread* newhead = thread->wait_queue_state().sublist_.pop_front();

      // Move the sublist from |thread| to |newhead|.
      newhead->wait_queue_state().sublist_ = ktl::move(thread->wait_queue_state().sublist_);

      // Patch in the new head into the queue head list.
      heads_.replace(*thread, newhead);
    }
  }
}

void WaitQueueCollection::Validate() const {
  // Validate that the queue is sorted properly
  const Thread* last_head = nullptr;
  for (const Thread& head : heads_) {
    head.canary().Assert();

    // Validate that the queue heads are sorted high to low priority.
    if (last_head) {
      DEBUG_ASSERT_MSG(last_head->scheduler_state().effective_priority() >
                           head.scheduler_state().effective_priority(),
                       "%p:%d  %p:%d", last_head, last_head->scheduler_state().effective_priority(),
                       &head, head.scheduler_state().effective_priority());
    }

    // Walk any threads linked to this head, validating that they're the same priority.
    for (const Thread& thread : head.wait_queue_state().sublist_) {
      thread.canary().Assert();
      DEBUG_ASSERT_MSG(head.scheduler_state().effective_priority() ==
                           thread.scheduler_state().effective_priority(),
                       "%p:%d  %p:%d", &head, head.scheduler_state().effective_priority(), &thread,
                       thread.scheduler_state().effective_priority());
    }

    last_head = &head;
  }
}

void WaitQueue::ValidateQueue() TA_REQ(thread_lock) {
  DEBUG_ASSERT_MAGIC_CHECK(this);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  collection_.Validate();
}

////////////////////////////////////////////////////////////////////////////////
//
// Begin user facing API
//
////////////////////////////////////////////////////////////////////////////////

// return the numeric priority of the highest priority thread queued
int WaitQueue::BlockedPriority() const {
  const Thread* t = Peek();
  if (!t) {
    return -1;
  }

  return t->scheduler_state().effective_priority();
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
 * @param  deadline       The time at which to abort the wait
 * @param  slack          The amount of time it is acceptable to deviate from deadline
 * @param  signal_mask    Mask of existing signals to ignore
 * @param  reason         Reason for the block
 * @param  interruptible  Whether the block can be interrupted
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
                                ResourceOwnership reason, Interruptible interruptible)
    TA_REQ(thread_lock) {
  Thread* current_thread = Thread::Current::Get();

  DEBUG_ASSERT_MAGIC_CHECK(this);
  DEBUG_ASSERT(current_thread->state() == THREAD_RUNNING);
  DEBUG_ASSERT(arch_ints_disabled());

  // Any time a thread blocks, it should be holding exactly one spinlock, and it
  // should be the thread lock.  If a thread blocks while holding another spin
  // lock, something has gone very wrong.
  DEBUG_ASSERT(thread_lock.IsHeld());
  DEBUG_ASSERT(arch_num_spinlocks_held() == 1);

  if (WAIT_QUEUE_VALIDATION) {
    ValidateQueue();
  }

  zx_status_t res = BlockEtcPreamble(deadline, signal_mask, reason, interruptible);
  if (res != ZX_OK) {
    return res;
  }

  return BlockEtcPostamble(deadline);
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
 * @return  Whether a thread was woken
 */
bool WaitQueue::WakeOne(bool reschedule, zx_status_t wait_queue_error) {
  Thread* t;
  bool woke = false;

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
    Dequeue(t, wait_queue_error);

    ktrace_ptr(TAG_KWAIT_WAKE, this, 0, 0);

    // wake up the new thread, putting it in a run queue on a cpu. reschedule if the local
    // cpu run queue was modified
    bool local_resched = Scheduler::Unblock(t);
    if (reschedule && local_resched) {
      Scheduler::Reschedule();
    }

    woke = true;
  }

  return woke;
}

void WaitQueue::DequeueThread(Thread* t, zx_status_t wait_queue_error) {
  DEBUG_ASSERT_MAGIC_CHECK(this);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  if (WAIT_QUEUE_VALIDATION) {
    ValidateQueue();
  }

  Dequeue(t, wait_queue_error);
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
  DEBUG_ASSERT(t->wait_queue_state().InWaitQueue());
  DEBUG_ASSERT(t->state() == THREAD_BLOCKED || t->state() == THREAD_BLOCKED_READ_LOCK);
  DEBUG_ASSERT(t->wait_queue_state().blocking_wait_queue_ == source);
  DEBUG_ASSERT(source->collection_.Count() > 0);

  source->collection_.Remove(t);
  dest->collection_.Insert(t);
  t->wait_queue_state().blocking_wait_queue_ = dest;
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
void WaitQueue::WakeAll(bool reschedule, zx_status_t wait_queue_error) {
  Thread* t;

  // Note(johngro): See the note in wake_one.  On one should ever be calling
  // this method on an OwnedWaitQueue
  DEBUG_ASSERT(magic_ == kMagic);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  if (WAIT_QUEUE_VALIDATION) {
    ValidateQueue();
  }

  if (collection_.Count() == 0) {
    return;
  }

  WaitQueueSublist list;

  // pop all the threads off the wait queue into the run queue
  // TODO: optimize with custom pop all routine
  while ((t = Peek())) {
    Dequeue(t, wait_queue_error);
    list.push_back(t);
  }

  DEBUG_ASSERT(collection_.Count() == 0);

  ktrace_ptr(TAG_KWAIT_WAKE, this, 0, 0);

  // wake up the new thread(s), putting it in a run queue on a cpu. reschedule if the local
  // cpu run queue was modified
  bool local_resched = Scheduler::Unblock(ktl::move(list));
  if (reschedule && local_resched) {
    Scheduler::Reschedule();
  }
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
  t->canary().Assert();
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  if (t->state() != THREAD_BLOCKED && t->state() != THREAD_BLOCKED_READ_LOCK) {
    return ZX_ERR_BAD_STATE;
  }

  WaitQueue* wq = t->wait_queue_state().blocking_wait_queue_;
  DEBUG_ASSERT(wq != nullptr);
  DEBUG_ASSERT_MAGIC_CHECK(wq);
  DEBUG_ASSERT(t->wait_queue_state().InWaitQueue());

  if (WAIT_QUEUE_VALIDATION) {
    wq->ValidateQueue();
  }

  int old_wq_prio = wq->BlockedPriority();
  wq->Dequeue(t, wait_queue_error);
  wq->UpdatePriority(old_wq_prio);

  if (Scheduler::Unblock(t)) {
    Scheduler::Reschedule();
  }

  return ZX_OK;
}

bool WaitQueue::PriorityChanged(Thread* t, int old_prio, PropagatePI propagate) {
  t->canary().Assert();
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());
  DEBUG_ASSERT(t->state() == THREAD_BLOCKED || t->state() == THREAD_BLOCKED_READ_LOCK);

  DEBUG_ASSERT(t->wait_queue_state().blocking_wait_queue_ == this);
  DEBUG_ASSERT_MAGIC_CHECK(this);

  LTRACEF("%p %d -> %d\n", t, old_prio, t->scheduler_state().effective_priority());

  // |t|'s effective priority has already been re-calculated.  If |t| is
  // currently at the head of this WaitQueue, then |t|'s old priority is the
  // previous priority of the WaitQueue.  Otherwise, it is the priority of
  // the WaitQueue as it stands before we re-insert |t|.
  int old_wq_prio = (Peek() == t) ? old_prio : BlockedPriority();

  // simple algorithm: remove the thread from the queue and add it back
  // TODO: implement optimal algorithm depending on all the different edge
  // cases of how the thread was previously queued and what priority its
  // switching to.
  collection_.Remove(t);
  collection_.Insert(t);

  bool ret = (propagate == PropagatePI::Yes) ? UpdatePriority(old_wq_prio) : false;

  if (WAIT_QUEUE_VALIDATION) {
    ValidateQueue();
  }
  return ret;
}
