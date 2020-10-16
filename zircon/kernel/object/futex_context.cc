// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/futex_context.h"

#include <assert.h>
#include <lib/ktrace.h>
#include <trace.h>
#include <zircon/types.h>

#include <fbl/auto_call.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/scheduler.h>
#include <kernel/thread_lock.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

#define LOCAL_TRACE 0

namespace {  // file scope only

// By default, Futex KTracing is disabled as it introduces some overhead in user
// mode operations which might be performance sensitive.  Developers who are
// debugging issues which could involve futex interactions may enable the
// tracing by setting this top level flag to true, provided that their
// investigation can tolerate the overhead.
constexpr bool kEnableFutexKTracing = false;

class KTraceBase {
 public:
  enum class FutexActive { Yes, No };
  enum class RequeueOp { Yes, No };

 protected:
  static constexpr uint32_t kCountSaturate = 0xFE;
  static constexpr uint32_t kUnlimitedCount = 0xFFFFFFFF;
};

template <bool Enabled>
class KTrace;

template <>
class KTrace<false> : public KTraceBase {
 public:
  KTrace() {}
  void FutexWait(uintptr_t futex_id, Thread* new_owner) {}
  void FutexWoke(uintptr_t futex_id, zx_status_t result) {}
  void FutexWake(uintptr_t futex_id, FutexActive active, RequeueOp requeue_op, uint32_t count,
                 Thread* assigned_owner) {}
  void FutexRequeue(uintptr_t futex_id, FutexActive active, uint32_t count,
                    Thread* assigned_owner) {}
};

template <>
class KTrace<true> : public KTraceBase {
 public:
  KTrace() : ts_(ktrace_timestamp()) {}

  void FutexWait(uintptr_t futex_id, Thread* new_owner) {
    ktrace(TAG_FUTEX_WAIT, static_cast<uint32_t>(futex_id), static_cast<uint32_t>(futex_id >> 32),
           static_cast<uint32_t>(new_owner ? new_owner->user_tid() : 0),
           static_cast<uint32_t>(arch_curr_cpu_num() & 0xFF), ts_);
  }

  void FutexWoke(uintptr_t futex_id, zx_status_t result) {
    ktrace(TAG_FUTEX_WOKE, static_cast<uint32_t>(futex_id), static_cast<uint32_t>(futex_id >> 32),
           static_cast<uint32_t>(result), static_cast<uint32_t>(arch_curr_cpu_num() & 0xFF), ts_);
  }

  void FutexWake(uintptr_t futex_id, FutexActive active, RequeueOp requeue_op, uint32_t count,
                 Thread* assigned_owner) {
    if ((count >= kCountSaturate) && (count != kUnlimitedCount)) {
      count = kCountSaturate;
    }

    uint32_t flags = (arch_curr_cpu_num() & KTRACE_FLAGS_FUTEX_CPUID_MASK) |
                     ((count & KTRACE_FLAGS_FUTEX_COUNT_MASK) << KTRACE_FLAGS_FUTEX_COUNT_SHIFT) |
                     ((requeue_op == RequeueOp::Yes) ? KTRACE_FLAGS_FUTEX_WAS_REQUEUE_FLAG : 0) |
                     ((active == FutexActive::Yes) ? KTRACE_FLAGS_FUTEX_WAS_ACTIVE_FLAG : 0);

    ktrace(TAG_FUTEX_WAKE, static_cast<uint32_t>(futex_id), static_cast<uint32_t>(futex_id >> 32),
           static_cast<uint32_t>(assigned_owner ? assigned_owner->user_tid() : 0), flags, ts_);
  }

  void FutexRequeue(uintptr_t futex_id, FutexActive active, uint32_t count,
                    Thread* assigned_owner) {
    if ((count >= kCountSaturate) && (count != kUnlimitedCount)) {
      count = kCountSaturate;
    }

    uint32_t flags = (arch_curr_cpu_num() & KTRACE_FLAGS_FUTEX_CPUID_MASK) |
                     ((count & KTRACE_FLAGS_FUTEX_COUNT_MASK) << KTRACE_FLAGS_FUTEX_COUNT_SHIFT) |
                     KTRACE_FLAGS_FUTEX_WAS_REQUEUE_FLAG |
                     ((active == FutexActive::Yes) ? KTRACE_FLAGS_FUTEX_WAS_ACTIVE_FLAG : 0);

    ktrace(TAG_FUTEX_WAKE, static_cast<uint32_t>(futex_id), static_cast<uint32_t>(futex_id >> 32),
           static_cast<uint32_t>(assigned_owner ? assigned_owner->user_tid() : 0), flags, ts_);
  }

 private:
  const uint64_t ts_;
};

// Gets a reference to the thread that the user is asserting is the new owner of
// the futex.  The thread must belong to the same process as the caller as
// futexes may not be owned by threads from another process.  In addition, the
// new potential owner thread must have been started.  Threads which have not
// started yet may not be the owner of a futex.
//
// Do this before we enter any potentially blocking locks.  Right now, this
// operation can block on BRW locks involved in protecting the global handle
// table, and the penalty for doing so can be severe due to other issues.
// Until these are resolved, we would rather pay the price to do validation
// here instead of while holding the lock.
//
// This said, we cannot bail out with an error just yet.  We need to make it
// into the futex's lock and perform futex state validation first.  See Bug
// #34382 for details.
zx_status_t ValidateFutexOwner(zx_handle_t new_owner_handle,
                               fbl::RefPtr<ThreadDispatcher>* thread_dispatcher) {
  if (new_owner_handle == ZX_HANDLE_INVALID) {
    return ZX_OK;
  }
  auto up = ProcessDispatcher::GetCurrent();
  zx_status_t status = up->handle_table().GetDispatcherWithRightsNoPolicyCheck(
      new_owner_handle, 0, thread_dispatcher, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  // Make sure that the proposed owner of the futex is running in our process,
  // and that it has been started.
  const auto& new_owner = *thread_dispatcher;
  if ((new_owner->process() != up) || !new_owner->HasStarted()) {
    thread_dispatcher->reset();
    return ZX_ERR_INVALID_ARGS;
  }

  // If the thread is already DEAD or DYING, don't bother attempting to assign
  // it as a new owner for the futex.
  if (new_owner->IsDyingOrDead()) {
    thread_dispatcher->reset();
  }
  return ZX_OK;
}

// NullGuard is a stub class that has the same API as lockdep::Guard but does nothing.
class NullGuard {
 public:
  NullGuard() {}
  NullGuard(lockdep::AdoptLockTag, NullGuard&& other) {}
  template <typename... Args>
  void Release(Args&&... args) {}
};

using KTracer = KTrace<kEnableFutexKTracing>;

inline zx_status_t ValidateFutexPointer(user_in_ptr<const zx_futex_t> value_ptr) {
  if (!value_ptr || (reinterpret_cast<uintptr_t>(value_ptr.get()) % sizeof(zx_futex_t))) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

}  // namespace

struct ResetBlockingFutexIdState {
  ResetBlockingFutexIdState() = default;

  // No move, no copy.
  ResetBlockingFutexIdState(const ResetBlockingFutexIdState&) = delete;
  ResetBlockingFutexIdState(ResetBlockingFutexIdState&&) = delete;
  ResetBlockingFutexIdState& operator=(const ResetBlockingFutexIdState&) = delete;
  ResetBlockingFutexIdState& operator=(ResetBlockingFutexIdState&&) = delete;

  uint32_t count = 0;
};

struct SetBlockingFutexIdState {
  explicit SetBlockingFutexIdState(uintptr_t new_id) : id(new_id) {}

  // No move, no copy.
  SetBlockingFutexIdState(const SetBlockingFutexIdState&) = delete;
  SetBlockingFutexIdState(SetBlockingFutexIdState&&) = delete;
  SetBlockingFutexIdState& operator=(const SetBlockingFutexIdState&) = delete;
  SetBlockingFutexIdState& operator=(SetBlockingFutexIdState&&) = delete;

  const uintptr_t id;
  uint32_t count = 0;
};

template <OwnedWaitQueue::Hook::Action action>
OwnedWaitQueue::Hook::Action FutexContext::ResetBlockingFutexId(Thread* thrd, void* ctx) {
  // Any thread involved in one of these operations is
  // currently blocked on a futex's wait queue, and therefor
  // *must* be a user mode thread.
  DEBUG_ASSERT((thrd != nullptr) && (thrd->user_thread() != nullptr));
  DEBUG_ASSERT(ctx != nullptr);
  auto state = reinterpret_cast<ResetBlockingFutexIdState*>(ctx);

  thrd->user_thread()->blocking_futex_id_ = 0;
  ++state->count;

  return action;
}

template <OwnedWaitQueue::Hook::Action action>
OwnedWaitQueue::Hook::Action FutexContext::SetBlockingFutexId(Thread* thrd, void* ctx) {
  // Any thread involved in one of these operations is
  // currently blocked on a futex's wait queue, and therefor
  // *must* be a user mode thread.
  DEBUG_ASSERT((thrd != nullptr) && (thrd->user_thread() != nullptr));
  DEBUG_ASSERT(ctx != nullptr);
  auto state = reinterpret_cast<SetBlockingFutexIdState*>(ctx);

  thrd->user_thread()->blocking_futex_id_ = state->id;
  ++state->count;

  return action;
}

FutexContext::FutexState::~FutexState() {}

FutexContext::FutexContext() { LTRACE_ENTRY; }

FutexContext::~FutexContext() {
  LTRACE_ENTRY;

  // All of the threads should have removed themselves from wait queues and
  // destroyed themselves by the time the process has exited.
  DEBUG_ASSERT(active_futexes_.is_empty());
  DEBUG_ASSERT(free_futexes_.is_empty());
}

zx_status_t FutexContext::GrowFutexStatePool() {
  fbl::AllocChecker ac;
  ktl::unique_ptr<FutexState> new_state1{new (&ac) FutexState};
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  ktl::unique_ptr<FutexState> new_state2{new (&ac) FutexState};
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  Guard<SpinLock, IrqSave> pool_lock_guard{&pool_lock_};
  free_futexes_.push_front(ktl::move(new_state1));
  free_futexes_.push_front(ktl::move(new_state2));
  return ZX_OK;
}

void FutexContext::ShrinkFutexStatePool() {
  ktl::unique_ptr<FutexState> state1, state2;
  {  // Do not let the futex state become released inside of the lock.
    Guard<SpinLock, IrqSave> pool_lock_guard{&pool_lock_};
    DEBUG_ASSERT(free_futexes_.is_empty() == false);
    state1 = free_futexes_.pop_front();
    state2 = free_futexes_.pop_front();
  }
}

// FutexWait verifies that the integer pointed to by |value_ptr| still equals
// |current_value|. If the test fails, FutexWait returns FAILED_PRECONDITION.
// Otherwise it will block the current thread until the |deadline| passes, or
// until the thread is woken by a FutexWake or FutexRequeue operation on the
// same |value_ptr| futex.
zx_status_t FutexContext::FutexWait(user_in_ptr<const zx_futex_t> value_ptr,
                                    zx_futex_t current_value, zx_handle_t new_futex_owner,
                                    const Deadline& deadline) {
  LTRACE_ENTRY;

  // Make sure the futex pointer is following the basic rules.
  zx_status_t result = ValidateFutexPointer(value_ptr);
  if (result != ZX_OK) {
    return result;
  }

  fbl::RefPtr<ThreadDispatcher> futex_owner_thread;
  zx_status_t owner_validator_status = ValidateFutexOwner(new_futex_owner, &futex_owner_thread);
  if (futex_owner_thread) {
    Guard<Mutex> futex_owner_guard{futex_owner_thread->get_lock()};
    return FutexWaitInternal<Guard<Mutex>>(
        value_ptr, current_value, futex_owner_thread.get(), futex_owner_thread->core_thread_,
        futex_owner_guard.take(), owner_validator_status, deadline);
  } else {
    NullGuard null_guard;
    return FutexWaitInternal<NullGuard>(value_ptr, current_value, nullptr, nullptr,
                                        ktl::move(null_guard), owner_validator_status, deadline);
  }
}

template <typename GuardType>
zx_status_t FutexContext::FutexWaitInternal(user_in_ptr<const zx_futex_t> value_ptr,
                                            zx_futex_t current_value,
                                            ThreadDispatcher* futex_owner_thread, Thread* new_owner,
                                            GuardType&& adopt_new_owner_guard,
                                            zx_status_t validator_status,
                                            const Deadline& deadline) {
  GuardType new_owner_guard{AdoptLock, ktl::move(adopt_new_owner_guard)};
  KTracer wait_tracer;
  zx_status_t result;

  Thread* current_core_thread = Thread::Current::Get();
  ThreadDispatcher* current_thread = current_core_thread->user_thread();
  uintptr_t futex_id = reinterpret_cast<uintptr_t>(value_ptr.get());
  {
    // Obtain the FutexState for the ID we are interested in, activating a free
    // futex state in the process if needed.  This operation should never fail
    // (there should always be a FutexState available to us).
    //
    FutexState::PendingOpRef futex_ref = ActivateFutex(futex_id);
    DEBUG_ASSERT(futex_ref != nullptr);

    // Now that we have a hold of the FutexState, enter the futex specific lock
    // and validate the user-mote futex state.
    //
    // FutexWait() checks that the address value_ptr still contains
    // current_value, and if so it sleeps awaiting a FutexWake() on value_ptr.
    // Those two steps must together be atomic with respect to FutexWake().  If
    // a FutexWake() operation could occur between them, a user-land mutex
    // operation built on top of futexes would have a race condition that could
    // miss wakeups.
    //
    // Note that we disable involuntary preemption while we are inside of this
    // lock.  The price of blocking while holding this lock is high, and we
    // should not (in theory) _ever_ be inside of this lock for very long at
    // all.  Were it not for the potential to block while resolving a page fault
    // during validation of the futex state, this would be an IRQ-disable spin
    // lock.  The vast majority of the time, we just need validate the state,
    // then trade this lock for the thread lock, and then block.  Even if we are
    // operating at the very end of our slice, it is best to disable preemption
    // until we manage to join the wait queue, or abort because of state
    // validation issues.
    AutoPreemptDisabler<APDInitialState::PREEMPT_DISABLED> preempt_disabler;
    Guard<Mutex> guard{&futex_ref->lock_};

    // Sanity check, bookkeeping should not indicate that we are blocked on
    // a futex at this point in time.
    DEBUG_ASSERT(current_thread->blocking_futex_id_ == 0);

    int value;
    result = value_ptr.copy_from_user(&value);
    if (result != ZX_OK) {
      return result;
    }
    if (value != current_value) {
      return ZX_ERR_BAD_STATE;
    }

    if (validator_status != ZX_OK) {
      if (validator_status == ZX_ERR_BAD_HANDLE) {
        __UNUSED auto res = ProcessDispatcher::GetCurrent()->EnforceBasicPolicy(ZX_POL_BAD_HANDLE);
      }
      return validator_status;
    }

    if (futex_owner_thread != nullptr) {
      // When attempting to wait, the new owner of the futex (if any) may not be
      // the thread which is attempting to wait.
      if (futex_owner_thread == ThreadDispatcher::GetCurrent()) {
        return ZX_ERR_INVALID_ARGS;
      }

      // If we have a valid new owner, then verify that this thread is not already
      // waiting on the target futex.
      if (futex_owner_thread->blocking_futex_id_ == futex_id) {
        return ZX_ERR_INVALID_ARGS;
      }
    }

    // Record the futex ID of the thread we are about to block on.
    current_thread->blocking_futex_id_ = futex_id;

    // Enter the thread lock (exchanging the futex context lock and the
    // ThreadDispatcher's object lock for the thread spin-lock in the process)
    // and wait on the futex wait queue, assigning ownership properly in the
    // process.
    //
    // We specifically want reschedule=MutexPolicy::NoReschedule here,
    // otherwise the combination of releasing the mutex and enqueuing the
    // current thread would not be atomic, which would mean that we could
    // miss wakeups.
    Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};
    ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::FUTEX);
    guard.Release(MutexPolicy::ThreadLockHeld, MutexPolicy::NoReschedule);
    new_owner_guard.Release(MutexPolicy::ThreadLockHeld, MutexPolicy::NoReschedule);

    wait_tracer.FutexWait(futex_id, new_owner);

    result = futex_ref->waiters_.BlockAndAssignOwner(deadline, new_owner, ResourceOwnership::Normal,
                                                     Interruptible::Yes);

    // Do _not_ allow the PendingOpRef helper to release our pending op
    // reference.  Having just woken up, either the thread which woke us will
    // have released our pending op reference, or we will need to revalidate
    // _which_ futex we were waiting on (because of FutexRequeue) and manage the
    // release of the reference ourselves.
    futex_ref.CancelRef();
  }

  // If we were woken by another thread, then our block result will be ZX_OK.
  // We know that the thread has handled releasing our pending op reference, and
  // has reset our blocking futex ID to zero.  No special action should be
  // needed by us at this point.
  KTracer woke_tracer;
  if (result == ZX_OK) {
    // The FutexWake operation should have already cleared our blocking
    // futex ID.
    DEBUG_ASSERT(current_thread->blocking_futex_id_ == 0);
    woke_tracer.FutexWoke(futex_id, result);
    return ZX_OK;
  }

  // If the result is not ZX_OK, then additional actions may be required by
  // us.  This could be because
  //
  // 1) We hit the deadline (ZX_ERR_TIMED_OUT)
  // 2) We were killed (ZX_ERR_INTERNAL_INTR_KILLED)
  // 3) We were suspended (ZX_ERR_INTERNAL_INTR_RETRY)
  //
  // In any one of these situations, it is possible that we were the last
  // waiter in our FutexState and need to return the FutexState to the free
  // pool as a result.  To complicate things just a bit further, becuse of
  // zx_futex_requeue, the futex that we went to sleep on may not be the futex
  // we just woke up from.  We need to find the futex we were blocked by, and
  // release our pending op reference to it (potentially returning the
  // FutexState to the free pool in the process).
  DEBUG_ASSERT(current_thread->blocking_futex_id_ != 0);
  woke_tracer.FutexWoke(current_thread->blocking_futex_id_, result);

  FutexState::PendingOpRef futex_ref = FindActiveFutex(current_thread->blocking_futex_id_);
  current_thread->blocking_futex_id_ = 0;
  DEBUG_ASSERT(futex_ref != nullptr);

  // Record the fact that we are holding an extra reference.  The first
  // reference was placed on the FutexState at the start of this method as we
  // fetched the FutexState from the pool.  This reference was not removed by a
  // waking thread because we just timed out, or were killed/suspended.
  //
  // The second reference was just added during the FindActiveFutex (above).
  //
  futex_ref.SetExtraRefs(1);

  // Enter the thread lock and deal with ownership of the futex.  It is possible
  // that we were the last thread waiting on the futex, but that the futex's
  // wait queue still has an owner assigned.  If that turns out to be the case
  // once we are inside of the thread-lock, we need to clear the wait queue's
  // owner.
  //
  // Note: We should not need the actual FutexState lock at this point in time.
  // We know that the FutexState cannot disappear out from under us (we are
  // holding two pending operation references), and once we are inside of the
  // thread lock, we no that no new threads can join the wait queue.  If there
  // is a thread racing with us to join the queue, then it will go ahead and
  // explicitly update ownership as it joins the queue once it has made it
  // inside of the thread lock.
  {
    Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};
    if (futex_ref->waiters_.IsEmpty() && futex_ref->waiters_.AssignOwner(nullptr)) {
      Scheduler::Reschedule();
    }
  }

  return result;
}

zx_status_t FutexContext::FutexWake(user_in_ptr<const zx_futex_t> value_ptr, uint32_t wake_count,
                                    OwnerAction owner_action) {
  LTRACE_ENTRY;
  zx_status_t result;
  KTracer tracer;

  // Make sure the futex pointer is following the basic rules.
  result = ValidateFutexPointer(value_ptr);
  if (result != ZX_OK) {
    return result;
  }

  // Try to find an active futex with the specified ID.  If we cannot find one,
  // then we are done.  This wake operation had no threads to wake.
  uintptr_t futex_id = reinterpret_cast<uintptr_t>(value_ptr.get());
  FutexState::PendingOpRef futex_ref = FindActiveFutex(futex_id);
  if (futex_ref == nullptr) {
    tracer.FutexWake(futex_id, KTracer::FutexActive::No, KTracer::RequeueOp::No, wake_count,
                     nullptr);
    return ZX_OK;
  }

  // We found an "active" futex, meaning its pending operation count was
  // non-zero when we went looking for it.  Now enter the FutexState specific
  // lock and see if there are any actual waiters to wake up.
  ResetBlockingFutexIdState wake_op;
  {
    AutoPreemptDisabler<APDInitialState::PREEMPT_DISABLED> preempt_disabler;
    Guard<Mutex> guard{&futex_ref->lock_};

    // Now, enter the thread lock and actually wake up the threads.
    // OwnedWakeQueue will handle the ownership bookkeeping for us.
    {
      using Action = OwnedWaitQueue::Hook::Action;
      Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

      // Attempt to wake |wake_count| threads.  Count the number of thread that
      // we have successfully woken, and assign each of their blocking futex IDs
      // to 0 as we go.  We need an accurate count in order to properly adjust
      // the pending operation ref count on our way out of this function.
      auto hook = (owner_action == OwnerAction::RELEASE)
                      ? ResetBlockingFutexId<Action::SelectAndKeepGoing>
                      : ResetBlockingFutexId<Action::SelectAndAssignOwner>;

      if (futex_ref->waiters_.WakeThreads(wake_count, {hook, &wake_op})) {
        Scheduler::Reschedule();
      }

      // Either our owner action was RELEASE (in which case we should not have
      // any owner), or our action was ASSIGN_WOKEN (in which case we should
      // _only_ have an owner if there are still waiters remaining.
      DEBUG_ASSERT(
          ((owner_action == OwnerAction::RELEASE) && (futex_ref->waiters_.owner() == nullptr)) ||
          ((owner_action == OwnerAction::ASSIGN_WOKEN) &&
           (!futex_ref->waiters_.IsEmpty() || (futex_ref->waiters_.owner() == nullptr))));

      tracer.FutexWake(futex_id, KTracer::FutexActive::Yes, KTracer::RequeueOp::No, wake_op.count,
                       futex_ref->waiters_.owner());
    }
  }

  // Adjust the number of pending operation refs we are about to release.  In
  // addition to the ref we were holding when we started the wake operation, we
  // are also now responsible for the refs which were being held by each of the
  // threads which we have successfully woken.  Those threads are exiting along
  // the FutexWait hot-path, and they have expected us to manage their
  // blocking_futex_id and pending operation references for them.
  futex_ref.SetExtraRefs(wake_op.count);
  return ZX_OK;
}

zx_status_t FutexContext::FutexRequeue(user_in_ptr<const zx_futex_t> wake_ptr, uint32_t wake_count,
                                       int current_value, OwnerAction owner_action,
                                       user_in_ptr<const zx_futex_t> requeue_ptr,
                                       uint32_t requeue_count,
                                       zx_handle_t new_requeue_owner_handle) {
  LTRACE_ENTRY;
  zx_status_t result;

  // Make sure the futex pointers are following the basic rules.
  result = ValidateFutexPointer(wake_ptr);
  if (result != ZX_OK) {
    return result;
  }

  result = ValidateFutexPointer(requeue_ptr);
  if (result != ZX_OK) {
    return result;
  }

  if (wake_ptr.get() == requeue_ptr.get()) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate the proposed new owner outside of any FutexState locks, but take
  // no action just yet.  See the comment in FutexWait for details.
  fbl::RefPtr<ThreadDispatcher> requeue_owner_thread;
  zx_status_t owner_validator_status =
      ValidateFutexOwner(new_requeue_owner_handle, &requeue_owner_thread);

  if (requeue_owner_thread) {
    Guard<Mutex> requeue_owner_guard{requeue_owner_thread->get_lock()};
    return FutexRequeueInternal<Guard<Mutex>>(
        wake_ptr, wake_count, current_value, owner_action, requeue_ptr, requeue_count,
        requeue_owner_thread.get(), requeue_owner_thread->core_thread_, requeue_owner_guard.take(),
        owner_validator_status);
  } else {
    NullGuard null_guard;
    return FutexRequeueInternal<NullGuard>(wake_ptr, wake_count, current_value, owner_action,
                                           requeue_ptr, requeue_count, nullptr, nullptr,
                                           ktl::move(null_guard), owner_validator_status);
  }
}

template <typename GuardType>
zx_status_t FutexContext::FutexRequeueInternal(
    user_in_ptr<const zx_futex_t> wake_ptr, uint32_t wake_count, zx_futex_t current_value,
    OwnerAction owner_action, user_in_ptr<const zx_futex_t> requeue_ptr, uint32_t requeue_count,
    ThreadDispatcher* requeue_owner_thread, Thread* new_requeue_owner,
    GuardType&& adopt_new_owner_guard, zx_status_t validator_status) {
  GuardType new_owner_guard{AdoptLock, ktl::move(adopt_new_owner_guard)};
  zx_status_t result;
  KTracer tracer;

  // Find the FutexState for the wake and requeue futexes.
  uintptr_t wake_id = reinterpret_cast<uintptr_t>(wake_ptr.get());
  uintptr_t requeue_id = reinterpret_cast<uintptr_t>(requeue_ptr.get());
  KTracer::FutexActive requeue_futex_was_active;

  Guard<SpinLock, IrqSave> ref_lookup_guard{&pool_lock_};
  FutexState::PendingOpRef wake_futex_ref = ActivateFutexLocked(wake_id);
  FutexState::PendingOpRef requeue_futex_ref = ActivateFutexLocked(requeue_id);

  DEBUG_ASSERT(wake_futex_ref != nullptr);
  DEBUG_ASSERT(requeue_futex_ref != nullptr);

  // Check to see if the requeue target was active or not when we fetched it by
  // looking at the pending operation ref count.  If it is exactly 1, then we
  // just activated it.  Note that the only reason why we can get away with this
  // is that we are still inside of the pool lock.
  requeue_futex_was_active = (requeue_futex_ref->pending_operation_count() == 1)
                                 ? KTracer::FutexActive::No
                                 : KTracer::FutexActive::Yes;

  // Manually release the ref lookup guard.  While we would typically do this
  // using scope, the PendingOpRefs need to live outside of just the locking
  // scope.  We cannot declare the PendingOpRefs outside of the scope because we
  // do not allow default construction of PendingOpRefs, nor do we allow move
  // assignment.  This is done on purpose; pending op refs should only ever be
  // constructed during lookup operations, and they really should not be moved
  // around.  We need to have a move constructor, but there is no reason for a
  // move assignment.
  ref_lookup_guard.Release();

  ResetBlockingFutexIdState wake_op;
  SetBlockingFutexIdState requeue_op(requeue_id);
  {
    AutoPreemptDisabler<APDInitialState::PREEMPT_DISABLED> preempt_disabler;
    GuardMultiple<2, Mutex> futex_guards{&wake_futex_ref->lock_, &requeue_futex_ref->lock_};

    // Validate the futex storage state.
    int value;
    result = wake_ptr.copy_from_user(&value);
    if (result != ZX_OK) {
      return result;
    }

    if (value != current_value) {
      return ZX_ERR_BAD_STATE;
    }

    // If owner validation failed earlier, then bail out now (after we have passed the state check).
    if (validator_status != ZX_OK) {
      if (validator_status == ZX_ERR_BAD_HANDLE) {
        __UNUSED auto res = ProcessDispatcher::GetCurrent()->EnforceBasicPolicy(ZX_POL_BAD_HANDLE);
      }
      return validator_status;
    }

    // Verify that the thread we are attempting to make the requeue target's
    // owner (if any) is not waiting on either the wake futex or the requeue
    // futex.
    if (requeue_owner_thread && ((requeue_owner_thread->blocking_futex_id_ == wake_id) ||
                                 (requeue_owner_thread->blocking_futex_id_ == requeue_id))) {
      return ZX_ERR_INVALID_ARGS;
    }

    // Now that all of our sanity checks are complete, it is time to do the
    // actual manipulation of the various wait queues.
    {
      DEBUG_ASSERT(wake_futex_ref != nullptr);
      // Exchange ThreadDispatcher's object lock for the global ThreadLock.
      Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};
      new_owner_guard.Release(MutexPolicy::ThreadLockHeld, MutexPolicy::NoReschedule);
      bool do_resched;

      using Action = OwnedWaitQueue::Hook::Action;
      auto wake_hook = (owner_action == OwnerAction::RELEASE)
                           ? ResetBlockingFutexId<Action::SelectAndKeepGoing>
                           : ResetBlockingFutexId<Action::SelectAndAssignOwner>;
      auto requeue_hook = SetBlockingFutexId<Action::SelectAndKeepGoing>;

      if (requeue_count) {
        DEBUG_ASSERT(requeue_futex_ref != nullptr);
        do_resched = wake_futex_ref->waiters_.WakeAndRequeue(
            wake_count, &(requeue_futex_ref->waiters_), requeue_count, new_requeue_owner,
            {wake_hook, &wake_op}, {requeue_hook, &requeue_op});
      } else {
        do_resched = wake_futex_ref->waiters_.WakeThreads(wake_count, {wake_hook, &wake_op});

        // We made no attempt to requeue anyone, but we still need to update
        // ownership.  If it has waiters currently, make sure that we clear out
        // any owner, no matter what the user requested.  Futexes without
        // waiters are not permitted to have owners.
        if (requeue_futex_ref->waiters_.IsEmpty()) {
          new_requeue_owner = nullptr;
        }

        if (requeue_futex_ref->waiters_.AssignOwner(new_requeue_owner)) {
          do_resched = true;
        }
      }

      // If we requeued any threads, we need to transfer their pending operation
      // counts from the FutexState that they went to sleep on, over to the
      // FutexState they are being requeued to.
      //
      // Sadly, this needs to be done from within the context of the thread
      // lock.  Failure to do this means that it would be possible for us to
      // requeue a thread from futex A over to futex B, then have that thread
      // time out from the futex before we have move the pending operation
      // references from A to B.  If the thread manages wake up and attempts to
      // drop its pending operation count on futex B before we have transferred
      // the count, it would result in a bookkeeping error.
      requeue_futex_ref.TakeRefs(&wake_futex_ref, requeue_op.count);

      tracer.FutexWake(wake_id, KTracer::FutexActive::Yes, KTracer::RequeueOp::Yes, wake_op.count,
                       wake_futex_ref->waiters_.owner());
      tracer.FutexRequeue(requeue_id, requeue_futex_was_active, requeue_op.count,
                          new_requeue_owner);

      if (do_resched) {
        Scheduler::Reschedule();
      }
    }
  }

  // Now, if we successfully woke any threads from the wake_futex, then we need
  // to adjust the number of references we are holding by that number of
  // threads.  They are on the hot-path out of FutexWake, and we are responsible
  // for their pending op refs.
  wake_futex_ref.SetExtraRefs(wake_op.count);

  // Now just return.  The futex states will return to the pool as needed.
  return ZX_OK;
}

// Get the KOID of the current owner of the specified futex, if any, or ZX_KOID_INVALID if there
// is no known owner.
zx_status_t FutexContext::FutexGetOwner(user_in_ptr<const zx_futex_t> value_ptr,
                                        user_out_ptr<zx_koid_t> koid_out) {
  zx_status_t result;

  // Make sure the futex pointer is following the basic rules.
  result = ValidateFutexPointer(value_ptr);
  if (result != ZX_OK) {
    return result;
  }

  // Attempt to find the futex.  If it is not in the active set, then there is no owner.
  zx_koid_t koid = ZX_KOID_INVALID;
  uintptr_t futex_id = reinterpret_cast<uintptr_t>(value_ptr.get());
  FutexState::PendingOpRef futex_ref = FindActiveFutex(futex_id);

  // We found a FutexState in the active set.  It may have an owner, but we need
  // to enter the thread lock in order to check.
  if (futex_ref != nullptr) {
    {  // explicit lock scope
      Guard<SpinLock, IrqSave> thread_lock_guard{ThreadLock::Get()};

      if (const Thread* owner = futex_ref->waiters_.owner(); owner != nullptr) {
        // Any thread which owns a FutexState's wait queue *must* be a
        // user mode thread.
        DEBUG_ASSERT(owner->user_thread() != nullptr);
        koid = owner->user_thread()->get_koid();
      }
    }
  }

  return koid_out.copy_to_user(koid);
}
