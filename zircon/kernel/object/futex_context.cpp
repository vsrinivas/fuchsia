// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/futex_context.h>

#include <assert.h>
#include <kernel/sched.h>
#include <kernel/thread_lock.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <trace.h>
#include <zircon/types.h>

#define LOCAL_TRACE 0

namespace {     // file scope only
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

// FutexWait first verifies that the integer pointed to by |value_ptr|
// still equals |current_value|. If the test fails, FutexWait returns FAILED_PRECONDITION.
// Otherwise it will block the current thread until the |deadline| passes,
// or until the thread is woken by a FutexWake or FutexRequeue operation
// on the same |value_ptr| futex.
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

    uintptr_t futex_id = reinterpret_cast<uintptr_t>(value_ptr.get());
    {
        // FutexWait() checks that the address value_ptr still contains
        // current_value, and if so it sleeps awaiting a FutexWake() on value_ptr.
        // Those two steps must together be atomic with respect to FutexWake().
        // If a FutexWake() operation could occur between them, a userland mutex
        // operation built on top of futexes would have a race condition that
        // could miss wakeups.
        Guard<fbl::Mutex> guard{&lock_};

        int value;
        result = value_ptr.copy_from_user(&value);
        if (result != ZX_OK) {
            return result;
        }
        if (value != current_value) {
            return ZX_ERR_BAD_STATE;
        }

        // Verify that the thread we are attempting to make the requeue target's
        // owner (if any) is not already waiting on the target futex.
        //
        // !! NOTE !!
        // This check *must* be done inside of the futex contex lock.  Right now,
        // there is not a great way to enforce this using clang's static thread
        // analysis.
        if ((futex_owner_thread != nullptr) &&
            (futex_owner_thread->blocking_futex_id() == futex_id)) {
            return ZX_ERR_INVALID_ARGS;
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
        }

        // The futex owner is now the owner specified by the caller (if any)
        futex_owner_thread.swap(futex->owner_);

        // TODO(johngro): If we had an old owner, we may need to re-evaluate its
        // effective priority.  Figure out the proper place to do this.

        // Release our reference to our previous owner.  Note: in a perfect
        // world, we would not be doing this from within the futex lock, but in
        // theory, at worst this should just be a call to free under the hood.
        futex_owner_thread.reset();

        // Finally, block on our futex; exchanging the futex context lock for
        // the thread spin-lock in the process.
        Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
        ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::FUTEX);

        // We specifically want reschedule=MutexPolicy::NoReschedule here, otherwise
        // the combination of releasing the mutex and enqueuing the current thread
        // would not be atomic, which would mean that we could miss wakeups.
        guard.Release(MutexPolicy::ThreadLockHeld, MutexPolicy::NoReschedule);

        thread_t* current_thread = get_current_thread();
        current_thread->user_thread->blocking_futex_id() = futex_id;
        current_thread->interruptable = true;
        result = futex->waiters_.Block(deadline);
        current_thread->interruptable = false;
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
    if (result == ZX_OK) {
        DEBUG_ASSERT(ThreadDispatcher::GetCurrent()->blocking_futex_id() == 0);
        return ZX_OK;
    }

    fbl::RefPtr<ThreadDispatcher> previous_owner;
    {
        Guard<fbl::Mutex> guard{&lock_};

        ThreadDispatcher* cur_thread = ThreadDispatcher::GetCurrent();
        DEBUG_ASSERT(cur_thread->blocking_futex_id() != 0);
        FutexState* futex = ObtainActiveFutex(cur_thread->blocking_futex_id());
        cur_thread->blocking_futex_id() = 0;

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
            bool was_empty;
            {
                Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
                was_empty = futex->waiters_.IsEmpty();
            }

            if (was_empty) {
                previous_owner = ReturnToPool(futex);
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

    // Make sure the futex pointer is following the basic rules.
    result = ValidateFutexPointer(value_ptr);
    if (result != ZX_OK) {
        return result;
    }

    uintptr_t futex_id = reinterpret_cast<uintptr_t>(value_ptr.get());
    fbl::RefPtr<ThreadDispatcher> emptied_owner;
    fbl::RefPtr<ThreadDispatcher> previous_owner;
    AutoReschedDisable resched_disable; // Must come before the Guard.
    {   // explicit lock scope for clarity.
        Guard<fbl::Mutex> guard{&lock_};

        // If the futex key is not in our hash table, then there is no one to
        // wake, we are finished.
        FutexState* futex = ObtainActiveFutex(futex_id);
        if (futex == nullptr) {
            return ZX_OK;
        }

        // Move any previous owner out of the scope of the lock.  No matter
        // what, when we are finished, the owner of the futex is going to be
        // either no one, or the thread that we are about to wake.
        previous_owner.swap(futex->owner_);

        // Now, enter the thread lock, and while we still have waiters, wake up
        // the number of threads we were asked to wake.
        bool futex_emptied;
        {
            resched_disable.Disable();
            Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
            bool do_resched = WakeThreads(futex, wake_count, owner_action);

            // If there are no longer any waiters for the futex, remove its
            // FutexState from the hash table once we leave the thread lock.
            futex_emptied = futex->waiters_.IsEmpty();

            // TODO(johngro): re-evaluate the effective priority of
            // |previous_owner| here.  Set do_resched if it has changed.

            if (do_resched) {
                sched_reschedule();
            }
        }

        if (futex_emptied) {
            emptied_owner = ReturnToPool(futex);
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
                                       zx_handle_t new_requeue_owner) {
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

    // Fetch a reference to the thread that the user is asserting is the new
    // requeue futex owner, if any.
    fbl::RefPtr<ThreadDispatcher> requeue_owner_thread;
    result = ValidateNewFutexOwner(new_requeue_owner, &requeue_owner_thread);
    if (result != ZX_OK) {
        return result;
    }

    fbl::RefPtr<ThreadDispatcher> previous_wake_owner;
    fbl::RefPtr<ThreadDispatcher> emptied_wake_owner;
    AutoReschedDisable resched_disable; // Must come before the Guard.
    Guard<fbl::Mutex> guard{&lock_};

    int value;
    result = wake_ptr.copy_from_user(&value);
    if (result != ZX_OK) return result;
    if (value != current_value) return ZX_ERR_BAD_STATE;

    // Verify that the thread we are attempting to make the requeue target's
    // owner (if any) is not waiting on either the wake futex or the requeue
    // futex.
    //
    // !! NOTE !!
    // This check *must* be done inside of the futex contex lock.  Right now,
    // there is not a great way to enforce this using clang's static thread
    // analysis.
    uintptr_t wake_id = reinterpret_cast<uintptr_t>(wake_ptr.get());
    uintptr_t requeue_id = reinterpret_cast<uintptr_t>(requeue_ptr.get());
    if ((requeue_owner_thread != nullptr) &&
        ((requeue_owner_thread->blocking_futex_id() == wake_id) ||
         (requeue_owner_thread->blocking_futex_id() == requeue_id))) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Find the FutexState for the wake and requeue futexes.
    FutexState* wake_futex = ObtainActiveFutex(wake_id);
    FutexState* requeue_futex = ObtainActiveFutex(requeue_id);

    // If we have no waiters for the wake futex, then we are more or less
    // finished.  Just be sure to re-assign the futex owner for the requeue
    // futex if needed.
    if (wake_futex == nullptr) {
        if (requeue_futex != nullptr) {
            requeue_futex->owner_.swap(requeue_owner_thread);
            // TODO(johngro): re-evaluate the effective priorities both the
            // previous and new owners here.
        }
        return ZX_OK;
    }

    // Before waking up any threads, move any pre-existing futex owner
    // reference into a scope where it will be released after we exit the
    // futex lock.  When we are done with the wake operation, the new futex
    // owner will be either nothing, or it will be the thread which we just
    // woke up, but it is not going to be the previous owner.
    previous_wake_owner = ktl::move(wake_futex->owner_);

    // Disable rescheduling and enter the thread lock.
    resched_disable.Disable();
    bool wake_futex_emptied;
    {
        Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};

        // Wake up the number of threads we were asked to wake.
        bool do_resched = WakeThreads(wake_futex, wake_count, owner_action);

        // If there are still threads left in the wake futex, and we were asked to
        // requeue threads, then do so.
        if (!wake_futex->waiters_.IsEmpty() && requeue_count) {
            // Start by making sure that we have a FutexState for the requeue futex.
            // We may not have one if there are no current waiters.
            if (requeue_futex == nullptr) {
                requeue_futex = ActivateFromPool(requeue_id);
            }

            // Assign the new futex owner if asked to do so.
            requeue_futex->owner_.swap(requeue_owner_thread);

            // Now actually requeue the threads.
            for (uint32_t i = 0; i < requeue_count; ++i) {
                thread_t* requeued = WaitQueue::RequeueOne(&wake_futex->waiters_,
                                                           &requeue_futex->waiters_);
                if (!requeued) {
                    break;
                }

                DEBUG_ASSERT(requeued->user_thread);
                DEBUG_ASSERT(requeued->user_thread->blocking_futex_id() == wake_id);
                requeued->user_thread->blocking_futex_id() = requeue_id;
            }
        }

        // Make a note of whether or not we have emptied the wake_futex's wait
        // list while we are inside of the thread lock.  If we have, then return
        // the structure to the free pool as we exit, after we have exited the
        // lock.
        wake_futex_emptied = wake_futex->waiters_.IsEmpty();

        // TODO(johngro): re-evaluate effective priorities.  We need to consider...
        //
        // 1) previous_wake_owner:  This thread (if it exists) no longer feels
        //    the pressure from the wake_futex waiters.
        // 2) requeue_owner_thread:  This thread (if it exists) no longer feels
        //    the pressure from the requeue_futex waiters.
        // 3) wake_futex->owner_: This thread (if it exists) now feels the
        //    pressure of the wake_futex waiters.  Note that this can be skipped
        //    if the wake futex has become emptied.
        // 4) requeue_futex->owner_: This thread (if it exists) now feels the
        //    pressure of the requeue_futex waiters.
        //
        // If any thread's effective priority has changed, we need to request a
        // reschedule.
        if (do_resched) {
            sched_reschedule();
        }
    }

    if (wake_futex_emptied) {
        emptied_wake_owner = ReturnToPool(wake_futex);
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
        if ((futex != nullptr) && (futex->owner_ != nullptr)) {
            koid = futex->owner_->get_koid();
        }
    }

    return koid_out.copy_to_user(koid);
}

bool FutexContext::WakeThreads(FutexState* futex, uint32_t wake_count, OwnerAction owner_action) {
    bool do_resched = false;

    DEBUG_ASSERT(futex != nullptr);
    for (uint32_t i = 0; i < wake_count; ++i) {
        thread_t* woken = futex->waiters_.DequeueOne(ZX_OK);

        if (!woken) {
            break;
        }

        // Only user mode threads should ever be blocked on a futex.  Any thread
        // woken from a futex should have been previously blocked by that futex,
        // but not be blocked by it anymore.
        DEBUG_ASSERT(woken->user_thread != nullptr);
        DEBUG_ASSERT(woken->user_thread->blocking_futex_id() == futex->id());
        woken->user_thread->blocking_futex_id() = 0;

        if (owner_action == OwnerAction::ASSIGN_WOKEN) {
            // ASSIGN_WOKEN is only valid when waking one thread.
            DEBUG_ASSERT(wake_count == 1);
            // The owner of a futex should have been moved out of the FutexState
            // before anyone ever calls WakeThreads.
            DEBUG_ASSERT(futex->owner_ == nullptr);

            futex->owner_ = fbl::WrapRefPtr(woken->user_thread);

            // TODO(johngro): re-evaluate the effective priority of
            // |woken| here.  Set do_resched if it has changed.
        }

        if (sched_unblock(woken)) {
            do_resched = true;
        }
    }

    return do_resched;
}
