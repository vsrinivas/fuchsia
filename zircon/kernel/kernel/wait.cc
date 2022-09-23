// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/wait.h"

#include <lib/ktrace.h>
#include <platform.h>
#include <trace.h>
#include <zircon/errors.h>

#include <kernel/auto_preempt_disabler.h>
#include <kernel/owned_wait_queue.h>
#include <kernel/scheduler.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <ktl/algorithm.h>
#include <ktl/move.h>

#include "kernel/wait_queue_internal.h"

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

#ifndef WAIT_QUEUE_DEPTH_TRACING_ENABLED
#define WAIT_QUEUE_DEPTH_TRACING_ENABLED false
#endif

static inline void WqTraceDepth(const WaitQueueCollection* collection, uint32_t depth) {
  if constexpr (WAIT_QUEUE_DEPTH_TRACING_ENABLED) {
    ktrace_probe(TraceEnabled<true>{}, TraceContext::Cpu, "wq_depth"_stringref,
                 reinterpret_cast<uint64_t>(collection), static_cast<uint64_t>(depth));
  }
}

// add expensive code to do a full validation of the wait queue at various entry points
// to this module.
#define WAIT_QUEUE_VALIDATION (0 || (LK_DEBUGLEVEL > 2))

// Wait queues come in 2 flavors (traditional and owned) which are distinguished
// using the magic number.  When DEBUG_ASSERT checking the magic number, check
// against both of the possible valid magic numbers.
#define DEBUG_ASSERT_MAGIC_CHECK(_queue)                                                 \
  DEBUG_ASSERT_MSG(                                                                      \
      ((_queue)->magic_ == kMagic) || ((_queue)->magic_ == OwnedWaitQueue::kOwnedMagic), \
      "magic 0x%08x", ((_queue)->magic_))

// There are a limited number of operations which should never be done on a
// WaitQueue which happens to be an OwnedWaitQueue.  Specifically, blocking.
// Blocking on an OWQ should always go through the OWQ specific
// BlockAndAssignOwner.  Add a macro to check for that as well.
#define DEBUG_ASSERT_MAGIC_AND_NOT_OWQ(_queue)                                          \
  do {                                                                                  \
    DEBUG_ASSERT_MSG(((_queue)->magic_ != OwnedWaitQueue::kOwnedMagic),                 \
                     "This operation should not be performed against the WaitQueue "    \
                     "API, use the OwnedWaitQueue API intead.");                        \
    DEBUG_ASSERT_MSG(((_queue)->magic_ == kMagic), "magic 0x%08x", ((_queue)->magic_)); \
  } while (false)

// Wait queues are building blocks that other locking primitives use to handle
// blocking threads.
void WaitQueue::TimeoutHandler(Timer* timer, zx_time_t now, void* arg) {
  Thread* thread = (Thread*)arg;

  thread->canary().Assert();

  // spin trylocking on the thread lock since the routine that set up the callback,
  // wait_queue_block, may be trying to simultaneously cancel this timer while holding the
  // thread_lock.
  if (timer->TrylockOrCancel(&thread_lock)) {
    return;
  }

  AnnotatedAutoPreemptDisabler aapd;
  UnblockThread(thread, ZX_ERR_TIMED_OUT);
  thread_lock.Release();
}

// Deal with the consequences of a change of maximum priority across the set of
// waiters in a wait queue.
void WaitQueue::UpdatePriority(int old_prio) TA_REQ(thread_lock, preempt_disabled_token) {
  // If this is an owned wait queue, and the maximum priority of its set of
  // waiters has changed, make sure to apply any needed priority inheritance.
  if ((magic_ == OwnedWaitQueue::kOwnedMagic) && (old_prio != BlockedPriority())) {
    static_cast<OwnedWaitQueue*>(this)->WaitersPriorityChanged(old_prio);
  }
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

Thread* WaitQueueCollection::Peek(zx_time_t signed_now) {
  // Find the "best" thread in the queue to run at time |now|.  See the comments
  // in thread.h, immediately above the definition of WaitQueueCollection for
  // details of how the data structure and this algorithm work.

  // If the collection is empty, there is nothing to do.
  if (threads_.is_empty()) {
    return nullptr;
  }

  // If the front of the collection has a key with the fair thread bit set in
  // it, then there are no deadline threads in the collection, and the front of
  // the queue is the proper choice.
  Thread& front = threads_.front();
  auto IsFair = [](const Thread& t) -> bool {
    const uint64_t key = t.wait_queue_state().blocked_threads_tree_sort_key_;
    return (key & kFairThreadSortKeyBit) != 0;
  };

  if (IsFair(front)) {
    // Front of the queue is a fair thread, which means that there are no
    // deadline threads in the queue.  This thread is our best choice.
    return &front;
  }

  // Looks like we have deadline threads waiting in the queue.  Is the absolute
  // deadline of the front of the queue in the future?  If so, then this is our
  // best choice.
  //
  // TODO(johngro): Is it actually worth this optimistic check, or would it be
  // better to simply do the search every time?
  DEBUG_ASSERT(signed_now >= 0);
  const uint64_t now = static_cast<uint64_t>(signed_now);
  if (front.wait_queue_state().blocked_threads_tree_sort_key_ > now) {
    return &front;
  }

  // Actually search the tree for the deadline thread with the smallest relative
  // deadline which is in the future relative to now.
  auto best_deadline = threads_.upper_bound({now, 0});
  if (best_deadline.IsValid() && !IsFair(*best_deadline)) {
    return &*best_deadline;
  }

  // Looks like we have deadline threads, but all of their deadlines have
  // expired.  Choose the thread with the minimum relative deadline in the tree.
  Thread* min_relative = threads_.root()->wait_queue_state().subtree_min_rel_deadline_thread_;
  DEBUG_ASSERT(min_relative != nullptr);
  return min_relative;
}

void WaitQueueCollection::Insert(Thread* thread) {
  WqTraceDepth(this, Count() + 1);

  auto& wq_state = thread->wait_queue_state();
  DEBUG_ASSERT(wq_state.blocked_threads_tree_sort_key_ == 0);
  DEBUG_ASSERT(wq_state.subtree_min_rel_deadline_thread_ == nullptr);

  // Pre-compute our sort key so that it does not have to be done every time we
  // need to compare our node against another node while we exist in the tree.
  //
  // See the comments in thread.h, immediately above the definition of
  // WaitQueueCollection for details of why we compute the key in this fashion.
  static_assert(SchedTime::Format::FractionalBits == 0,
                "WaitQueueCollection assumes that the raw_value() of a SchedTime is always a whole "
                "number of nanoseconds");
  static_assert(SchedDuration::Format::FractionalBits == 0,
                "WaitQueueCollection assumes that the raw_value() of a SchedDuration is always a "
                "whole number of nanoseconds");

  const auto& sched_state = thread->scheduler_state();
  if (sched_state.discipline() == SchedDiscipline::Fair) {
    // Statically assert that the offset we are going to add to a fair thread's
    // start time to form its virtual start time can never be the equivalent of
    // something more than ~1 year.  If the resolution of SchedWeight becomes
    // too fine, it could drive the sum of the thread's virtual start time into
    // saturation for low weight threads, making the key useless for sorting.
    // By putting a limit of 1 year on the offset, we know that the
    // current_time() of the system would need to be greater than 2^63
    // nanoseconds minus one year, or about 291 years, before this can happen.
    constexpr SchedWeight kMinPosWeight{ffl::FromRatio<int64_t>(1, SchedWeight::Format::Power)};
    constexpr SchedDuration OneYear{SchedMs(zx_duration_t(1) * 86400 * 365245)};
    static_assert(OneYear >= (Scheduler::kDefaultTargetLatency / kMinPosWeight),
                  "SchedWeight resolution is too fine");

    SchedTime key =
        sched_state.start_time() + (Scheduler::kDefaultTargetLatency / sched_state.fair().weight);
    wq_state.blocked_threads_tree_sort_key_ =
        static_cast<uint64_t>(key.raw_value()) | kFairThreadSortKeyBit;
  } else {
    wq_state.blocked_threads_tree_sort_key_ =
        static_cast<uint64_t>(sched_state.finish_time().raw_value());
  }

  threads_.insert(thread);
}

void WaitQueueCollection::Remove(Thread* thread) {
  WqTraceDepth(this, Count() - 1);
  threads_.erase(*thread);

  // In a debug build, zero out the sort key now that we have left the
  // collection.  This can help to find bugs by allowing us to assert that the
  // value is zero during insertion, however it is not strictly needed in a
  // production build and can be skipped.
#ifdef DEBUG_ASSERT_IMPLEMENTED
  auto& wq_state = thread->wait_queue_state();
  wq_state.blocked_threads_tree_sort_key_ = 0;
#endif
}

void WaitQueue::ValidateQueue() TA_REQ(thread_lock) {
  DEBUG_ASSERT_MAGIC_CHECK(this);
  thread_lock.AssertHeld();
}

////////////////////////////////////////////////////////////////////////////////
//
// Begin user facing API
//
////////////////////////////////////////////////////////////////////////////////

// return the numeric priority of the highest priority thread queued
int WaitQueue::BlockedPriority() const {
  // TODO(johngro): Remove this, as well as the concept of "priority" from all
  // of the OwnedWaitQueue and profile inheritance code.  The wait queue
  // ordering no longer depends on the deprecated concept of priority, and there
  // is no point in maintaining the system of inheriting the "maximum priority"
  // during inheritance events.
  //
  // Instead, PI will be switched over to inheriting the sum of the weights of
  // all of the upstream threads, modeling the weight of a deadline thread as
  // the weight of a "max priority" thread (as is done today).  This will be a
  // temporary stepping stone on the way to implementing generalize deadline
  // inheritance, which depends on knowing the minimum relative deadline across
  // a set of waiting threads, something which is already being maintained using
  // the WaitQueueCollection's augmented binary tree.
  int ret = -1;
  for (const Thread& t : collection_.threads_) {
    ret = ktl::max(ret, t.scheduler_state().effective_priority());
  }
  return ret;
}

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

  DEBUG_ASSERT_MAGIC_AND_NOT_OWQ(this);
  DEBUG_ASSERT(current_thread->state() == THREAD_RUNNING);

  // Any time a thread blocks, it should be holding exactly one spinlock, and it
  // should be the thread lock.  If a thread blocks while holding another spin
  // lock, something has gone very wrong.
  thread_lock.AssertHeld();
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
 * @param wait_queue_error  The return value which the new thread will receive
 * from wait_queue_block().
 *
 * @return  Whether a thread was woken
 */
bool WaitQueue::WakeOne(zx_status_t wait_queue_error) {
  Thread* t;
  bool woke = false;

  // Note(johngro): No one should ever calling wait_queue_wake_one on an
  // instance of an OwnedWaitQueue.  OwnedWaitQueues need to deal with
  // priority inheritance, and all wake operations on an OwnedWaitQueue should
  // be going through their interface instead.
  DEBUG_ASSERT(magic_ == kMagic);
  thread_lock.AssertHeld();

  if (WAIT_QUEUE_VALIDATION) {
    ValidateQueue();
  }

  t = Peek(current_time());
  if (t) {
    Dequeue(t, wait_queue_error);

    // Wake up the new thread, putting it in a run queue on a cpu.
    Scheduler::Unblock(t);
    woke = true;
  }

  return woke;
}

void WaitQueue::DequeueThread(Thread* t, zx_status_t wait_queue_error) {
  DEBUG_ASSERT_MAGIC_CHECK(this);
  thread_lock.AssertHeld();

  if (WAIT_QUEUE_VALIDATION) {
    ValidateQueue();
  }

  Dequeue(t, wait_queue_error);
}

void WaitQueue::MoveThread(WaitQueue* source, WaitQueue* dest, Thread* t) {
  DEBUG_ASSERT_MAGIC_CHECK(source);
  DEBUG_ASSERT_MAGIC_CHECK(dest);
  thread_lock.AssertHeld();

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
 * @param wait_queue_error  The return value which the new thread will receive
 * from wait_queue_block().
 *
 * @return  The number of threads woken
 */
void WaitQueue::WakeAll(zx_status_t wait_queue_error) {
  Thread* t;

  // Note(johngro): See the note in wake_one.  On one should ever be calling
  // this method on an OwnedWaitQueue
  DEBUG_ASSERT(magic_ == kMagic);
  thread_lock.AssertHeld();

  if (WAIT_QUEUE_VALIDATION) {
    ValidateQueue();
  }

  if (collection_.Count() == 0) {
    return;
  }

  // pop all the threads off the wait queue into the run queue
  // TODO(johngro): Look into ways to optimize this.  There is no real reason to:
  //
  // 1) Remove the threads from the collection in wake order.
  // 2) Rebalance the tree's wait queue collection during the process of
  //    removing the nodes.
  // 3) Have separate node storage in the thread for existing on the unblock
  //    list.
  //
  Thread::UnblockList list;
  zx_time_t now = current_time();
  while ((t = Peek(now))) {
    Dequeue(t, wait_queue_error);
    list.push_back(t);
  }

  DEBUG_ASSERT(collection_.Count() == 0);

  // Wake up the new thread(s), putting it in a run queue on a cpu.
  Scheduler::Unblock(ktl::move(list));
}

bool WaitQueue::IsEmpty() const {
  DEBUG_ASSERT_MAGIC_CHECK(this);
  thread_lock.AssertHeld();

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
  thread_lock.AssertHeld();

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

  Scheduler::Unblock(t);
  return ZX_OK;
}

void WaitQueue::PriorityChanged(Thread* t, int old_prio, PropagatePI propagate) {
  t->canary().Assert();
  thread_lock.AssertHeld();
  DEBUG_ASSERT(t->state() == THREAD_BLOCKED || t->state() == THREAD_BLOCKED_READ_LOCK);

  DEBUG_ASSERT(t->wait_queue_state().blocking_wait_queue_ == this);
  DEBUG_ASSERT_MAGIC_CHECK(this);

  LTRACEF("%p %d -> %d\n", t, old_prio, t->scheduler_state().effective_priority());

  // |t|'s effective priority has already been re-calculated.  If |t| is
  // currently at the head of this WaitQueue, then |t|'s old priority is the
  // previous priority of the WaitQueue.  Otherwise, it is the priority of
  // the WaitQueue as it stands before we re-insert |t|.
  const int old_wq_prio = (Peek(current_time()) == t) ? old_prio : BlockedPriority();

  // simple algorithm: remove the thread from the queue and add it back
  // TODO: implement optimal algorithm depending on all the different edge
  // cases of how the thread was previously queued and what priority its
  // switching to.
  collection_.Remove(t);
  collection_.Insert(t);

  if (propagate == PropagatePI::Yes) {
    UpdatePriority(old_wq_prio);
  }
  if (WAIT_QUEUE_VALIDATION) {
    ValidateQueue();
  }
}
