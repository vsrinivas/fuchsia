// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <debug.h>
#include <fbl/canary.h>
#include <kernel/sched.h>
#include <kernel/thread.h>
#include <kernel/wait.h>
#include <ktl/atomic.h>
#include <stdint.h>
#include <zircon/thread_annotations.h>

// Blocking (i.e. non spinning) reader-writer lock. Readers and writers are
// ordered by priority (i.e. their wait_queue release order) and otherwise
// readers and writers are treated equally and will fall back to FIFO ordering
// at some priority.
class TA_CAP("mutex") BrwLock {
public:
    BrwLock() : state_(0), writer_(nullptr) {}
    ~BrwLock();

    void ReadAcquire() TA_ACQ_SHARED(this) TA_NO_THREAD_SAFETY_ANALYSIS {
        DEBUG_ASSERT(!arch_blocking_disallowed());
        canary_.Assert();
        // Attempt the optimistic grab
        uint64_t prev = state_.fetch_add(kBrwLockReader, ktl::memory_order_acquire);
        // See if there are only readers
        if (unlikely((prev & kBrwLockReaderMask) != prev)) {
            ContendedReadAcquire();
        }
    }

    void WriteAcquire() TA_ACQ(this) {
        DEBUG_ASSERT(!arch_blocking_disallowed());
        canary_.Assert();
        // When acquiring the write lock we require there be no-one else using
        // the lock.
        CommonWriteAcquire(0, [this] { ContendedWriteAcquire(); });
    }

    void WriteRelease() TA_REL(this);

    void ReadRelease() TA_REL_SHARED(this) TA_NO_THREAD_SAFETY_ANALYSIS {
        canary_.Assert();
        uint64_t prev = state_.fetch_sub(kBrwLockReader, ktl::memory_order_release);
        if (unlikely((prev & kBrwLockReaderMask) == 1 && (prev & kBrwLockWaiterMask) != 0)) {
            // there are no readers but still some waiters, becomes our job to wake them up
            ReleaseWakeup();
        }
    }

    void ReadUpgrade() TA_REL_SHARED(this) TA_ACQ(this) TA_NO_THREAD_SAFETY_ANALYSIS {
        canary_.Assert();
        DEBUG_ASSERT(!arch_blocking_disallowed());
        // To upgrade we require that we as a current reader be the only current
        // user of the lock.
        CommonWriteAcquire(kBrwLockReader, [this] { ContendedReadUpgrade(); });
    }

    // suppress default constructors
    DISALLOW_COPY_ASSIGN_AND_MOVE(BrwLock);

    // Tag structs needed for linking BrwLock acquisition options to the different
    // policy structures. See LOCK_DEP_POLICY_OPTION usage below.
    struct Reader {};
    struct Writer {};

    struct ReaderPolicy {
        struct State {};
        // This will be seen by Guard to know to generate shared acquisitions for thread analysis.
        struct Shared {};

        static bool Acquire(BrwLock* lock, State* state) TA_ACQ_SHARED(lock) {
            lock->ReadAcquire();
            return true;
        }
        static void Release(BrwLock* lock, State* state) TA_REL_SHARED(lock) {
            lock->ReadRelease();
        }
    };

    struct WriterPolicy {
        struct State {};

        static bool Acquire(BrwLock* lock, State* state) TA_ACQ(lock) {
            lock->WriteAcquire();
            return true;
        }
        static void Release(BrwLock* lock, State* state) TA_REL(lock) { lock->WriteRelease(); }
    };

private:
    // We count readers in the low part of the state
    static constexpr uint64_t kBrwLockReader = 1;
    static constexpr uint64_t kBrwLockReaderMask = 0xFFFFFFFF;
    // We count waiters in all but the MSB of the state
    static constexpr uint64_t kBrwLockWaiter = 1ul << 32;
    static constexpr uint64_t kBrwLockWaiterMask = 0x7FFFFFFF00000000;
    // Writer is in the MSB
    static constexpr uint64_t kBrwLockWriter = 1ul << 63;

    void ContendedReadAcquire();
    void ContendedWriteAcquire();
    void ContendedReadUpgrade();
    void ReleaseWakeup();
    void Block(bool write) TA_REQ(thread_lock);
    void WakeAny() TA_REQ(thread_lock);
    void WakeThread(thread_t* thread, uint64_t adjust) TA_REQ(thread_lock);
    void WakeReaders() TA_REQ(thread_lock);
    void WakeWriter(thread_t* thread) TA_REQ(thread_lock) TA_REQ(thread_lock);

    template <typename F>
    void CommonWriteAcquire(uint64_t expected_state, F contended)
        TA_ACQ(this) TA_NO_THREAD_SAFETY_ANALYSIS {
        thread_t* ct = get_current_thread();
        // First set the kBrwLockWriter bit in the state and then set the
        // `writer` variable. This leaves a window where another thread could
        // see the lock is contended, go to block, but because `writer` isn't
        // yet set fail to donate its priority. This can only happen if we
        // become descheduled between setting `state` and setting `writer` and
        // hence there is a long enough window for another thread to observe
        // a contended lock, acquiring the thread_lock, and then still see an
        // empty `writer` variable.
        //
        // We could first attempt to set `writer` and then the state, but there
        // is still a race where we get descheduled between setting `writer`
        // and `state`, another thread acquires and releases the write lock,
        // putting us back into the same condition we were trying to avoid.
        //
        // The currently remaining race could be removed by disabling preemption,
        // but this is not free and the performance should be measured and balanced
        // with the risk of failing to perform correct priority inheritance.
        bool success = state_.compare_exchange_weak(
            expected_state, kBrwLockWriter, ktl::memory_order_acquire, ktl::memory_order_relaxed);
        if (likely(success)) {
            writer_.store(ct, ktl::memory_order_relaxed);
        } else {
            contended();
        }

        DEBUG_ASSERT(writer_.load(ktl::memory_order_relaxed) == ct);
    }

    fbl::Canary<fbl::magic("RWLK")> canary_;
    ktl::atomic<uint64_t> state_;
    ktl::atomic<thread_t*> writer_;
    WaitQueue wait_;
};

#define DECLARE_BRWLOCK(container_type) LOCK_DEP_INSTRUMENT(container_type, BrwLock)

// Configure fbl::Guard<BrwLock, BrwLock::Writer> write locks through the given policy.
LOCK_DEP_POLICY_OPTION(BrwLock, BrwLock::Writer, BrwLock::WriterPolicy);

// Configure fbl::Guard<BrwLock, BrwLock::Reader> read locks through the given policy.
LOCK_DEP_POLICY_OPTION(BrwLock, BrwLock::Reader, BrwLock::ReaderPolicy);
