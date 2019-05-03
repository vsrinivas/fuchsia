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
#include <ktl/type_traits.h>
#include <lib/ktrace.h>
#include <trace.h>
#include <zircon/types.h>

#define LOCAL_TRACE 0

namespace {

enum class KernelMutexTracingLevel {
    None,       // No tracing is ever done.  All code drops out at compile time.
    Contested,  // Trace events are only generated when mutexes are contested.
    All         // Trace events are generated for all mutex interactions.
};

// By default, kernel mutex tracing is disabled.
template <KernelMutexTracingLevel = KernelMutexTracingLevel::None, typename = void>
class KTracer;

template <>
class KTracer<KernelMutexTracingLevel::None> {
public:
    KTracer() = default;
    void KernelMutexUncontestedAcquire(const Mutex* mutex) {}
    void KernelMutexUncontestedRelease(const Mutex* mutex) {}
    void KernelMutexBlock(const Mutex* mutex, thread_t* blocker, uint32_t waiter_count) {}
    void KernelMutexWake(const Mutex* mutex, thread_t* new_owner, uint32_t waiter_count) {}
};

template <KernelMutexTracingLevel Level>
class KTracer<Level,
              ktl::enable_if_t<(Level == KernelMutexTracingLevel::Contested) ||
                               (Level == KernelMutexTracingLevel::All)>> {
public:
    KTracer() : ts_(ktrace_timestamp()) {}

    void KernelMutexUncontestedAcquire(const Mutex* mutex) {
        if constexpr (Level == KernelMutexTracingLevel::All) {
            KernelMutexTrace(TAG_KERNEL_MUTEX_ACQUIRE, mutex, nullptr, 0);
        }
    }

    void KernelMutexUncontestedRelease(const Mutex* mutex) {
        if constexpr (Level == KernelMutexTracingLevel::All) {
            KernelMutexTrace(TAG_KERNEL_MUTEX_RELEASE, mutex, nullptr, 0);
        }
    }

    void KernelMutexBlock(const Mutex* mutex, const thread_t* blocker, uint32_t waiter_count) {
        KernelMutexTrace(TAG_KERNEL_MUTEX_BLOCK, mutex, blocker, waiter_count);
    }

    void KernelMutexWake(const Mutex* mutex, const thread_t* new_owner, uint32_t waiter_count) {
        KernelMutexTrace(TAG_KERNEL_MUTEX_RELEASE, mutex, new_owner, waiter_count);
    }

private:
    void KernelMutexTrace(uint32_t tag,
                          const Mutex* mutex,
                          const thread_t* t,
                          uint32_t waiter_count) {
        uint32_t mutex_id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mutex));
        uint32_t tid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(t));
        uint32_t flags = static_cast<uint32_t>(
                arch_curr_cpu_num() & KTRACE_FLAGS_KERNEL_MUTEX_CPUID_MASK);

        if ((t != nullptr) && (t->user_thread != nullptr)) {
            tid = static_cast<uint32_t>(t->user_tid);
            flags |= KTRACE_FLAGS_KERNEL_MUTEX_USER_MODE_TID;
        }

        ktrace(tag, mutex_id, tid, waiter_count, flags, ts_);
    }

    const uint64_t ts_;
};

}  // anon namespace

Mutex::~Mutex() {
    magic_.Assert();
    DEBUG_ASSERT(!arch_blocking_disallowed());

    if (LK_DEBUGLEVEL > 0) {
        if (val() != STATE_FREE) {
            thread_t* h = holder();
            panic("~Mutex(): thread %p (%s) tried to destroy locked mutex %p, locked by %p (%s)\n",
                  get_current_thread(), get_current_thread()->name, this, h, h->name);
        }
    }

    val_.store(STATE_FREE, ktl::memory_order_relaxed);
}

/**
 * @brief  Acquire the mutex
 */
void Mutex::Acquire() {
    magic_.Assert();
    DEBUG_ASSERT(!arch_blocking_disallowed());

    thread_t* ct = get_current_thread();
    uintptr_t old_mutex_state;
    uintptr_t new_mutex_state;

    // fast path: assume the mutex is unlocked and try to grab it
    //
    old_mutex_state = STATE_FREE;
    new_mutex_state = reinterpret_cast<uintptr_t>(ct);
    if (likely(val_.compare_exchange_strong(old_mutex_state, new_mutex_state,
                                            ktl::memory_order_seq_cst,
                                            ktl::memory_order_seq_cst))) {
        // acquired it cleanly.  Don't bother to update the ownership of our
        // wait queue.  As of this instant, the mutex appears to be uncontested.
        // If someone else attempts to acquire the mutex and discovers it to be
        // already locked, they will take care of updating the wait queue
        // ownership while they are inside of the thread_lock.
        KTracer{}.KernelMutexUncontestedAcquire(this);
        return;
    }

    if ((LK_DEBUGLEVEL > 0) && unlikely(this->IsHeld())) {
        panic("Mutex::Acquire: thread %p (%s) tried to acquire mutex %p it already owns.\n",
              ct, ct->name, this);
    }

    {
        // we contended with someone else, will probably need to block
        Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

        // Check if the queued flag is currently set. The contested flag can only be changed
        // whilst the thread lock is held so we know we aren't racing with anyone here. This
        // is just an optimization and allows us to avoid redundantly doing the atomic OR.
        old_mutex_state = val();

        if (unlikely(!(old_mutex_state & STATE_FLAG_CONTESTED))) {
            // Set the queued flag to indicate that we're blocking.
            old_mutex_state = val_.fetch_or(STATE_FLAG_CONTESTED, ktl::memory_order_seq_cst);
            // We may have raced with the holder as they dropped the mutex.
            if (unlikely(old_mutex_state == STATE_FREE)) {
                // Since we set the contested flag we know that there are no
                // waiters and no one is able to perform fast path acquisition.
                // Therefore we can just take the mutex, and remove the queued
                // flag.
                val_.store(reinterpret_cast<uintptr_t>(ct), ktl::memory_order_seq_cst);
                return;
            }
        }

        // extract the current holder of the mutex from oldval, no need to
        // re-read from the mutex as it cannot change if the queued flag is set
        // without holding the thread lock (which we currently hold).  We need
        // to be sure that we inform our owned wait queue that this is the
        // proper queue owner as we block.
        thread_t* cur_owner = holder_from_val(old_mutex_state);
        KTracer{}.KernelMutexBlock(this, cur_owner, wait_.Count() + 1);
        zx_status_t ret = wait_.BlockAndAssignOwner(Deadline::infinite(),
                                                    cur_owner,
                                                    ResourceOwnership::Normal);

        if (unlikely(ret < ZX_OK)) {
            // mutexes are not interruptible and cannot time out, so it
            // is illegal to return with any error state.
            panic("Mutex::Acquire: wait queue block returns with error %d m %p, thr %p, sp %p\n",
                   ret, this, ct, __GET_FRAME());
        }

        // someone must have woken us up, we should own the mutex now
        DEBUG_ASSERT(ct == holder());
    }
}

// Shared implementation of release
template <Mutex::ThreadLockState TLS>
void Mutex::ReleaseInternal(const bool allow_reschedule) {
    thread_t* ct = get_current_thread();

    // Try the fast path.  Assume that we are locked, but uncontested.
    uintptr_t old_mutex_state = reinterpret_cast<uintptr_t>(ct);
    if (likely(val_.compare_exchange_strong(old_mutex_state, STATE_FREE,
                                            ktl::memory_order_seq_cst,
                                            ktl::memory_order_seq_cst))) {
        // We're done.  Since this mutex was uncontested, we know that we were
        // not receiving any priority pressure from the wait queue, and there is
        // nothing further to do.
        KTracer{}.KernelMutexUncontestedRelease(this);
        return;
    }

    // Sanity checks.  The mutex should have been either locked by us and
    // uncontested, or locked by us and contested.  Anything else is an internal
    // consistency error worthy of a panic.
    if (LK_DEBUGLEVEL > 0) {
        uintptr_t expected_state = reinterpret_cast<uintptr_t>(ct) | STATE_FLAG_CONTESTED;

        if (unlikely(old_mutex_state != expected_state)) {
            auto other_holder = reinterpret_cast<thread_t*>(old_mutex_state & ~STATE_FLAG_CONTESTED);
            panic("Mutex::ReleaseInternal: sanity check failure.  Thread %p (%s) tried to release "
                  "mutex %p.  Expected state (%lx) != observed state (%lx).  Other holder (%s)\n",
                  ct, ct->name, this, expected_state, old_mutex_state,
                  other_holder ? other_holder->name : "<none>");
        }
    }

    // compile-time conditionally acquire/release the thread lock
    // NOTE: using the manual spinlock grab/release instead of THREAD_LOCK because
    // the state variable needs to exit in either path.
    __UNUSED spin_lock_saved_state_t irq_state;
    if constexpr (TLS == ThreadLockState::NotHeld) {
        spin_lock_irqsave(&thread_lock, irq_state);
    }

    // Attempt to release a thread. If there are still waiters in the queue
    // after we successfully have woken a thread, be sure to assign ownership of
    // the queue to the thread which was woken so that it can properly receive
    // the priority pressure of the remaining waiters.
    using Action = OwnedWaitQueue::Hook::Action;
    thread_t* woken;
    auto cbk = [](thread_t* woken, void* ctx) -> Action {
        *(reinterpret_cast<thread**>(ctx)) = woken;
        return Action::SelectAndAssignOwner;
    };

    KTracer tracer;
    bool need_reschedule = wait_.WakeThreads(1, { cbk, &woken });
    tracer.KernelMutexWake(this, woken, wait_.Count());

    ktrace_ptr(TAG_KWAIT_WAKE, &wait_, 1, 0);

    // So, the mutex is now in one of three states.  It can be...
    //
    // 1) Owned and contested (we woke a thread up, and there are still waiters)
    // 2) Owned and uncontested (we woke a thread up, but it was the last one)
    // 3) Unowned (no thread woke up when we tried to wake one)
    //
    // Note, the only way to be in situation #3 is for the lock to have become
    // contested at some point in the past, but then to have a thread stop
    // waiting for the lock before acquiring it (either it timed out or was
    // killed).
    //
    uintptr_t new_mutex_state;
    if (woken != nullptr) {
        // We woke _someone_ up.  We be in situation #1 or #2
        new_mutex_state = reinterpret_cast<uintptr_t>(woken);
        if (!wait_.IsEmpty()) {
            // Situation #1.
            DEBUG_ASSERT(wait_.owner() == woken);
            new_mutex_state |= STATE_FLAG_CONTESTED;
        } else {
            // Situation #2.
            DEBUG_ASSERT(wait_.owner() == nullptr);
        }
    } else {
        DEBUG_ASSERT(wait_.IsEmpty());
        DEBUG_ASSERT(wait_.owner() == nullptr);
        new_mutex_state = STATE_FREE;
    }

    if (unlikely(!val_.compare_exchange_strong(old_mutex_state, new_mutex_state,
                                               ktl::memory_order_seq_cst,
                                               ktl::memory_order_seq_cst))) {
        panic("bad state (%lx != %lx) in mutex release %p, current thread %p\n",
                reinterpret_cast<uintptr_t>(ct) | STATE_FLAG_CONTESTED,
                old_mutex_state, this, ct);
    }

    if (allow_reschedule && need_reschedule) {
        sched_reschedule();
    }

    // compile-time conditionally THREAD_UNLOCK
    if constexpr (TLS == ThreadLockState::NotHeld) {
        spin_unlock_irqrestore(&thread_lock, irq_state);
    }
}

void Mutex::Release() {
    magic_.Assert();
    DEBUG_ASSERT(!arch_blocking_disallowed());

    // default release will reschedule if any threads are woken up and acquire the thread lock
    ReleaseInternal<ThreadLockState::NotHeld>(true);
}

void Mutex::ReleaseThreadLocked(const bool allow_reschedule) {
    magic_.Assert();
    DEBUG_ASSERT(!arch_blocking_disallowed());
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    // This special version of release will pass through the allow_reschedule flag
    // and not acquire the thread_lock
    ReleaseInternal<ThreadLockState::Held>(allow_reschedule);
}
