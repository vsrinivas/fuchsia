// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/brwlock.h>
#include <kernel/thread_lock.h>

BrwLock::~BrwLock() {
    DEBUG_ASSERT(state_.load(ktl::memory_order_relaxed) == 0);
}

void BrwLock::Block(bool write) {
    thread_t* ct = get_current_thread();

    // TODO(johngro): Block here in an OwnedWaitQueue where we specify the owner
    // as writer_ in order to transmit our priority pressure for PI purposes.

    zx_status_t ret =
        write ? wait_.Block(Deadline::infinite()) : wait_.BlockReadLock(Deadline::infinite());
    if (unlikely(ret < ZX_OK)) {
        panic("BrwLock::Block: wait_queue_block returns with error %d lock %p, thr %p, sp %p\n",
              ret, this, ct, __GET_FRAME());
    }
}

void BrwLock::WakeThread(thread_t* thread, uint64_t adjust) {
    state_.fetch_add(-kBrwLockWaiter + adjust, ktl::memory_order_acq_rel);
    zx_status_t status = wait_.UnblockThread(thread, ZX_OK);
    if (status != ZX_OK) {
        panic("Tried to unblock thread from wait queue that was not blocked");
    }
}

void BrwLock::WakeReaders() {
    while (!wait_.IsEmpty()) {
        thread_t* next = wait_.Peek();
        if (next->state != THREAD_BLOCKED_READ_LOCK) {
            break;
        }
        WakeThread(next, kBrwLockReader);
    }
}

void BrwLock::WakeWriter(thread_t* thread) {
    DEBUG_ASSERT(thread);
    writer_.store(thread, ktl::memory_order_relaxed);
    WakeThread(thread, kBrwLockWriter);
}

void BrwLock::WakeAny() {
    thread_t* next = wait_.Peek();
    DEBUG_ASSERT(next != NULL);
    if (next->state == THREAD_BLOCKED_READ_LOCK) {
        WakeReaders();
    } else {
        WakeWriter(next);
    }
}

void BrwLock::ContendedReadAcquire() {
    // In the case where we wake other threads up we need them to not run until we're finished
    // holding the thread_lock, so disable local rescheduling.
    AutoReschedDisable resched_disable;
    resched_disable.Disable();
    {
        Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
        // Remove our optimistic reader from the count, and put a waiter on there instead.
        uint64_t prev = state_.fetch_add(-kBrwLockReader + kBrwLockWaiter, ktl::memory_order_relaxed);
        // If there is a writer then we just block, they will wake us up
        if (prev & kBrwLockWriter) {
            Block(false);
            return;
        }
        // If we raced and there is in fact no one waiting then we can switch to
        // having the lock
        if ((prev & kBrwLockWaiterMask) == 0) {
            state_.fetch_add(-kBrwLockWaiter + kBrwLockReader, ktl::memory_order_acquire);
            return;
        }
        thread_t* next = wait_.Peek();
        DEBUG_ASSERT(next != NULL);
        if (next->state == THREAD_BLOCKED_READ_LOCK) {
            WakeReaders();
            // Join the reader pool.
            state_.fetch_add(-kBrwLockWaiter + kBrwLockReader, ktl::memory_order_acquire);
            return;
        }
        // If there are no current readers then we unblock this writer, since
        // otherwise nobody will be using the lock.
        if ((prev & kBrwLockReaderMask) == 1) {
            WakeWriter(next);
        }

        Block(false);
    }
}

void BrwLock::ContendedWriteAcquire() {
    // In the case where we wake other threads up we need them to not run until we're finished
    // holding the thread_lock, so disable local rescheduling.
    AutoReschedDisable resched_disable;
    resched_disable.Disable();
    {
        Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
        // Mark ourselves as waiting
        uint64_t prev = state_.fetch_add(kBrwLockWaiter, ktl::memory_order_relaxed);
        // If there is a writer then we just block, they will wake us up
        if (prev & kBrwLockWriter) {
            Block(true);
            return;
        }
        if ((prev & kBrwLockReaderMask) == 0) {
            if ((prev & kBrwLockWaiterMask) == 0) {
                writer_.store(get_current_thread(), ktl::memory_order_relaxed);
                // Must have raced previously as turns out there's no readers or
                // waiters, so we can convert to having the lock
                state_.fetch_add(-kBrwLockWaiter + kBrwLockWriter, ktl::memory_order_acquire);
                return;
            } else {
                // There's no readers, but someone already waiting, wake up someone
                // before we ourselves block
                WakeAny();
            }
        }
        Block(true);
    }
}

void BrwLock::WriteRelease() TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();

#if LK_DEBUG_LEVEL > 0
    thread_t* holder = writer_.load(ktl::memory_order_relaxed);
    if (unlikely(ct != holder)) {
        panic("BrwLock::WriteRelease: thread %p (%s) tried to release brwlock %p it doesn't "
              "own. Ownedby %p (%s)\n",
              ct, ct->name, this, holder, holder ? holder->name : "none");
    }
#endif

    // Drop the `writer` before updating `state`. The race here of another thread
    // observing a null `writer` and 'failing' to do PI in `Block` does not matter
    // since we're already doing release and would only immediately give the
    // donation back.
    writer_.store(nullptr, ktl::memory_order_relaxed);
    uint64_t prev = state_.fetch_sub(kBrwLockWriter, ktl::memory_order_release);

    // Perform release wakeup prior to deboosting our priority as we can be
    // certain we aren't racing with someone trying to Block after that
    if (unlikely((prev & kBrwLockWaiterMask) != 0)) {
        // There are waiters, we need to wake them up
        ReleaseWakeup();
    }
}

void BrwLock::ReleaseWakeup() {
    // Don't reschedule whilst we're waking up all the threads as if there are
    // several readers available then we'd like to get them all out of the wait queue.
    AutoReschedDisable resched_disable;
    resched_disable.Disable();
    {
        Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
        uint64_t count = state_.load(ktl::memory_order_relaxed);
        if ((count & kBrwLockWaiterMask) != 0 && (count & kBrwLockWriter) == 0 &&
            (count & kBrwLockReaderMask) == 0) {
            WakeAny();
        }
    }
}

void BrwLock::ContendedReadUpgrade() {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

    // Convert our reading into waiting
    uint64_t prev = state_.fetch_add(-kBrwLockReader + kBrwLockWaiter, ktl::memory_order_relaxed);
    if ((prev & ~kBrwLockWaiterMask) == kBrwLockReader) {
        writer_.store(get_current_thread(), ktl::memory_order_relaxed);
        // There are no writers or readers. There might be waiters, but as we
        // already have some form of lock we still have fairness even if we
        // bypass the queue, so we convert our waiting into writing
        state_.fetch_add(-kBrwLockWaiter + kBrwLockWriter, ktl::memory_order_acquire);
    } else {
        Block(true);
    }
}
