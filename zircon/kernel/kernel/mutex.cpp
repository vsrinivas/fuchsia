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

/**
 * @brief  mutex_t destructor
 *
 * This function performs sanity checks, calls the wait_queue_t destructor
 * equivalent, and invalidated the state of the internal mutex storage (eg;
 * invalidates the magic number).
 */
mutex::~mutex() {
    DEBUG_ASSERT(magic == MUTEX_MAGIC);
    DEBUG_ASSERT(!arch_blocking_disallowed());

#if LK_DEBUGLEVEL > 0
    if (unlikely(mutex_val(this) != 0)) {
        thread_t* holder = mutex_holder(this);
        panic("mutex_destroy: thread %p (%s) tried to destroy locked mutex %p,"
              " locked by %p (%s)\n",
              get_current_thread(), get_current_thread()->name, this,
              holder, holder->name);
    }
#endif
    magic = 0;
    val.store(0, ktl::memory_order_relaxed);
    wait_queue_destroy(&wait);
}

/**
 * @brief  Acquire the mutex
 */
void mutex_acquire(mutex_t* m) TA_NO_THREAD_SAFETY_ANALYSIS {
    DEBUG_ASSERT(m->magic == MUTEX_MAGIC);
    DEBUG_ASSERT(!arch_blocking_disallowed());

    thread_t* ct = get_current_thread();
    uintptr_t oldval;

retry:
    // fast path: assume its unheld, try to grab it
    oldval = 0;
    if (likely(m->val.compare_exchange_strong(oldval, reinterpret_cast<uintptr_t>(ct),
                                              ktl::memory_order_seq_cst,
                                              ktl::memory_order_seq_cst))) {
        // acquired it cleanly
        ct->mutexes_held++;
        return;
    }

#if LK_DEBUGLEVEL > 0
    if (unlikely(ct == mutex_holder(m)))
        panic("mutex_acquire: thread %p (%s) tried to acquire mutex %p it already owns.\n",
              ct, ct->name, m);
#endif

    {
        // we contended with someone else, will probably need to block
        Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

        // save the current state and check to see if it wasn't released in the interim
        oldval = mutex_val(m);
        if (unlikely(oldval == 0)) {
            goto retry;
        }

        // try to exchange again with a flag indicating that we're blocking is set
        if (unlikely(!m->val.compare_exchange_strong(oldval, oldval | MUTEX_FLAG_QUEUED,
                                                     ktl::memory_order_seq_cst,
                                                     ktl::memory_order_seq_cst))) {
            // if we fail, just start over from the top
            goto retry;
        }

        // have the holder inherit our priority
        // discard the local reschedule flag because we're just about to block anyway
        bool unused;
        sched_inherit_priority(mutex_holder(m), ct->effec_priority, &unused);

        // we have signalled that we're blocking, so drop into the wait queue
        zx_status_t ret = wait_queue_block(&m->wait, ZX_TIME_INFINITE);
        if (unlikely(ret < ZX_OK)) {
            // mutexes are not interruptable and cannot time out, so it
            // is illegal to return with any error state.
            panic("mutex_acquire: wait_queue_block returns with error %d m %p, thr %p, sp %p\n",
                  ret, m, ct, __GET_FRAME());
        }

        // someone must have woken us up, we should own the mutex now
        DEBUG_ASSERT(ct == mutex_holder(m));

        // record that we hold it
        ct->mutexes_held++;
    }
}

// shared implementation of release
static inline void mutex_release_internal(mutex_t* m, bool reschedule, bool thread_lock_held)
    TA_NO_THREAD_SAFETY_ANALYSIS {
    thread_t* ct = get_current_thread();
    uintptr_t oldval;

    // we're going to release it, mark as such
    ct->mutexes_held--;

    // in case there's no contention, try the fast path
    oldval = (uintptr_t)ct;
    if (likely(m->val.compare_exchange_strong(oldval, 0, ktl::memory_order_seq_cst,
                                              ktl::memory_order_seq_cst))) {
        // we're done, exit
        // if we had inherited any priorities, undo it if we are no longer holding any mutexes
        if (unlikely(ct->inherited_priority >= 0) && ct->mutexes_held == 0) {
            spin_lock_saved_state_t state;
            if (!thread_lock_held) {
                spin_lock_irqsave(&thread_lock, state);
            }

            bool local_resched = false;
            sched_inherit_priority(ct, -1, &local_resched);
            if (reschedule && local_resched) {
                sched_reschedule();
            }

            if (!thread_lock_held) {
                spin_unlock_irqrestore(&thread_lock, state);
            }
        }
        return;
    }

    DEBUG_ASSERT(ct->mutexes_held >= 0);

// must have been some contention, try the slow release

#if LK_DEBUGLEVEL > 0
    if (unlikely(ct != mutex_holder(m))) {
        thread_t* holder = mutex_holder(m);
        panic("mutex_release: thread %p (%s) tried to release mutex %p it doesn't own. owned by %p (%s)\n",
              ct, ct->name, m, holder, holder ? holder->name : "none");
    }
#endif

    // conditionally acquire/release the thread lock
    // NOTE: using the manual spinlock grab/release instead of THREAD_LOCK because
    // the state variable needs to exit in either path.
    spin_lock_saved_state_t state;
    if (!thread_lock_held) {
        spin_lock_irqsave(&thread_lock, state);
    }

    // release a thread in the wait queue
    thread_t* t = wait_queue_dequeue_one(&m->wait, ZX_OK);
    DEBUG_ASSERT_MSG(t,
                     "mutex_release: wait queue didn't have anything, but m->val = %#" PRIxPTR "\n",
                     mutex_val(m));

    // we woke up a thread, mark the mutex owned by that thread
    uintptr_t newval = (uintptr_t)t | (wait_queue_is_empty(&m->wait) ? 0 : MUTEX_FLAG_QUEUED);

    oldval = (uintptr_t)ct | MUTEX_FLAG_QUEUED;
    if (!m->val.compare_exchange_strong(oldval, newval, ktl::memory_order_seq_cst,
                                        ktl::memory_order_seq_cst)) {
        panic("bad state in mutex release %p, current thread %p\n", m, ct);
    }

    ktrace_ptr(TAG_KWAIT_WAKE, &m->wait, 1, 0);

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

    // conditionally THREAD_UNLOCK
    if (!thread_lock_held) {
        spin_unlock_irqrestore(&thread_lock, state);
    }
}

void mutex_release(mutex_t* m) TA_NO_THREAD_SAFETY_ANALYSIS {
    DEBUG_ASSERT(m->magic == MUTEX_MAGIC);
    DEBUG_ASSERT(!arch_blocking_disallowed());

    // default release will reschedule if any threads are woken up and acquire the thread lock
    mutex_release_internal(m, true, false);
}

void mutex_release_thread_locked(mutex_t* m, bool reschedule) TA_NO_THREAD_SAFETY_ANALYSIS {
    DEBUG_ASSERT(m->magic == MUTEX_MAGIC);
    DEBUG_ASSERT(!arch_blocking_disallowed());
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    // this special version of release will pass through the reschedule flag and not acquire
    // the thread_lock
    mutex_release_internal(m, reschedule, true);
}
