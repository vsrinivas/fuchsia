// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/futex_context.h>

#include <assert.h>
#include <kernel/sched.h>
#include <kernel/thread_lock.h>
#include <lib/ktrace.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <trace.h>
#include <zircon/types.h>

#define LOCAL_TRACE 0

namespace { // file scope only

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
    void FutexWait(uintptr_t futex_id, thread_t* new_owner) {}
    void FutexWoke(uintptr_t futex_id, zx_status_t result) {}
    void FutexWake(uintptr_t futex_id,
                   FutexActive active,
                   RequeueOp requeue_op,
                   uint32_t count,
                   thread_t* assigned_owner) {}
    void FutexRequeue(uintptr_t futex_id,
                      FutexActive active,
                      uint32_t count,
                      thread_t* assigned_owner) {}
};

template <>
class KTrace<true> : public KTraceBase {
public:
    KTrace() : ts_(ktrace_timestamp()) {}

    void FutexWait(uintptr_t futex_id, thread_t* new_owner) {
        ktrace(TAG_FUTEX_WAIT,
                static_cast<uint32_t>(futex_id),
                static_cast<uint32_t>(futex_id >> 32),
                static_cast<uint32_t>(new_owner ? new_owner->user_tid : 0),
                static_cast<uint32_t>(arch_curr_cpu_num() & 0xFF),
                ts_);
    }

    void FutexWoke(uintptr_t futex_id, zx_status_t result) {
        ktrace(TAG_FUTEX_WOKE,
                static_cast<uint32_t>(futex_id),
                static_cast<uint32_t>(futex_id >> 32),
                static_cast<uint32_t>(result),
                static_cast<uint32_t>(arch_curr_cpu_num() & 0xFF),
                ts_);
    }

    void FutexWake(uintptr_t futex_id,
                   FutexActive active,
                   RequeueOp requeue_op,
                   uint32_t count,
                   thread_t* assigned_owner) {
        if ((count >= kCountSaturate) && (count != kUnlimitedCount)) {
            count = kCountSaturate;
        }

        uint32_t flags =
            (arch_curr_cpu_num() & KTRACE_FLAGS_FUTEX_CPUID_MASK) |
            ((count & KTRACE_FLAGS_FUTEX_COUNT_MASK) << KTRACE_FLAGS_FUTEX_COUNT_SHIFT) |
            ((requeue_op == RequeueOp::Yes) ?  KTRACE_FLAGS_FUTEX_WAS_REQUEUE_FLAG : 0) |
            ((active == FutexActive::Yes) ?  KTRACE_FLAGS_FUTEX_WAS_ACTIVE_FLAG : 0);

        ktrace(TAG_FUTEX_WAKE,
                static_cast<uint32_t>(futex_id),
                static_cast<uint32_t>(futex_id >> 32),
                static_cast<uint32_t>(assigned_owner ? assigned_owner->user_tid : 0),
                flags, ts_);
    }

    void FutexRequeue(uintptr_t futex_id,
                      FutexActive active,
                      uint32_t count,
                      thread_t* assigned_owner) {
        if ((count >= kCountSaturate) && (count != kUnlimitedCount)) {
            count = kCountSaturate;
        }

        uint32_t flags =
            (arch_curr_cpu_num() & KTRACE_FLAGS_FUTEX_CPUID_MASK) |
            ((count & KTRACE_FLAGS_FUTEX_COUNT_MASK) << KTRACE_FLAGS_FUTEX_COUNT_SHIFT) |
            KTRACE_FLAGS_FUTEX_WAS_REQUEUE_FLAG |
            ((active == FutexActive::Yes) ?  KTRACE_FLAGS_FUTEX_WAS_ACTIVE_FLAG : 0);

        ktrace(TAG_FUTEX_WAKE,
                static_cast<uint32_t>(futex_id),
                static_cast<uint32_t>(futex_id >> 32),
                static_cast<uint32_t>(assigned_owner ? assigned_owner->user_tid : 0),
                flags, ts_);
    }

private:
    const uint64_t ts_;
};

using KTracer = KTrace<kEnableFutexKTracing>;

inline zx_status_t ValidateFutexPointer(user_in_ptr<const zx_futex_t> value_ptr) {
    if (!value_ptr || (reinterpret_cast<uintptr_t>(value_ptr.get()) % sizeof(zx_futex_t))) {
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
}

inline zx_status_t ValidateNewFutexOwner(zx_handle_t new_owner_handle,
                                         fbl::RefPtr<ThreadDispatcher>* new_owner_thread_out) {
    DEBUG_ASSERT(new_owner_thread_out != nullptr);
    DEBUG_ASSERT(*new_owner_thread_out == nullptr);

    if (new_owner_handle == ZX_HANDLE_INVALID) {
        return ZX_OK;
    }

    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t status = up->GetDispatcherWithRights(new_owner_handle, 0, new_owner_thread_out);
    if (status != ZX_OK) {
        return status;
    }

    // The thread has to be a member of the calling process.  Futexes may not be
    // owned by threads from another process.
    if ((*new_owner_thread_out)->process() != up) {
        return ZX_ERR_INVALID_ARGS;
    }

    return ZX_OK;
}
}  // anon namespace

template <OwnedWaitQueue::Hook::Action action>
OwnedWaitQueue::Hook::Action FutexContext::SetBlockingFutexId(thread_t* thrd, void* ctx) {
    // Any thread involved in one of these operations is
    // currently blocked on a futex's wait queue, and therefor
    // *must* be a user mode thread.
    DEBUG_ASSERT((thrd != nullptr) && (thrd->user_thread != nullptr));
    thrd->user_thread->blocking_futex_id_ = reinterpret_cast<uintptr_t>(ctx);
    return action;
}

FutexContext::FutexState::~FutexState() { }

FutexContext::FutexContext() {
    LTRACE_ENTRY;
}

FutexContext::~FutexContext() {
    LTRACE_ENTRY;

    // All of the threads should have removed themselves from wait queues and
    // destroyed themselves by the time the process has exited.
    DEBUG_ASSERT(futex_table_.is_empty());
    DEBUG_ASSERT(free_futexes_.is_empty());
}

zx_status_t FutexContext::GrowFutexStatePool() {
    fbl::AllocChecker ac;
    ktl::unique_ptr<FutexState> new_state{ new (&ac) FutexState() };

    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    Guard<fbl::Mutex> guard{&lock_};
    free_futexes_.push_front(ktl::move(new_state));
    return ZX_OK;
}

void FutexContext::ShrinkFutexStatePool() {
    ktl::unique_ptr<FutexState> state;
    { // Do not let the futex state become released inside of the lock.
        Guard<fbl::Mutex> guard{&lock_};
        DEBUG_ASSERT(free_futexes_.is_empty() == false);
        state = free_futexes_.pop_front();
    }
}

// FutexWait verifies that the integer pointed to by |value_ptr| still equals
// |current_value|. If the test fails, FutexWait returns FAILED_PRECONDITION.
// Otherwise it will block the current thread until the |deadline| passes, or
// until the thread is woken by a FutexWake or FutexRequeue operation on the
// same |value_ptr| futex.
zx_status_t FutexContext::FutexWait(user_in_ptr<const zx_futex_t> value_ptr,
                                    zx_futex_t current_value,
                                    zx_handle_t new_futex_owner,
                                    const Deadline& deadline) {
    LTRACE_ENTRY;
    zx_status_t result;

    // Make sure the futex pointer is following the basic rules.
    result = ValidateFutexPointer(value_ptr);
    if (result != ZX_OK) {
        return result;
    }

    // Fetch a reference to the thread that the user is asserting is the new
    // futex owner, if any.
    fbl::RefPtr<ThreadDispatcher> futex_owner_thread;
    result = ValidateNewFutexOwner(new_futex_owner, &futex_owner_thread);
    if (result != ZX_OK) {
        return result;
    }

    // When attempting to wait, the new owner of the futex (if any) may not be
    // the thread which is attempting to wait.
    if (futex_owner_thread.get() == ThreadDispatcher::GetCurrent()) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto current_thread = ThreadDispatcher::GetCurrent();
    uintptr_t futex_id = reinterpret_cast<uintptr_t>(value_ptr.get());
    {
        // FutexWait() checks that the address value_ptr still contains
        // current_value, and if so it sleeps awaiting a FutexWake() on value_ptr.
        // Those two steps must together be atomic with respect to FutexWake().
        // If a FutexWake() operation could occur between them, a userland mutex
        // operation built on top of futexes would have a race condition that
        // could miss wakeups.
        Guard<fbl::Mutex> guard{&lock_};

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

        // Find the FutexState for this futex.  If there is no FutexState
        // already, then there are no current waiters.  Grab a free futex futex
        // struct from the pool, and add it to the hash table instead.
        //
        // Either way, make sure that we hold a reference to the FutexState that
        // we end up with.  We will want to keep it alive in order to optimize
        // the case where we are removed from the wait queue for a reason other
        // then being explicitly woken.  If we fail to do this, it is possible
        // for us to time out on the futex, then have someone else return the
        // futex to the free pool, and finally have the futex removed from the
        // free pool and destroyed by an exiting thread.
        FutexState* futex = ObtainActiveFutex(futex_id);
        if (futex == nullptr) {
            futex = ActivateFromPool(futex_id);
        } else {
            // If there was already a FutexState (implying that there are
            // currently waiters, and perhaps an owner) verify that the thread
            // we are attempting to make the new futex owner (if any) is not
            // already waiting on the target futex.
            if (futex_owner_thread) {
                if (futex_owner_thread->blocking_futex_id_ == futex_id) {
                    return ZX_ERR_INVALID_ARGS;
                }
            }
        }

        // Record the futex ID of the thread we are about to block on.
        current_thread->blocking_futex_id_ = futex_id;

        // Enter the thread lock (exchanging the futex context lock for the
        // thread spin-lock in the process) and wait on the futex wait queue,
        // assigning ownership properly in the process.
        //
        // We specifically want reschedule=MutexPolicy::NoReschedule here,
        // otherwise the combination of releasing the mutex and enqueuing the
        // current thread would not be atomic, which would mean that we could
        // miss wakeups.
        Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
        ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::FUTEX);
        guard.Release(MutexPolicy::ThreadLockHeld, MutexPolicy::NoReschedule);

        thread_t* new_owner = futex_owner_thread ? &futex_owner_thread->thread_ : nullptr;

        KTracer tracer;
        tracer.FutexWait(futex_id, new_owner);

        current_thread->thread_.interruptable = true;
        result = futex->waiters_.BlockAndAssignOwner(deadline,
                                                     new_owner,
                                                     ResourceOwnership::Normal);
        current_thread->thread_.interruptable = false;
    }

    // If we were woken by another thread, then our block result will be ZX_OK.
    // We know that the thread who woke us up will have returned the FutexState
    // to the free list if needed.  No special action should be needed by us at
    // this point.
    //
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
    // we just woke up from.  We need to re-enter the context's futex lock and
    // revalidate the state of the world.
    KTracer tracer;
    if (result == ZX_OK) {
        // The FutexWake operation should have already cleared our blocking
        // futex ID.
        DEBUG_ASSERT(current_thread->blocking_futex_id_ == 0);
        tracer.FutexWoke(futex_id, result);
        return ZX_OK;
    }

    {
        Guard<fbl::Mutex> guard{&lock_};

        DEBUG_ASSERT(current_thread->blocking_futex_id_ != 0);
        FutexState* futex = ObtainActiveFutex(current_thread->blocking_futex_id_);
        tracer.FutexWoke(current_thread->blocking_futex_id_, result);
        current_thread->blocking_futex_id_ = 0;

        // Important Note:
        //
        // It is possible for this thread to have exited via an error path
        // (timeout, killed, whatever), but for our futex context to have
        // already been returned to the free pool.  It is not possible, however,
        // for the blocking_futex_id to ever be 0 at this point.  The sequence
        // which would produce something like this is as follows.
        //
        // 1) Threads A is blocked in a Futex X's wait queue.
        // 2) Thread A times out, and under the protection of the ThreadLock is
        //    removed from the wait queue by the kernel.  The wait queue now has
        //    no waiters, but futex X's FutexState has not been returned to the
        //    pool yet.
        // 3) Before thread A makes it to the guard at the top of this block,
        //    Thread B comes along and attempts to wake at least one thread from
        //    futex X.
        // 4) Thread B is inside of the processes futex context lock when it
        //    does this, it notices that futex X's wait queue is now empty, so it
        //    returns the queue to the free pool.
        // 5) Finally, thread A makes it into the futex context lock and
        //    discovers that it had been waiting on futex X, but futex X is not in
        //    the set of active futexes.
        //
        // There are many other variations on this sequence, this just happens
        // to be the simplest one that I can think of.  Other threads can be
        // involved, futex X could have been retired, then reactivated any
        // number of times, and so on.
        //
        // The important things to realize here are...
        // 1) An truly active futex *must* have at least one waiter.
        // 2) Because of timeouts, it is possible for a futex to be in the
        //    active set, with no waiters.
        // 3) If #2 is true, then there must be at least one thread which has
        //    been released from the wait queue and it traveling along the error
        //    path.
        // 4) One of these threads will make it to here, enter the lock, and
        //    attempt to retire the FutexState to the inactive pool if it is
        //    still in the active set.
        // 5) It does not matter *who* does this, as long as someone does this
        //    job.  It can be the waking thread, or one of the timed out
        //    threads.  As long as everyone makes an attempt while inside of the
        //    lock, things should be OK and no FutexStates should be leaked.
        if (futex != nullptr) {
            // Looks like the futex is still in the active set.  Enter the
            // thread_lock and check to see if the OwnedWaitQueue member of this
            // FutexState is now empty.  If so, then we need to release the wait
            // queue owner, update any related PI pressure, and return the futex
            // state to the available pool.
            bool is_empty = false;
            {
                Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
                DEBUG_ASSERT(current_thread->thread_.blocking_wait_queue == nullptr);
                is_empty = futex->waiters_.IsEmpty();
                if (is_empty) {
                    if (futex->waiters_.AssignOwner(nullptr)) {
                        sched_reschedule();
                    }
                }
            }

            if (is_empty) {
                ReturnToPool(futex);
            }
        }
    }

    return result;
}

zx_status_t FutexContext::FutexWake(user_in_ptr<const zx_futex_t> value_ptr,
                                    uint32_t wake_count,
                                    OwnerAction owner_action) {
    LTRACE_ENTRY;
    zx_status_t result;
    KTracer tracer;

    // Make sure the futex pointer is following the basic rules.
    result = ValidateFutexPointer(value_ptr);
    if (result != ZX_OK) {
        return result;
    }

    uintptr_t futex_id = reinterpret_cast<uintptr_t>(value_ptr.get());
    AutoReschedDisable resched_disable; // Must come before the Guard.
    {   // explicit lock scope for clarity.
        Guard<fbl::Mutex> guard{&lock_};

        // If the futex key is not in our hash table, then there is no one to
        // wake, we are finished.
        FutexState* futex = ObtainActiveFutex(futex_id);
        if (futex == nullptr) {
            tracer.FutexWake(futex_id, KTracer::FutexActive::No, KTracer::RequeueOp::No,
                             wake_count, nullptr);
            return ZX_OK;
        }

        // Now, enter the thread lock and actually wake up the threads.
        // OwnedWakeQueue will handle the ownership bookkeeping for us.
        bool futex_emptied;
        {
            resched_disable.Disable();
            Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};

            using Action = OwnedWaitQueue::Hook::Action;
            auto hook = (owner_action == OwnerAction::RELEASE)
                      ? SetBlockingFutexId<Action::SelectAndKeepGoing>
                      : SetBlockingFutexId<Action::SelectAndAssignOwner>;

            if (futex->waiters_.WakeThreads(wake_count, { hook, nullptr })) {
                sched_reschedule();
            }

            futex_emptied = futex->waiters_.IsEmpty();
            tracer.FutexWake(futex_id, KTracer::FutexActive::Yes, KTracer::RequeueOp::No,
                             wake_count, futex->waiters_.owner());
        }

        // Now that we are outside of the thread lock, if there are no longer
        // any waiters for this futex, return the state to the pool.
        if (futex_emptied) {
            ReturnToPool(futex);
        }
    }

    return ZX_OK;
}

zx_status_t FutexContext::FutexRequeue(user_in_ptr<const zx_futex_t> wake_ptr,
                                       uint32_t wake_count,
                                       int current_value,
                                       OwnerAction owner_action,
                                       user_in_ptr<const zx_futex_t> requeue_ptr,
                                       uint32_t requeue_count,
                                       zx_handle_t new_requeue_owner_handle) {
    LTRACE_ENTRY;
    zx_status_t result;
    KTracer tracer;

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

    // Fetch a reference to the thread that the user is asserting is the new
    // requeue futex owner, if any.
    fbl::RefPtr<ThreadDispatcher> requeue_owner_thread;
    result = ValidateNewFutexOwner(new_requeue_owner_handle, &requeue_owner_thread);
    if (result != ZX_OK) {
        return result;
    }

    AutoReschedDisable resched_disable; // Must come before the Guard.
    Guard<fbl::Mutex> guard{&lock_};

    int value;
    result = wake_ptr.copy_from_user(&value);
    if (result != ZX_OK) return result;
    if (value != current_value) return ZX_ERR_BAD_STATE;

    // Find the FutexState for the wake and requeue futexes.
    uintptr_t wake_id = reinterpret_cast<uintptr_t>(wake_ptr.get());
    uintptr_t requeue_id = reinterpret_cast<uintptr_t>(requeue_ptr.get());
    FutexState* wake_futex = ObtainActiveFutex(wake_id);
    FutexState* requeue_futex = ObtainActiveFutex(requeue_id);

    // Verify that the thread we are attempting to make the requeue target's
    // owner (if any) is not waiting on either the wake futex or the requeue
    // futex.
    if (requeue_owner_thread && ((requeue_owner_thread->blocking_futex_id_ == wake_id) ||
                                 (requeue_owner_thread->blocking_futex_id_ == requeue_id))) {
        return ZX_ERR_INVALID_ARGS;
    }

    thread_t* new_requeue_owner = requeue_owner_thread
                                ? &(requeue_owner_thread->thread_)
                                : nullptr;
    KTracer::FutexActive requeue_futex_was_active = (requeue_futex == nullptr)
                                                  ? KTracer::FutexActive::No
                                                  : KTracer::FutexActive::Yes;

    // If we have no waiters for the wake futex, then we are more or less
    // finished.  Just be sure to re-assign the futex owner for the requeue
    // futex if needed.
    if (wake_futex == nullptr) {
        tracer.FutexWake(wake_id, KTracer::FutexActive::No, KTracer::RequeueOp::Yes,
                         wake_count, nullptr);
        tracer.FutexRequeue(requeue_id, requeue_futex_was_active,
                            requeue_count, new_requeue_owner);

        if (requeue_futex != nullptr) {
            Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};

            thread_t* new_owner = (requeue_owner_thread != nullptr)
                                ?  &requeue_owner_thread->thread_
                                : nullptr;

            if (requeue_futex->waiters_.AssignOwner(new_owner)) {
                sched_reschedule();
            }
        }
        return ZX_OK;
    }

    // If we plan to make an attempt to requeue _any_ threads, make sure that we
    // have a requeue target ready.
    if (requeue_count && (requeue_futex == nullptr)) {
        requeue_futex = ActivateFromPool(requeue_id);
    }

    // Now that all of our sanity checks are complete, it is time to do the
    // actual manipulation of the various wait queues.  Start by disabling
    // rescheduling and entering the thread lock.
    resched_disable.Disable();
    bool wake_futex_emptied;
    bool requeue_futex_emptied;
    {
        DEBUG_ASSERT(wake_futex != nullptr);
        Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
        bool do_resched;

        using Action = OwnedWaitQueue::Hook::Action;
        auto wake_hook = (owner_action == OwnerAction::RELEASE)
                       ? SetBlockingFutexId<Action::SelectAndKeepGoing>
                       : SetBlockingFutexId<Action::SelectAndAssignOwner>;
        auto requeue_hook = SetBlockingFutexId<Action::SelectAndKeepGoing>;

        if (requeue_count) {
            DEBUG_ASSERT(requeue_futex != nullptr);
            do_resched = wake_futex->waiters_.WakeAndRequeue(
                    wake_count,
                    &(requeue_futex->waiters_),
                    requeue_count,
                    new_requeue_owner,
                    { wake_hook, nullptr },
                    { requeue_hook, reinterpret_cast<void*>(requeue_id) });
        } else {
            do_resched = wake_futex->waiters_.WakeThreads(wake_count, { wake_hook, nullptr });
        }

        tracer.FutexWake(wake_id, KTracer::FutexActive::Yes, KTracer::RequeueOp::Yes,
                         wake_count, wake_futex->waiters_.owner());
        tracer.FutexRequeue(requeue_id, requeue_futex_was_active,
                            requeue_count, new_requeue_owner);

        wake_futex_emptied = wake_futex->waiters_.IsEmpty();
        requeue_futex_emptied = (requeue_futex != nullptr) && requeue_futex->waiters_.IsEmpty();

        if (do_resched) {
            sched_reschedule();
        }
    }

    // Make sure we have retuned any now-empty futex states to the pool before
    // requesting a reschedule (if needed).
    if (wake_futex_emptied) {
        ReturnToPool(wake_futex);
    }

    if (requeue_futex_emptied) {
        ReturnToPool(requeue_futex);
    }

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

    zx_koid_t koid = ZX_KOID_INVALID;
    uintptr_t futex_id = reinterpret_cast<uintptr_t>(value_ptr.get());
    {
        Guard<fbl::Mutex> guard{&lock_};
        FutexState* futex = ObtainActiveFutex(futex_id);
        if (futex != nullptr) {
            Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};

            if (const thread_t* owner = futex->waiters_.owner(); owner != nullptr) {
                // Any thread which owns a FutexState's wait queue *must* be a
                // user mode thread.
                DEBUG_ASSERT(owner->user_thread != nullptr);
                koid = owner->user_thread->get_koid();
            }
        }
    }

    return koid_out.copy_to_user(koid);
}
