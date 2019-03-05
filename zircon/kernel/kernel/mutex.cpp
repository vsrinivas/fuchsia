// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
// Copyright (c) 2012-2012 Shantanu Gupta
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/**
 * @file
 * @brief  Mutex functions
 *
 * @defgroup mutex Mutex
 * @{
 */

#include <kernel/mutex.h>

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/sched.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <lib/ktrace.h>
#include <trace.h>
#include <zircon/types.h>

#define LOCAL_TRACE 0

Mutex::~Mutex() {
    magic_.Assert();
    DEBUG_ASSERT(!arch_blocking_disallowed());

#if LK_DEBUGLEVEL > 0
    if (unlikely(val() != 0)) {
        thread_t* h = holder();
        panic("~Mutex(): thread %p (%s) tried to destroy locked mutex %p, locked by %p (%s)\n",
              get_current_thread(), get_current_thread()->name, this, h, h->name);
    }
#endif

    val_.store(0, ktl::memory_order_relaxed);
    wait_queue_destroy(&wait_);
}

/**
 * @brief  Acquire the mutex
 */
void Mutex::Acquire() {
    magic_.Assert();
    DEBUG_ASSERT(!arch_blocking_disallowed());

    thread_t* ct = get_current_thread();
    uintptr_t oldval;

    // fast path: assume its unheld, try to grab it
    oldval = 0;
    if (likely(val_.compare_exchange_strong(oldval, reinterpret_cast<uintptr_t>(ct),
                                            ktl::memory_order_seq_cst,
                                            ktl::memory_order_seq_cst))) {
        // acquired it cleanly
        ct->mutexes_held++;
        return;
    }

#if LK_DEBUGLEVEL > 0
    if (unlikely(ct == holder()))
        panic("Mutex::Acquire: thread %p (%s) tried to acquire mutex %p it already owns.\n",
              ct, ct->name, this);
#endif

    {
        // we contended with someone else, will probably need to block
        Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

        // Check if the queued flag is currently set. The queued flag can only be changed
        // whilst the thread lock is held so we know we aren't racing with anyone here. This
        // is just an optimization and allows us to avoid redundantly doing the atomic OR.
        oldval = val();
        thread_t* hold;
        if (unlikely(!(oldval & FLAG_QUEUED))) {
            // Set the queued flag to indicate that we're blocking.
            oldval = val_.fetch_or(FLAG_QUEUED, ktl::memory_order_seq_cst);
            // We may have raced with the holder as they dropped the mutex.
            if (unlikely(oldval == 0)) {
                // Since we set the queued flag we know that there are no waiters and no one
                // is able to perform fast path acquisition. Therefore we can just
                // take the mutex, and remove the queued flag.
                val_.store(reinterpret_cast<uintptr_t>(ct), ktl::memory_order_seq_cst);
                ct->mutexes_held++;
                return;
            }
        }
        // extract the current holder from oldval, no need to re-read from the mutex
        // as it cannot change if the queued flag is set without holding the thread lock (which
        // we currently hold).
        hold = holder_from_val(oldval);

        // have the holder inherit our priority
        // discard the local reschedule flag because we're just about to block anyway
        bool unused;
        sched_inherit_priority(hold, ct->effec_priority, &unused);

        // we have signalled that we're blocking, so drop into the wait queue
        zx_status_t ret = wait_queue_block(&wait_, ZX_TIME_INFINITE);
        if (unlikely(ret < ZX_OK)) {
            // mutexes are not interruptable and cannot time out, so it
            // is illegal to return with any error state.
            panic("Mutex::Acquire: wait_queue_block returns with error %d m %p, thr %p, sp %p"
                  "\n", ret, this, ct, __GET_FRAME());
        }

        // someone must have woken us up, we should own the mutex now
        DEBUG_ASSERT(ct == holder());

        // record that we hold it
        ct->mutexes_held++;
    }
}

// Shared implementation of release
template <Mutex::ThreadLockState TLS>
void Mutex::ReleaseInternal(bool reschedule) {
    thread_t* ct = get_current_thread();
    uintptr_t oldval;

    // we're going to release it, mark as such
    ct->mutexes_held--;

    // in case there's no contention, try the fast path
    oldval = (uintptr_t)ct;
    if (likely(val_.compare_exchange_strong(oldval, 0,
                                            ktl::memory_order_seq_cst,
                                            ktl::memory_order_seq_cst))) {
        // we're done, exit
        // if we had inherited any priorities, undo it if we are no longer holding any mutexes
        if (unlikely(ct->inherited_priority >= 0) && ct->mutexes_held == 0) {
            __UNUSED spin_lock_saved_state_t state;
            if constexpr (TLS == ThreadLockState::NotHeld) {
                spin_lock_irqsave(&thread_lock, state);
            }

            bool local_resched = false;
            sched_inherit_priority(ct, -1, &local_resched);
            if (reschedule && local_resched) {
                sched_reschedule();
            }

            if constexpr (TLS == ThreadLockState::NotHeld) {
                spin_unlock_irqrestore(&thread_lock, state);
            }
        }
        return;
    }
    DEBUG_ASSERT(oldval & FLAG_QUEUED);

    DEBUG_ASSERT(ct->mutexes_held >= 0);

    // must have been some contention, try the slow release
#if LK_DEBUGLEVEL > 0
    if (unlikely(ct != holder())) {
        thread_t* h = holder();
        panic("Mutex::ReleaseInternal: thread %p (%s) tried to release mutex %p it doesn't "
              "own. owned by %p (%s)\n",
              ct, ct->name, this, h, h ? h->name : "none");
    }
#endif

    // compile-time conditionally acquire/release the thread lock
    // NOTE: using the manual spinlock grab/release instead of THREAD_LOCK because
    // the state variable needs to exit in either path.
    __UNUSED spin_lock_saved_state_t state;
    if constexpr (TLS == ThreadLockState::NotHeld) {
        spin_lock_irqsave(&thread_lock, state);
    }

    // release a thread in the wait queue
    thread_t* t = wait_queue_dequeue_one(&wait_, ZX_OK);
    DEBUG_ASSERT_MSG(t, "Mutex::ReleaseInternal: wait queue didn't have anything, but "
                        "m->val = %#" PRIxPTR "\n", val());


    // we woke up a thread, mark the mutex owned by that thread. As we hold the lock we are
    // allowed to potentially change the queued flag so we may directly store our new value
    // without worry of clashing with someone else.
    uintptr_t newval = (uintptr_t)t | (wait_queue_is_empty(&wait_) ? 0 : FLAG_QUEUED);
    val_.store(newval, ktl::memory_order_seq_cst);

    ktrace_ptr(TAG_KWAIT_WAKE, &wait_, 1, 0);

    // deboost ourself if this is the last mutex we held
    bool local_resched = false;
    if (ct->inherited_priority >= 0 && ct->mutexes_held == 0) {
        sched_inherit_priority(ct, -1, &local_resched);
    }

    // wake up the new thread, putting it in a run queue on a cpu. reschedule if the local
    // cpu run queue was modified
    local_resched |= sched_unblock(t);
    if (reschedule && local_resched) {
        sched_reschedule();
    }

    // compile-time conditionally THREAD_UNLOCK
    if constexpr (TLS == ThreadLockState::NotHeld) {
        spin_unlock_irqrestore(&thread_lock, state);
    }
}

void Mutex::Release() {
    magic_.Assert();
    DEBUG_ASSERT(!arch_blocking_disallowed());

    // default release will reschedule if any threads are woken up and acquire the thread lock
    ReleaseInternal<ThreadLockState::NotHeld>(true);
}

void Mutex::ReleaseThreadLocked(bool reschedule) {
    magic_.Assert();
    DEBUG_ASSERT(!arch_blocking_disallowed());
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    // This special version of release will pass through the reschedule flag
    // and not acquire the thread_lock
    ReleaseInternal<ThreadLockState::Held>(reschedule);
}
