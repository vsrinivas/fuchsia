// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/owned_wait_queue.h"

#include <lib/counters.h>

#include <arch/mp.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <kernel/sched.h>
#include <kernel/wait_queue_internal.h>
#include <ktl/popcount.h>
#include <ktl/type_traits.h>

// Notes on the defined kernel counters.
//
// Adjustments (aka promotions and demotions)
// The number of times that a thread increased or decreased its priority because
// of a priority inheritance related event.
//
// Note that the number of promotions does not have to equal the number of
// demotions in the system.  For example, a thread could slowly climb up in priority as
// threads of increasing priority join a wait queue it owns, then suddenly drop
// back down to its base priority when it releases its queue.
//
// There are other (more complicated) sequences which could cause a thread to
// jump up in priority with one promotion, then slowly step back down again over
// multiple demotions.
//
// Reschedule events.
// Counts of the number of times that a local reschedule was requested, as well
// as the total number of reschedule IPIs which were sent, as a result of
// priority inheritance related events.
//
// Max chain traversal.
// The maximum traversed length of a PI chain during exection of the propagation
// algorithm.
//
// IOW - if a change to a wait queue's maximum effective priority ends up
// changing the inherited priority of thread A, but nothing else is needed, this
// is a traversal length of 1.  OTOH, if thread A was blocked by a wait queue
// (Qa) which was owned by thread B, and Qa's maximum effective priority, then
// the algorithm would need to traverse another link in the chain, and our
// traversed chain length would be at least 2.
//
// Note that the maximum traversed chain length does not have to be the length
// maximum PI chain ever assembled in the system.  This is a result of the fact
// that the PI algortim attempt to terminate propagation as soon as it can, as
// well as the fact that changes can start to propagate in the middle of a chain
// instead of being required to start at the end (for example, 2 chains of
// length 2 could merge to form a chain of length 4, but still result in a
// traversal of only length 1).
KCOUNTER(pi_promotions, "kernel.pi.adj.promotions")
KCOUNTER(pi_demotions, "kernel.pi.adj.demotions")
KCOUNTER(pi_triggered_local_reschedules, "kernel.pi.resched.local")
KCOUNTER(pi_triggered_ipis, "kernel.pi.resched.ipis")
KCOUNTER_DECLARE(max_pi_chain_traverse, "kernel.pi.max_chain_traverse", Max)

namespace {

enum class PiTracingLevel {
  // No tracing of PI events will happen
  None,

  // Only PI events which result in change of a target's effective priority
  // will be traced.
  Normal,

  // PI events which result in change of either a target's effective or
  // inherited priority will be traced.
  Extended,
};

// Compile time control of whether recursion and infinite loop guards are
// enabled.  By default, guards are enabled in everything but release builds.
constexpr bool kEnablePiChainGuards = LK_DEBUGLEVEL > 0;

// Default tracing level is Normal
constexpr PiTracingLevel kDefaultPiTracingLevel = PiTracingLevel::Normal;

// A couple of small stateful helper classes which drop out of release builds
// which perform some sanity checks for us when propagating priority
// inheritance.  In specific, we want to make sure that...
//
// ++ We never recurse from any of the calls we make into the scheduler into
//    this code.
// ++ When propagating iteratively, we are always making progress, and we never
//    exceed any completely insane limits for a priority inheritance chain.
template <bool Enable = kEnablePiChainGuards>
class RecursionGuard;
template <bool Enable = kEnablePiChainGuards>
class InfiniteLoopGuard;

template <>
class RecursionGuard<false> {
 public:
  void Acquire() {}
  void Release() {}
};

template <>
class RecursionGuard<true> {
 public:
  constexpr RecursionGuard() = default;

  void Acquire() {
    ASSERT(!acquired_);
    acquired_ = true;
  }
  void Release() { acquired_ = false; }

 private:
  bool acquired_ = false;
};

template <>
class InfiniteLoopGuard<false> {
 public:
  constexpr InfiniteLoopGuard() = default;
  void CheckProgress(uint32_t) {}
};

template <>
class InfiniteLoopGuard<true> {
 public:
  constexpr InfiniteLoopGuard() = default;
  void CheckProgress(uint32_t next) {
    // ASSERT that we are making progress
    ASSERT(expected_next_ == next);
    expected_next_ = next + 1;

    // ASSERT that we have not exceeded any completely ludicrous loop
    // bounds.  Note that in practice, a PI chain can technically be as long
    // as the user has resources for.  In reality, chains tend to be 2-3
    // nodes long at most.  If we see anything on the order to 2000, it
    // almost certainly indicates that something went Very Wrong, and we
    // should stop and investigate.
    constexpr uint32_t kMaxChainLen = 2048;
    ASSERT(next <= kMaxChainLen);
  }

 private:
  uint32_t expected_next_ = 1;
};

// Update our reschedule related kernel counters and send a request for any
// needed IPIs.
inline void UpdateStatsAndSendIPIs(bool local_resched, cpu_mask_t accum_cpu_mask)
    TA_REQ(thread_lock) {
  accum_cpu_mask &= ~cpu_num_to_mask(arch_curr_cpu_num());

  if (accum_cpu_mask) {
    mp_reschedule(accum_cpu_mask, 0);
    pi_triggered_ipis.Add(ktl::popcount(accum_cpu_mask));
  }

  if (local_resched) {
    pi_triggered_local_reschedules.Add(1);
  }
}

template <PiTracingLevel Level = kDefaultPiTracingLevel, typename = void>
class PiKTracer;

// Disabled PiKTracer stores nothing and does nothing.
template <>
class PiKTracer<PiTracingLevel::None> {
 public:
  void Trace(thread_t* t, int old_effec_prio, int new_effec_prio) {}
};

struct PiKTracerFlowIdGenerator {
 private:
  friend class PiKTracer<PiTracingLevel::Normal>;
  friend class PiKTracer<PiTracingLevel::Extended>;
  inline static ktl::atomic<uint32_t> gen_{0};
};

template <PiTracingLevel Level>
class PiKTracer<Level, ktl::enable_if_t<(Level == PiTracingLevel::Normal) ||
                                        (Level == PiTracingLevel::Extended)>> {
 public:
  PiKTracer() = default;
  ~PiKTracer() { Flush(FlushType::FINAL); }

  void Trace(thread_t* t, int old_effec_prio, int old_inherited_prio) {
    if ((old_effec_prio != t->effec_priority) ||
        ((Level == PiTracingLevel::Extended) && (old_inherited_prio != t->inherited_priority))) {
      if (thread_ == nullptr) {
        // Generate the start event and a flow id.
        flow_id_ = PiKTracerFlowIdGenerator::gen_.fetch_add(1, std::memory_order_relaxed);
        ktrace(TAG_INHERIT_PRIORITY_START, flow_id_, 0, 0, arch_curr_cpu_num());
      } else {
        // Flush the previous event, but do not declare it to be the last in
        // the flow.
        Flush(FlushType::INTERMEDIATE);
      }

      // Record the info we will need for the subsequent event to be logged.
      // We don't want to actually log this event until we know whether or not
      // it will be the final event in the flow.
      thread_ = t;
      priorities_ = (old_effec_prio & 0xFF) | ((t->effec_priority & 0xFF) << 8) |
                    ((old_inherited_prio & 0xFF) << 16) | ((t->inherited_priority & 0xFF) << 24);
    }
  }

 private:
  enum class FlushType { FINAL, INTERMEDIATE };

  void Flush(FlushType type) {
    if (!thread_) {
      return;
    }

    uint32_t tid;
    uint32_t flags;
    if (thread_->user_thread != nullptr) {
      tid = static_cast<uint32_t>(thread_->user_tid);
      flags = static_cast<uint32_t>(arch_curr_cpu_num());
    } else {
      tid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(thread_));
      flags = static_cast<uint32_t>(arch_curr_cpu_num()) | KTRACE_FLAGS_INHERIT_PRIORITY_KERNEL_TID;
    }

    if (type == FlushType::FINAL) {
      flags |= KTRACE_FLAGS_INHERIT_PRIORITY_FINAL_EVT;
    }

    ktrace(TAG_INHERIT_PRIORITY, flow_id_, tid, priorities_, flags);
  }

  thread_t* thread_ = nullptr;
  uint32_t flow_id_ = 0;
  uint32_t priorities_ = 0;
};

RecursionGuard qpc_recursion_guard;

}  // namespace

OwnedWaitQueue::~OwnedWaitQueue() {
  // Something is very very wrong if we have been allowed to destruct while we
  // still have an owner.
  DEBUG_ASSERT(owner_ == nullptr);
}

void OwnedWaitQueue::DisownAllQueues(thread_t* t) {
  // It is important that this thread not be blocked by any other wait queues
  // during this operation.  If it was possible for the thread to be blocked,
  // we would need to update all of the PI chain bookkeeping too.
  DEBUG_ASSERT(t->blocking_wait_queue == nullptr);

  for (auto& q : t->owned_wait_queues) {
    DEBUG_ASSERT(q.owner_ == t);
    q.owner_ = nullptr;
  }

  t->owned_wait_queues.clear();
}

bool OwnedWaitQueue::QueuePressureChanged(thread_t* t, int old_prio, int new_prio,
                                          cpu_mask_t* accum_cpu_mask) TA_REQ(thread_lock) {
  fbl::AutoLock guard(&qpc_recursion_guard);
  DEBUG_ASSERT(old_prio != new_prio);

  bool local_resched = false;
  uint32_t traverse_len = 1;

  // When we have finally finished updating everything, make sure to update
  // our max traversal statistic.
  //
  // Note, the only real reason that this is an accurate max at all is because
  // the counter is effectively protected by the thread lock (although there
  // is no real good way to annotate that fact).
  auto on_exit = fbl::MakeAutoCall([&traverse_len]() {
    auto old = max_pi_chain_traverse.Value();
    if (old < traverse_len) {
      max_pi_chain_traverse.Add(traverse_len - old);
    }
  });

  DEBUG_ASSERT(t != nullptr);

  PiKTracer tracer;
  InfiniteLoopGuard inf_loop_guard;
  while (true) {
    inf_loop_guard.CheckProgress(traverse_len);
    if (new_prio < old_prio) {
      // If the pressure just dropped, but the old pressure was strictly
      // lower than the current inherited priority of the thread, then
      // there is nothing to do.  We can just stop.  The maximum inherited
      // priority must have come from a different wait queue.
      //
      if (old_prio < t->inherited_priority) {
        return local_resched;
      }

      // Since the pressure from one of our queues just dropped, we need
      // to recompute the new maximum priority across all of the wait
      // queues currently owned by this thread.
      __UNUSED int orig_new_prio = new_prio;
      for (const auto& owq : t->owned_wait_queues) {
        int queue_prio = wait_queue_blocked_priority(&owq);

        // If our bookkeeping is accurate, it should be impossible for
        // our original new priority to be greater than the priority of
        // any of the queues currently owned by this thread.
        DEBUG_ASSERT(orig_new_prio <= queue_prio);
        new_prio = fbl::max(new_prio, queue_prio);
      }

      // If our calculated new priority is still the same as our current
      // inherited priority, then we are done.
      if (new_prio == t->inherited_priority) {
        return local_resched;
      }
    } else {
      // Likewise, if the pressure just went up, but the new pressure is
      // not strictly higher than the current inherited priority, then
      // there is nothing to do.
      if (new_prio <= t->inherited_priority) {
        return local_resched;
      }
    }

    // OK, at this point in time, we know that there has been a change to
    // our inherited priority.  Update it, and check to see if that resulted
    // in a change of the maximum waiter priority of the wait queue blocking
    // this thread (if any).  If not, then we are done.
    const wait_queue_t* bwq = t->blocking_wait_queue;
    int old_effec_prio = t->effec_priority;
    int old_inherited_prio = t->inherited_priority;
    int old_queue_prio = bwq ? wait_queue_blocked_priority(bwq) : -1;
    int new_queue_prio;

    sched_inherit_priority(t, new_prio, &local_resched, accum_cpu_mask);

    new_queue_prio = bwq ? wait_queue_blocked_priority(bwq) : -1;

    // If the effective priority of this thread has gone up or down, record
    // it in the kernel counters as a PI promotion or demotion.
    if (old_effec_prio != t->effec_priority) {
      if (old_effec_prio < t->effec_priority) {
        pi_promotions.Add(1);
      } else {
        pi_demotions.Add(1);
      }
    }

    // Trace the change in priority if enabled.
    tracer.Trace(t, old_effec_prio, old_inherited_prio);

    if (old_queue_prio == new_queue_prio) {
      return local_resched;
    }

    // It looks the change of this thread's inherited priority affected its
    // blocking wait queue in a meaningful way.  If this wait_queue is an
    // OwnedWait queue, and it currently has an owner, then continue to
    // propagate the change.  Otherwise, we are done.
    if ((bwq != nullptr) && (bwq->magic == OwnedWaitQueue::MAGIC)) {
      t = static_cast<const OwnedWaitQueue*>(bwq)->owner();
      if (t != nullptr) {
        old_prio = old_queue_prio;
        new_prio = new_queue_prio;
        ++traverse_len;
        continue;
      }
    }

    return local_resched;
  }
}

bool OwnedWaitQueue::WaitersPriorityChanged(int old_prio) {
  if (owner() == nullptr) {
    return false;
  }

  int new_prio = wait_queue_blocked_priority(this);
  if (old_prio == new_prio) {
    return false;
  }

  cpu_mask_t accum_cpu_mask = 0;
  bool local_resched = QueuePressureChanged(owner(), old_prio, new_prio, &accum_cpu_mask);
  UpdateStatsAndSendIPIs(local_resched, accum_cpu_mask);
  return local_resched;
}

bool OwnedWaitQueue::UpdateBookkeeping(thread_t* new_owner, int old_prio,
                                       cpu_mask_t* out_accum_cpu_mask) {
  int new_prio = wait_queue_blocked_priority(this);
  bool local_resched = false;
  cpu_mask_t accum_cpu_mask = 0;

  // The new owner may not be a dying thread.
  if ((new_owner != nullptr) && (new_owner->state == THREAD_DEATH)) {
    new_owner = nullptr;
  }

  if (new_owner == owner()) {
    // The owner has not changed.  If there never was an owner, or there is
    // an owner but the queue pressure has not changed, then there is
    // nothing we need to do.
    if ((owner() == nullptr) || (new_prio == old_prio)) {
      return false;
    }

    local_resched = QueuePressureChanged(owner(), old_prio, new_prio, &accum_cpu_mask);
  } else {
    // Looks like the ownership has actually changed.  Start releasing
    // ownership and propagating the PI consequences for the old owner (if
    // any).
    thread_t* old_owner = owner();
    if (old_owner != nullptr) {
      DEBUG_ASSERT(this->InContainer());
      old_owner->owned_wait_queues.erase(*this);
      owner_ = nullptr;

      if ((old_prio >= 0) && QueuePressureChanged(old_owner, old_prio, -1, &accum_cpu_mask)) {
        local_resched = true;
      }

      // If we no longer own any queues, then we had better not be inheriting any priority at
      // this point in time.
      DEBUG_ASSERT(!old_owner->owned_wait_queues.is_empty() ||
                   (old_owner->inherited_priority == -1));
    }

    // Update to the new owner.  If there is a new owner, fix the
    // bookkeeping.  Then, if there are waiters in the queue (therefore,
    // non-negative pressure), then apply that pressure now.
    owner_ = new_owner;
    if (new_owner != nullptr) {
      DEBUG_ASSERT(!this->InContainer());
      new_owner->owned_wait_queues.push_back(this);

      if ((new_prio >= 0) && QueuePressureChanged(new_owner, -1, new_prio, &accum_cpu_mask)) {
        local_resched = true;
      }
    }
  }

  if (out_accum_cpu_mask != nullptr) {
    *out_accum_cpu_mask |= accum_cpu_mask;
  } else {
    UpdateStatsAndSendIPIs(local_resched, accum_cpu_mask);
  }

  return local_resched;
}

bool OwnedWaitQueue::WakeThreadsInternal(uint32_t wake_count, thread_t** out_new_owner,
                                         Hook on_thread_wake_hook) {
  DEBUG_ASSERT(magic == MAGIC);
  DEBUG_ASSERT(out_new_owner != nullptr);

  // Note: This methods relies on the wait queue to be kept sorted in the
  // order that the scheduler would prefer to wake threads.
  *out_new_owner = nullptr;

  // If our wake_count is zero, then there is nothing to do.
  if (wake_count == 0) {
    return false;
  }

  bool do_resched = false;
  uint32_t woken = 0;
  ForeachThread([&](thread_t* t) TA_REQ(thread_lock) -> bool {
    // Call the user supplied hook and let them decide what to do with this
    // thread (updating their own bookkeeping in the process)
    using Action = Hook::Action;
    Action action = on_thread_wake_hook(t);

    // If the user wants to skip this thread, just move on to the next.
    if (action == Action::Skip) {
      return true;
    }

    if (action == Action::Stop) {
      return false;
    }

    // All other choices involve waking up this thread, so go ahead and do that now.
    ++woken;
    DequeueThread(t, ZX_OK);
    if (sched_unblock(t)) {
      do_resched = true;
    }

    // If we are supposed to keep going, do so now if we are still permitted
    // to wake more threads.
    if (action == Action::SelectAndKeepGoing) {
      return (woken < wake_count);
    }

    // No matter what the user chose at this point, we are going to stop
    // after this.  If the user requested that we assign ownership to the
    // thread we just woke, make sure that we have not woken any other
    // threads, and return a pointer to the thread who is to become the new
    // owner if there are still threads waiting in the queue.
    if (action == Action::SelectAndAssignOwner) {
      DEBUG_ASSERT(woken == 1);
      if (!IsEmpty()) {
        *out_new_owner = t;
      }
    } else {
      // SelectAndStop is the only remaining valid option.
      DEBUG_ASSERT(action == Action::SelectAndStop);
    }

    return false;
  });

  return do_resched;
}

zx_status_t OwnedWaitQueue::BlockAndAssignOwner(const Deadline& deadline, thread_t* new_owner,
                                                ResourceOwnership resource_ownership) {
  thread_t* current_thread = get_current_thread();

  DEBUG_ASSERT(magic == MAGIC);
  DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  // Remember what the maximum effective priority of the wait queue was before
  // we add current_thread to it.
  int old_queue_prio = wait_queue_blocked_priority(this);

  // Perform the first half of the block_etc operation.  If this fails, then
  // the state of the actual wait queue is unchanged and we can just get out
  // now.
  zx_status_t res = internal::wait_queue_block_etc_pre(this, deadline, 0u, resource_ownership);
  if (res != ZX_OK) {
    // There are only three reasons why the pre-wait operation should ever fail.
    //
    // 1) ZX_ERR_TIMED_OUT            : The timeout has already expired.
    // 2) ZX_ERR_INTERNAL_INTR_KILLED : The thread has been signaled for death.
    // 3) ZX_ERR_INTERNAL_INTR_RETRY  : The thread has been signaled for suspend.
    //
    // No matter what, we are not actually going to block in the wait queue.
    // Even so, however, we still need to assign the owner to what was
    // requested by the thread.  Just because we didn't manage to block does
    // not mean that ownership assignment gets skipped.
    ZX_DEBUG_ASSERT((res == ZX_ERR_TIMED_OUT) || (res == ZX_ERR_INTERNAL_INTR_KILLED) ||
                    (res == ZX_ERR_INTERNAL_INTR_RETRY));
    if (AssignOwner(new_owner)) {
      sched_reschedule();
    }

    return res;
  }

  // Success.  The current thread has passed all of its sanity checks and been
  // added to the wait queue.  Go ahead and update our priority inheritance
  // bookkeeping since both ownership and current PI pressure may have changed
  // (ownership because of |new_owner| and pressure because of the addition of
  // the thread to the queue.
  //
  // Note: it is safe to ignore the local reschedule hint passed back from
  // UpdateBookkeeping.  We are just about to block this thread, which is
  // going to trigger a reschedule anyway.
  __UNUSED bool local_resched;
  local_resched = UpdateBookkeeping(new_owner, old_queue_prio);

  // Finally, go ahead and run the second half of the block_etc operation.
  // This will actually block our thread and handle setting any timeout timers
  // in the process.
  return internal::wait_queue_block_etc_post(this, deadline);
}

bool OwnedWaitQueue::WakeThreads(uint32_t wake_count, Hook on_thread_wake_hook) {
  DEBUG_ASSERT(magic == MAGIC);

  thread_t* new_owner;
  int old_queue_prio = wait_queue_blocked_priority(this);
  bool local_resched = WakeThreadsInternal(wake_count, &new_owner, on_thread_wake_hook);

  if (UpdateBookkeeping(new_owner, old_queue_prio)) {
    local_resched = true;
  }

  return local_resched;
}

bool OwnedWaitQueue::WakeAndRequeue(uint32_t wake_count, OwnedWaitQueue* requeue_target,
                                    uint32_t requeue_count, thread_t* requeue_owner,
                                    Hook on_thread_wake_hook, Hook on_thread_requeue_hook) {
  DEBUG_ASSERT(magic == MAGIC);
  DEBUG_ASSERT(requeue_target != nullptr);
  DEBUG_ASSERT(requeue_target->magic == MAGIC);

  // If the potential new owner of the requeue wait queue is already dead,
  // then it cannot become the owner of the requeue wait queue.
  if (requeue_owner != nullptr) {
    // It should not be possible for a thread which is not yet running to be
    // declared as the owner of an OwnedWaitQueue.  Any attempts to assign
    // ownership to a thread which is not yet started should have been rejected
    // by layers of code above us, and a proper status code returned to the
    // user.
    DEBUG_ASSERT(requeue_owner->state != THREAD_INITIAL);
    if (requeue_owner->state == THREAD_DEATH) {
      requeue_owner = nullptr;
    }
  }

  // Remember what our queue priorities had been.  We will need this when it
  // comes time to update the PI chains.
  int old_wake_prio = wait_queue_blocked_priority(this);
  int old_requeue_prio = wait_queue_blocked_priority(requeue_target);

  thread_t* new_wake_owner;
  bool local_resched = WakeThreadsInternal(wake_count, &new_wake_owner, on_thread_wake_hook);

  // If there are still threads left in the wake queue (this), and we were asked to
  // requeue threads, then do so.
  if (!this->IsEmpty() && requeue_count) {
    uint32_t requeued = 0;
    ForeachThread([&](thread_t* t) TA_REQ(thread_lock) -> bool {
      // Call the user's requeue hook so that we can decide what to do
      // with this thread.
      using Action = Hook::Action;
      Action action = on_thread_requeue_hook(t);

      // It is illegal to ask for a requeue operation to assign ownership.
      DEBUG_ASSERT(action != Action::SelectAndAssignOwner);

      // If the user wants to skip this thread, just move on to the next.
      if (action == Action::Skip) {
        return true;
      }

      if (action == Action::Stop) {
        return false;
      }

      // Actually move the thread from this to the requeue_target.
      WaitQueue::MoveThread(this, requeue_target, t);
      ++requeued;

      // Are we done?
      if (action == Action::SelectAndStop) {
        return false;
      }

      // SelectAndKeepGoing is the only legal choice left.
      DEBUG_ASSERT(action == Action::SelectAndKeepGoing);
      return (requeued < requeue_count);
    });
  }

  // Now that we are finished moving everyone around, update the ownership of
  // the queues involved in the operation.  These updates should deal with
  // propagating any priority inheritance consequences of the requeue operation.
  cpu_mask_t accum_cpu_mask = 0;
  bool pi_resched = false;

  if (UpdateBookkeeping(new_wake_owner, old_wake_prio, &accum_cpu_mask)) {
    pi_resched = true;
  }

  // If there is no one waiting in the requeue target, then it is not allowed to
  // have an owner.
  if (requeue_target->IsEmpty()) {
    requeue_owner = nullptr;
  }

  if (requeue_target->UpdateBookkeeping(requeue_owner, old_requeue_prio, &accum_cpu_mask)) {
    pi_resched = true;
  }

  UpdateStatsAndSendIPIs(pi_resched, accum_cpu_mask);
  return local_resched || pi_resched;
}
