// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/futex.h>
#include <lib/zx/event.h>
#include <limits>
#include <unittest/unittest.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "utils.h"

namespace {

// Templated operation adapters which allow us to test the wake operation using
// the same code for zx_wake and zx_requeue
enum class OpType {
    kStandard,
    kRequeue,
};

template <OpType OPERATION>
struct WakeOperation;

template <>
struct WakeOperation<OpType::kStandard> {
    static zx_status_t wake(const fbl::futex_t& wake_futex, uint32_t count) {
        return zx_futex_wake(&wake_futex, count);
    }

    static zx_status_t wake_single_owner(const fbl::futex_t& wake_futex) {
        return zx_futex_wake_single_owner(&wake_futex);
    }
};

template <>
struct WakeOperation<OpType::kRequeue> {
    static zx_status_t wake(const fbl::futex_t& wake_futex, uint32_t count) {
        const fbl::futex_t& requeue_futex(0);
        return zx_futex_requeue(&wake_futex, count,
                                0, &requeue_futex, 0u, ZX_HANDLE_INVALID);
    }

    static zx_status_t wake_single_owner(const fbl::futex_t& wake_futex) {
        const fbl::futex_t& requeue_futex(0);
        return zx_futex_requeue_single_owner(&wake_futex,
                                             0, &requeue_futex, 0u, ZX_HANDLE_INVALID);
    }
};

static bool BasicGetOwnerTest() {
    BEGIN_TEST;
    fbl::futex_t the_futex(0);

    // No one should own our brand new futex right now.
    zx_status_t res;
    zx_koid_t koid = ~ZX_KOID_INVALID;
    res = zx_futex_get_owner(&the_futex, &koid);
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, ZX_KOID_INVALID);

    // Passing a bad pointer for koid is an error.
    res = zx_futex_get_owner(&the_futex, nullptr);
    ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);

    // Passing a misaligned pointer for the futex is an error.
    res = zx_futex_get_owner(
            reinterpret_cast<zx_futex_t*>(reinterpret_cast<uintptr_t>(&the_futex) + 1),
            &koid);
    ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);

    // Passing a null pointer for the futex is an error.
    res = zx_futex_get_owner(nullptr, &koid);
    ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);

    END_TEST;
}

static bool WaitOwnershipTest() {
    BEGIN_TEST;

    fbl::futex_t the_futex(0);
    ExternalThread external;
    Thread thread1, thread2, thread3;
    Event wake_thread3;
    zx_status_t res;
    std::atomic<zx_status_t> t1_res, t2_res, t3_res;

    zx_handle_t test_thread_handle = zx_thread_self();
    zx_koid_t test_thread_koid = CurrentThreadKoid();
    zx_koid_t koid;

    // If things go wrong, and we bail out early, do out best to shut down all
    // of the threads we may have started before unwinding our stack state out
    // from under them.
    auto cleanup = fbl::MakeAutoCall([&]() {
        zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
        wake_thread3.Signal();
        external.Stop();
        thread1.Stop();
        thread2.Stop();
        thread3.Stop();
    });

    // Attempt to fetch the owner of the futex.  It should be no-one right now.
    res = zx_futex_get_owner(&the_futex, &koid);
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, ZX_KOID_INVALID);

    // Start a thread and have it declare us to be the owner of the futex.
    koid = ~ZX_KOID_INVALID;
    t1_res.store(ZX_ERR_INTERNAL);
    ASSERT_TRUE(thread1.Start("thread_1", [&]() -> int {
        t1_res.store(zx_futex_wait(&the_futex, 0, test_thread_handle, ZX_TIME_INFINITE));
        return 0;
    }));
    ASSERT_TRUE(WaitFor(ZX_MSEC(1000), [&]() -> bool {
        res = zx_futex_get_owner(&the_futex, &koid);
        // Stop waiting if we fail to fetch the owner, or if the koid matches what we expect.
        return ((res != ZX_OK) || (koid == test_thread_koid));
    }));
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, test_thread_koid);
    ASSERT_EQ(t1_res.load(), ZX_ERR_INTERNAL);  // thread1 is still waiting.

    // Start another thread and have it fail to set the futex owner to no one because of
    // an expected futex value mismatch.
    t2_res.store(ZX_ERR_INTERNAL);
    ASSERT_TRUE(thread2.Start("thread_2.0", [&]() -> int {
        t2_res.store(zx_futex_wait(&the_futex, 1, ZX_HANDLE_INVALID, ZX_TIME_INFINITE));
        return 0;
    }));
    ASSERT_EQ(thread2.Stop(), ZX_OK);

    // The futex owner should not have changed.
    res = zx_futex_get_owner(&the_futex, &koid);
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, test_thread_koid);
    ASSERT_EQ(t2_res.load(), ZX_ERR_BAD_STATE);

    // Start a thread and attempt to set the futex owner to the thread doing the
    // wait (thread2).  This should fail.
    t2_res.store(ZX_ERR_INTERNAL);
    ASSERT_TRUE(thread2.Start("thread_2.1", [&]() -> int {
        t2_res.store(zx_futex_wait(&the_futex, 0, thread2.handle().get(), ZX_TIME_INFINITE));
        return 0;
    }));
    ASSERT_EQ(thread2.Stop(), ZX_OK);

    // The futex owner should not have changed.
    res = zx_futex_get_owner(&the_futex, &koid);
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, test_thread_koid);
    ASSERT_EQ(t2_res.load(), ZX_ERR_INVALID_ARGS);

    // Start a thread and attempt to set the futex owner to the thread which is
    // already waiting (thread1).  This should fail.
    t2_res.store(ZX_ERR_INTERNAL);
    ASSERT_TRUE(thread2.Start("thread_2.2", [&]() -> int {
        t2_res.store(zx_futex_wait(&the_futex, 0, thread1.handle().get(), ZX_TIME_INFINITE));
        return 0;
    }));
    ASSERT_EQ(thread2.Stop(), ZX_OK);

    // The futex owner should not have changed.
    res = zx_futex_get_owner(&the_futex, &koid);
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, test_thread_koid);
    ASSERT_EQ(t2_res.load(), ZX_ERR_INVALID_ARGS);

    // Start a thread and attempt to set the futex owner to a handle which is valid, but is not
    // actually a thread.
    zx::event not_a_thread;
    res = zx::event::create(0, &not_a_thread);
    ASSERT_EQ(res, ZX_OK);

    t2_res.store(ZX_ERR_INTERNAL);
    ASSERT_TRUE(thread2.Start("thread_2.3", [&]() -> int {
        t2_res.store(zx_futex_wait(&the_futex, 0, not_a_thread.get(), ZX_TIME_INFINITE));
        return 0;
    }));
    ASSERT_EQ(thread2.Stop(), ZX_OK);

    // The futex owner should not have changed.
    res = zx_futex_get_owner(&the_futex, &koid);
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, test_thread_koid);
    ASSERT_EQ(t2_res.load(), ZX_ERR_WRONG_TYPE);

    // Start a thread and attempt to set the futex owner to the handle to a thread in another
    // process.
    ASSERT_TRUE(external.Start());
    t2_res.store(ZX_ERR_INTERNAL);
    ASSERT_TRUE(thread2.Start("thread_2.4", [&]() -> int {
        t2_res.store(zx_futex_wait(&the_futex, 0, external.thread().get(), ZX_TIME_INFINITE));
        return 0;
    }));
    ASSERT_EQ(thread2.Stop(), ZX_OK);
    external.Stop();

    res = zx_futex_get_owner(&the_futex, &koid);
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, test_thread_koid);
    ASSERT_EQ(t2_res.load(), ZX_ERR_INVALID_ARGS);

    // Start thread3, just so we have a different owner to assign.  Then start
    // up thread2 and have it declare thread3 to be the new owner of the futex,
    // and finally timeout.  Verify that the ownership changes properly, and
    // that it does not change when thread2 times out.
    t3_res.store(ZX_ERR_INTERNAL);
    ASSERT_TRUE(thread3.Start("thread_3", [&]() -> int {
        t3_res.store(wake_thread3.Wait(ZX_SEC(5)));
        return 0;
    }));

    t2_res.store(ZX_ERR_INTERNAL);
    ASSERT_TRUE(thread2.Start("thread_2.5", [&]() -> int {
        t2_res.store(zx_futex_wait(&the_futex, 0, thread3.handle().get(),
                                   zx_deadline_after(ZX_MSEC(10))));
        return 0;
    }));

    ASSERT_TRUE(WaitFor(ZX_MSEC(1000), [&]() -> bool {
        res = zx_futex_get_owner(&the_futex, &koid);
        // Stop waiting if we fail to fetch the owner, or if the koid matches what we expect.
        return ((res != ZX_OK) || (koid == thread3.koid()));
    }));
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, thread3.koid());

    ASSERT_EQ(thread2.Stop(), ZX_OK);
    res = zx_futex_get_owner(&the_futex, &koid);
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, thread3.koid());
    ASSERT_EQ(t2_res.load(), ZX_ERR_TIMED_OUT);

    // Finally, start second thread and have it succeed in waiting, setting
    // the owner of the futex to nothing in the process.
    t2_res.store(ZX_ERR_INTERNAL);
    ASSERT_TRUE(thread2.Start("thread_2.6", [&]() -> int {
        t2_res.store(zx_futex_wait(&the_futex, 0, ZX_HANDLE_INVALID, ZX_TIME_INFINITE));
        return 0;
    }));
    ASSERT_TRUE(WaitFor(ZX_MSEC(1000), [&]() -> bool {
        res = zx_futex_get_owner(&the_futex, &koid);
        // Stop waiting if we fail to fetch the owner, or if the koid matches what we expect.
        return ((res != ZX_OK) || (koid == ZX_KOID_INVALID));
    }));
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, ZX_KOID_INVALID);

    // Wakeup all of the threads and join
    res = zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
    wake_thread3.Signal();
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(thread1.Stop(), ZX_OK);
    ASSERT_EQ(thread2.Stop(), ZX_OK);
    ASSERT_EQ(thread3.Stop(), ZX_OK);
    ASSERT_EQ(t1_res.load(), ZX_OK);
    ASSERT_EQ(t2_res.load(), ZX_OK);
    ASSERT_EQ(t3_res.load(), ZX_OK);

    cleanup.cancel();
    END_TEST;
}

template <OpType OPERATION>
static bool WakeOwnershipTest() {
    BEGIN_TEST;

    using do_op = WakeOperation<OPERATION>;

    fbl::futex_t the_futex(0);
    zx_handle_t test_thread_handle = zx_thread_self();
    zx_koid_t test_thread_koid = CurrentThreadKoid();
    zx_koid_t koid;
    zx_status_t res;

    struct WaiterState {
        Thread thread;
        std::atomic<zx_status_t> res;
        bool woken;
    } WAITERS[8];

    // If things go wrong, and we bail out early, do out best to shut down all
    // of the threads we may have started before unwinding our stack state out
    // from under them.
    auto cleanup = fbl::MakeAutoCall([&]() {
        zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
        for (auto& waiter : WAITERS) {
            waiter.thread.Stop();
        }
    });

    // Run this test 2 times.  The first time, use a traditional wake which
    // should always set the futex to "unowned".  The second time, use the
    // wake_single_owner variant which should assign ownership to the thread
    // which was woken.
    for (uint32_t pass = 0; pass < 2; ++pass) {
        // Start a bunch of threads and have them all declare us to be
        // the_futex's owner.
        for (auto& waiter : WAITERS) {
            waiter.res.store(ZX_ERR_INTERNAL);
            waiter.woken = false;
            ASSERT_TRUE(waiter.thread.Start("wake_test_waiter",
                [&waiter, &the_futex, test_thread_handle]() -> int {
                    waiter.res.store(zx_futex_wait(&the_futex, 0,
                                                   test_thread_handle,
                                                   ZX_TIME_INFINITE));
                    return 0;
                }));
        }

        // Wait until all of the threads are blocked.
        res = ZX_ERR_INTERNAL;
        ASSERT_TRUE(WaitFor(ZX_MSEC(1000), [&WAITERS, &res]() -> bool {
            for (const auto& waiter : WAITERS) {
                // If we fail to fetch thread state, stop waiting.
                uint32_t state;
                res = waiter.thread.GetRunState(&state);
                if (res != ZX_OK) {
                    return true;
                }

                // If this thread is not blocked yet, keep waiting.
                if (state != ZX_THREAD_STATE_BLOCKED_FUTEX) {
                    return false;
                }

                // If this thread is blocked, but is not in the RUNNING state,
                // then it is blocked on the wrong futex (in this case, the
                // Thread's stop_event's futex).  Stop waiting and report the
                // error.
                if (waiter.thread.state() != Thread::State::RUNNING) {
                    res = ZX_ERR_BAD_STATE;
                    return true;
                }
            }

            // All threads are blocked, we are finished.
            return true;
        }));
        ASSERT_EQ(res, ZX_OK);

        // We should currently be the owner of the futex.
        res = zx_futex_get_owner(&the_futex, &koid);
        ASSERT_EQ(res, ZX_OK);
        ASSERT_EQ(koid, test_thread_koid);

        // If we are testing the wake behavior of zx_futex_requeue_*, then make
        // sure that attempting to do a wake op when the wake-futex value verification
        // fails does nothing to change the ownership of the futex.
        if constexpr (OPERATION == OpType::kRequeue) {
            fbl::futex_t requeue_futex(1);
            if (pass == 0) {
                res = zx_futex_requeue(&the_futex, 1u,
                                       1, &requeue_futex, 0u, ZX_HANDLE_INVALID);
            } else {
                res = zx_futex_requeue_single_owner(&the_futex,
                                                    1, &requeue_futex,
                                                    0u, ZX_HANDLE_INVALID);
            }
            ASSERT_EQ(res, ZX_ERR_BAD_STATE);

            // We should still be the owner of the futex.
            res = zx_futex_get_owner(&the_futex, &koid);
            ASSERT_EQ(res, ZX_OK);
            ASSERT_EQ(koid, test_thread_koid);

            // All waiters should still be blocked on our futex.
            for (const auto& waiter : WAITERS) {
                uint32_t state;
                res = waiter.thread.GetRunState(&state);
                ASSERT_EQ(res, ZX_OK);
                ASSERT_EQ(state, ZX_THREAD_STATE_BLOCKED_FUTEX);
            }
        }

        // Now wake all of the threads.  We don't know or have any guarantee as
        // to which thread the kernel is going to choose to wake, so we cannot
        // make any assumptions here, just that some thread will be woken.
        //
        // ++ Pass 0 validation uses the traditonal wake and should result in no
        //    owner.
        // ++ Pass 1 validation uses wake_single_owner and should assign
        //    ownership to the thread which was woken, until the last thread is
        //    woken (at which point, there should be no owner as there are no
        //    waiters).
        //
        for (uint32_t i = 0; i < countof(WAITERS); ++i) {
            if (!pass) {
                // Wake a thread.
                res = do_op::wake(the_futex, 1u);
            } else {
                res = do_op::wake_single_owner(the_futex);
            }
            ASSERT_EQ(res, ZX_OK);

            // Wait until at least one thread has finished its lambda, which we
            // have not noticed before.
            WaiterState* woken_waiter = nullptr;
            res = ZX_ERR_INTERNAL;

            ASSERT_TRUE(WaitFor(ZX_MSEC(1000), [&WAITERS, &woken_waiter]() -> bool {
                for (auto& waiter : WAITERS) {
                    if (!waiter.woken) {
                        if (waiter.thread.state() == Thread::State::WAITING_TO_STOP) {
                            waiter.woken = true;
                            woken_waiter = &waiter;
                            return true;
                        }
                    }
                }

                return false;
            }));

            ASSERT_NONNULL(woken_waiter);
            ASSERT_EQ(woken_waiter->res.load(), ZX_OK);

            // Now check to be sure that ownership was updated properly.  It
            // should be INVALID if this is pass 0, or if we just woke up the
            // last thread.
            zx_koid_t expected_koid = (!pass || ((i + 1) == countof(WAITERS)))
                                    ? ZX_KOID_INVALID
                                    : woken_waiter->thread.koid();

            res = zx_futex_get_owner(&the_futex, &koid);

            ASSERT_EQ(res, ZX_OK);
            ASSERT_EQ(koid, expected_koid);

            // Recycle our thread for the next pass.
            ASSERT_EQ(woken_waiter->thread.Stop(), ZX_OK);
        }
    }

    cleanup.cancel();
    END_TEST;
}

template <OpType OPERATION>
static bool WakeZeroOwnershipTest() {
    BEGIN_TEST;

    using do_op = WakeOperation<OPERATION>;

    fbl::futex_t the_futex(0);
    zx_status_t res;
    Thread thread1;
    std::atomic<zx_status_t> t1_res;

    zx_handle_t test_thread_handle = zx_thread_self();
    zx_koid_t test_thread_koid = CurrentThreadKoid();
    zx_koid_t koid;
    uint32_t state;

    // If things go wrong, and we bail out early, do out best to shut down all
    // of the threads we may have started before unwinding our stack state out
    // from under them.
    auto cleanup = fbl::MakeAutoCall([&]() {
        zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
        thread1.Stop();
    });

    // Start a thread and have it declare us to be the owner of the futex.
    koid = ~ZX_KOID_INVALID;
    t1_res.store(ZX_ERR_INTERNAL);
    ASSERT_TRUE(thread1.Start("thread_1", [&]() -> int {
        t1_res.store(zx_futex_wait(&the_futex, 0, test_thread_handle, ZX_TIME_INFINITE));
        return 0;
    }));

    // Wait until the thread has become blocked on the futex
    ASSERT_TRUE(WaitFor(ZX_MSEC(1000), [&]() -> bool {
        res = thread1.GetRunState(&state);
        // Stop waiting if we fail to fetch the run state, or the thread has
        // reached our desired state.
        return ((res != ZX_OK) || (state == ZX_THREAD_STATE_BLOCKED_FUTEX));
    }));
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(state, ZX_THREAD_STATE_BLOCKED_FUTEX);

    // We should now be the owner of the futex
    res = zx_futex_get_owner(&the_futex, &koid);
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, test_thread_koid);
    ASSERT_EQ(t1_res.load(), ZX_ERR_INTERNAL);  // thread1 is still waiting.

    // Attempt to wake zero threads.  This should succeed, thread1 should still
    // blocked on the futex, and the owner of the futex should now be no one.
    res = do_op::wake(the_futex, 0);
    ASSERT_EQ(res, ZX_OK);

    // Wait up to 100mSec for the thread to unblock.  If it is still blocked on
    // the futex after 100mSec, then assume that it is going to remain blocked.
    //
    // TODO(johngro): Look into changing the need for this.  The issue here is
    // that the run state of user mode threads is tracked using a helper class
    // in ThreadDispatcher called "AutoBlocked".  When a thread blocks on a
    // futex (for example), it puts an AutoBlocked(BY_FUTEX) on its local stack,
    // joins a wait queue, and is suspended.  When it resumes and the AutoBlock
    // destructor runs, it restores the thread's previous run state.
    //
    // Because of this, when Thread A wakes Thread B from a futex wait queue,
    // the user-mode run state state of thread B is not updated atomically as
    // the thread is removed from the wait queue by thread A.  If it takes a bit
    // of time for thread B to be scheduled again (and run the AutoBlocked
    // destructor), then it will appear to be blocked by a futex still, even
    // though the thread is actually run-able.  Failure to wait for a little bit
    // here can lead to a flaky test (esp. under qemu).
    //
    // Still, as long as this state is not atomically updated by the wake
    // operation, the test is always has the potential to flaky, which is why
    // the TODO.
    ASSERT_FALSE(WaitFor(ZX_MSEC(100), [&]() -> bool {
        res = thread1.GetRunState(&state);
        return ((res != ZX_OK) || (state != ZX_THREAD_STATE_BLOCKED_FUTEX));
    }));
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(state, ZX_THREAD_STATE_BLOCKED_FUTEX);

    res = zx_futex_get_owner(&the_futex, &koid);
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, ZX_KOID_INVALID);

    // Finished.  Wake up the thread and shut down.
    res = zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(thread1.Stop(), ZX_OK);
    ASSERT_EQ(t1_res.load(), ZX_OK);

    cleanup.cancel();
    END_TEST;
}

static bool RequeueOwnershipTest() {
    BEGIN_TEST;

    fbl::futex_t wake_futex(0);
    fbl::futex_t requeue_futex(1);
    ExternalThread external;
    zx::event not_a_thread;
    zx_handle_t test_thread_handle = zx_thread_self();
    zx_koid_t test_thread_koid = CurrentThreadKoid();
    zx_koid_t koid;
    zx_status_t res;

    struct WaiterState {
        Thread thread;
        std::atomic<zx_status_t> res;
        bool woken;
    } WAITERS[8];

    // If things go wrong, and we bail out early, do out best to shut down all
    // of the threads we may have started before unwinding our stack state out
    // from under them.
    auto cleanup = fbl::MakeAutoCall([&]() {
        zx_futex_wake(&wake_futex, std::numeric_limits<uint32_t>::max());
        zx_futex_wake(&requeue_futex, std::numeric_limits<uint32_t>::max());
        external.Stop();
        for (auto& waiter : WAITERS) {
            waiter.thread.Stop();
        }
    });

    // Start a bunch of threads and have them all declare us to be
    // the_futex's owner.
    for (auto& waiter : WAITERS) {
        waiter.res.store(ZX_ERR_INTERNAL);
        waiter.woken = false;
        ASSERT_TRUE(waiter.thread.Start("requeue_test_waiter",
            [&waiter, &wake_futex, test_thread_handle]() -> int {
                waiter.res.store(zx_futex_wait(&wake_futex, 0,
                                               test_thread_handle,
                                               ZX_TIME_INFINITE));
                return 0;
            }));
    }

    // Wait until all of the threads are blocked.
    res = ZX_ERR_INTERNAL;
    ASSERT_TRUE(WaitFor(ZX_MSEC(1000), [&WAITERS, &res]() -> bool {
        for (const auto& waiter : WAITERS) {
            // If we fail to fetch thread state, stop waiting.
            uint32_t state;
            res = waiter.thread.GetRunState(&state);
            if (res != ZX_OK) {
                return true;
            }

            // If this thread is not blocked yet, keep waiting.
            if (state != ZX_THREAD_STATE_BLOCKED_FUTEX) {
                return false;
            }
        }

        // All threads are blocked, we are finished.
        return true;
    }));
    ASSERT_EQ(res, ZX_OK);

    // Create a valid handle which is not a thread.  We will need it to make
    // sure that it is illegal to set the requeue target to something which is a
    // valid handle, but not a thread.
    res = zx::event::create(0, &not_a_thread);
    ASSERT_EQ(res, ZX_OK);

    // Start a thread in another process.  We will need one to make sure that we
    // are not allowed to change the owner of the requeue futex to a thread from
    // a another process.
    ASSERT_TRUE(external.Start());

    // A small helper lambda we use to reduce the boilerplate state checks we
    // are about to do a number of times.
    auto VerifyState = [&](zx_koid_t expected_wake_owner,
                           zx_koid_t expected_requeue_owner) -> bool {
        BEGIN_HELPER;
        zx_koid_t koid;
        zx_status_t res;

        // Check the owners.
        res = zx_futex_get_owner(&wake_futex, &koid);
        ASSERT_EQ(res, ZX_OK);
        ASSERT_EQ(koid, expected_wake_owner);

        res = zx_futex_get_owner(&requeue_futex, &koid);
        ASSERT_EQ(res, ZX_OK);
        ASSERT_EQ(koid, expected_requeue_owner);

        // Check each of the waiters.
        for (const auto& waiter : WAITERS) {
            uint32_t state;
            res = waiter.thread.GetRunState(&state);
            ASSERT_EQ(res, ZX_OK);

            if (!waiter.woken) {
                ASSERT_EQ(state, ZX_THREAD_STATE_BLOCKED_FUTEX);
            }
        }

        END_HELPER;
    };

    // OK, basic setup is complete.  We should be the owner of the wait futex, no one
    // should own the requeue futex, and all threads should be blocked waiting
    // on the wait futex (although, at this point in the test, we can only check
    // to be sure that the are all blocked by a futex... we don't know which
    // one).
    ASSERT_TRUE(VerifyState(test_thread_koid, ZX_KOID_INVALID));

    // Wake a single thread assigning ownership of the wake thread to it in the
    // process, and requeue a single thread from the wake futex to the requeue
    // futex (we have no good way to know which one gets requeued, just that it
    // has been).  Assign ownership of the requeue futex to ourselves in the
    // process.
    res = zx_futex_requeue_single_owner(&wake_futex, 0,
                                        &requeue_futex, 1, test_thread_handle);
    ASSERT_EQ(res, ZX_OK);

    // Find the thread we just woke up.
    const WaiterState* woken_waiter = nullptr;
    res = zx_futex_get_owner(&wake_futex, &koid);
    ASSERT_EQ(res, ZX_OK);
    ASSERT_NE(koid, ZX_KOID_INVALID);
    ASSERT_NE(koid, test_thread_koid);
    for (auto& waiter : WAITERS) {
        if (!waiter.woken && (waiter.thread.koid() == koid)) {
            waiter.woken = true;
            woken_waiter = &waiter;
        }
    }
    ASSERT_NONNULL(woken_waiter);

    // Wait until it has finished its lambda and waiting for our permission to stop.
    ASSERT_TRUE(WaitFor(ZX_MSEC(1000), [woken_waiter]() -> bool {
        return (woken_waiter->thread.state() == Thread::State::WAITING_TO_STOP);
    }));

    zx_koid_t woken_thread_koid = woken_waiter->thread.koid();
    ASSERT_TRUE(VerifyState(woken_thread_koid, test_thread_koid));

    // Next, start a sequence of failure tests.  In each of the tests, attempt
    // to wake no threads, but requeue a single thread declaring the owner of
    // the requeue futex to be no one.
    //
    // After each of these tests, nothing should have changed.  We should own
    // the requeue futex, the thread which was woken during setup should own the
    // wake futex, and all of our threads (except the woken thread) should be
    // blocked on a futex (we just don't know which one).
    //
    // Failure Test #1:
    // It is illegal to specify either nullptr or a misaligned futex for the
    // wake futex.
    //
    res = zx_futex_requeue(nullptr, 1u, 0, &requeue_futex, 1, ZX_HANDLE_INVALID);
    ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
    ASSERT_TRUE(VerifyState(woken_thread_koid, test_thread_koid));

    const zx_futex_t* misaligned_wake_futex = reinterpret_cast<const zx_futex_t*>(
            reinterpret_cast<uintptr_t>(&wake_futex) + 1);
    res = zx_futex_requeue(misaligned_wake_futex, 1u, 0,
                           &requeue_futex, 1, ZX_HANDLE_INVALID);
    ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
    ASSERT_TRUE(VerifyState(woken_thread_koid, test_thread_koid));

    // Failure Test #2:
    // It is illegal to specify either nullptr or a misaligned futex for the
    // requeue futex.
    //
    res = zx_futex_requeue(&wake_futex, 1u, 0, nullptr, 1, ZX_HANDLE_INVALID);
    ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
    ASSERT_TRUE(VerifyState(woken_thread_koid, test_thread_koid));

    const zx_futex_t* misaligned_requeue_futex = reinterpret_cast<const zx_futex_t*>(
            reinterpret_cast<uintptr_t>(&requeue_futex) + 1);
    res = zx_futex_requeue(&wake_futex, 1u, 0,
                           misaligned_requeue_futex, 1, ZX_HANDLE_INVALID);
    ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
    ASSERT_TRUE(VerifyState(woken_thread_koid, test_thread_koid));

    // Failure Test #3:
    // It is illegal to use the same futex for both wake and requeue.
    //
    res = zx_futex_requeue(&wake_futex, 1u, 0,
                           &wake_futex, 1, ZX_HANDLE_INVALID);
    ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
    ASSERT_TRUE(VerifyState(woken_thread_koid, test_thread_koid));

    // Failure Test #4:
    // It is illegal to use an invalid handle value as the new requeue owner
    // which is not ZX_HANDLE_INVALID
    //
    res = zx_futex_requeue(&wake_futex, 1u, 0,
                           &requeue_futex, 1, static_cast<zx_handle_t>(1));
    ASSERT_EQ(res, ZX_ERR_BAD_HANDLE);
    ASSERT_TRUE(VerifyState(woken_thread_koid, test_thread_koid));

    // Failure Test #5:
    // It is illegal to use an valid handle value which is not a thread.
    //
    res = zx_futex_requeue(&wake_futex, 1u, 0,
                           &requeue_futex, 1, not_a_thread.get());
    ASSERT_EQ(res, ZX_ERR_WRONG_TYPE);
    ASSERT_TRUE(VerifyState(woken_thread_koid, test_thread_koid));

    // Failure Test #6:
    // It is illegal to use an valid thread handle handle from another process.
    //
    res = zx_futex_requeue(&wake_futex, 1u, 0,
                           &requeue_futex, 1, external.thread().get());
    ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
    ASSERT_TRUE(VerifyState(woken_thread_koid, test_thread_koid));

    // We don't need our external process anymore.
    external.Stop();

    // Failure Test #7:
    // It is illegal to a thread currently in waiting in either the wait queue
    // or the requeue queue.  We don't really know which thread is which at this
    // point in time, but trying them all should cover both cases.
    //
    for (const auto& waiter : WAITERS) {
        if (waiter.woken) { continue; }
        res = zx_futex_requeue(&wake_futex, 1u, 0,
                               &requeue_futex, 1, waiter.thread.handle().get());
        ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
        ASSERT_TRUE(VerifyState(woken_thread_koid, test_thread_koid));
    }

    // Failure Test #8:
    // Nothing should change if we fail to validate the wake futex state.
    //
    res = zx_futex_requeue(&wake_futex, 1u, 1,
                           &requeue_futex, 1, ZX_HANDLE_INVALID);
    ASSERT_EQ(res, ZX_ERR_BAD_STATE);
    ASSERT_TRUE(VerifyState(woken_thread_koid, test_thread_koid));

    // Time for success tests.
    //
    // During setup, we woke exactly one thread from the wake futex, and
    // requeued exactly one thread from the wake to the requeue futex.  So we
    // should have 1 thread ready to stop, 1 thread blocked on the requeue
    // futex, and the rest of the threads blocked on the wake futex.
    //
    // Verify that exactly one thread was waiting in the requeue futex by waking
    // everyone on the requeue_futex and waiting a little bit to see who becomes
    // blocked on the exit event.
    //
    // Note: See TODO above about possibly eliminating the need to perform this
    // arbitrary wait.
    auto CountJustWoken = [&WAITERS](bool* timed_out) -> uint32_t {
        uint32_t just_woken = 0;
        *timed_out = !WaitFor(ZX_MSEC(100), [&]() -> bool {
            for (auto& waiter : WAITERS) {
                if (!waiter.woken) {
                    if (waiter.thread.state() == Thread::State::WAITING_TO_STOP) {
                        ++just_woken;
                        waiter.woken = true;
                    }
                }
            }
            return false;
        });
        return just_woken;
    };

    res = zx_futex_wake(&requeue_futex, std::numeric_limits<uint32_t>::max());
    ASSERT_EQ(res, ZX_OK);

    bool timed_out;
    uint32_t just_woken;
    just_woken = CountJustWoken(&timed_out);
    ASSERT_TRUE(timed_out);
    ASSERT_EQ(just_woken, 1u);
    ASSERT_TRUE(VerifyState(woken_thread_koid, ZX_KOID_INVALID));

    // Now requeue exactly two threads, setting the owner to the thread that we
    // originally woke up in the process.
    res = zx_futex_requeue(&wake_futex, 0u, 0,
                           &requeue_futex, 2, woken_waiter->thread.handle().get());
    ASSERT_EQ(res, ZX_OK);
    ASSERT_TRUE(VerifyState(ZX_KOID_INVALID, woken_thread_koid));

    res = zx_futex_wake(&requeue_futex, std::numeric_limits<uint32_t>::max());
    ASSERT_EQ(res, ZX_OK);

    just_woken = CountJustWoken(&timed_out);
    ASSERT_TRUE(timed_out);
    ASSERT_EQ(just_woken, 2u);
    ASSERT_TRUE(VerifyState(ZX_KOID_INVALID, ZX_KOID_INVALID));

    // Finally, requeue the rest of the threads, setting ownership of the
    // requeue futex back to ourselves in the process.
    res = zx_futex_requeue(&wake_futex, 0u, 0,
                           &requeue_futex,
                           std::numeric_limits<uint32_t>::max(),
                           test_thread_handle);
    ASSERT_TRUE(VerifyState(ZX_KOID_INVALID, test_thread_koid));

    // Verify that all threads were requeued by waking up everyone on the
    // requeue futex, and stopping threads.
    res = zx_futex_wake(&requeue_futex, std::numeric_limits<uint32_t>::max());
    ASSERT_EQ(res, ZX_OK);
    for (auto& waiter : WAITERS) {
        ASSERT_EQ(waiter.thread.Stop(), ZX_OK);
        waiter.woken = true;
        ASSERT_EQ(waiter.res.load(), ZX_OK);
    }

    // Success!
    cleanup.cancel();
    END_TEST;
}

static bool OwnerExitTest() {
    BEGIN_TEST;

    fbl::futex_t the_futex(0);
    Thread the_owner;
    Thread the_waiter;
    std::atomic<zx_status_t> waiter_res;
    zx_status_t res;

    // If things go wrong, and we bail out early, do out best to shut down all
    // of the threads.
    auto cleanup = fbl::MakeAutoCall([&]() {
        zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
        the_owner.Stop();
        the_waiter.Stop();
    });

    // Start the "owner" thread.  Have it do nothing at all.  It will end up
    // blocking on an internal signal, waiting for us to tell it to stop.
    ASSERT_TRUE(the_owner.Start("OwnerExitTest owner", []() -> int { return 0; }));

    // Start the "waiter" thread.  Have it wait on the futex, and declare the
    // owner thread to be the owner of the_futex.
    waiter_res.store(ZX_ERR_INTERNAL);
    ASSERT_TRUE(the_waiter.Start("OwnerExitTest waiter",
        [&waiter_res, &the_futex, test_thread_handle = the_owner.handle().get()]() -> int {
            waiter_res.store(zx_futex_wait(&the_futex, 0,
                                           test_thread_handle,
                                           ZX_TIME_INFINITE));
            return 0;
        }));

    // Wait until our waiter has become blocked by the futex.
    ASSERT_TRUE(WaitFor(ZX_MSEC(1000), [&the_waiter, &res]() -> bool {
        // If we fail to fetch thread state, stop waiting.
        uint32_t state;
        res = the_waiter.GetRunState(&state);
        if (res != ZX_OK) {
            return true;
        }

        // We are done if the thread has reached the BLOCKED_FUTEX state
        return (state == ZX_THREAD_STATE_BLOCKED_FUTEX);
    }));
    ASSERT_EQ(res, ZX_OK);

    // Verify that our futex is owned by our owner thread.
    zx_koid_t koid = ~ZX_KOID_INVALID;
    res = zx_futex_get_owner(&the_futex, &koid);
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, the_owner.koid());

    // OK, now let the owner thread exit.  Ownership of the futex should become
    // automatically released.
    ASSERT_EQ(the_owner.Stop(), ZX_OK);
    koid = ~ZX_KOID_INVALID;
    res = zx_futex_get_owner(&the_futex, &koid);
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(koid, ZX_KOID_INVALID);

    // Release our waiter thread and shut down.
    res = zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
    ASSERT_EQ(res, ZX_OK);
    ASSERT_EQ(the_waiter.Stop(), ZX_OK);
    ASSERT_EQ(waiter_res.load(), ZX_OK);

    cleanup.cancel();
    END_TEST;
}

}  // anon namespace

BEGIN_TEST_CASE(futex_ownership_tests)
RUN_TEST(BasicGetOwnerTest);
RUN_TEST(WaitOwnershipTest);
RUN_TEST(WakeOwnershipTest<OpType::kStandard>);
RUN_TEST(WakeOwnershipTest<OpType::kRequeue>);
RUN_TEST(WakeZeroOwnershipTest<OpType::kStandard>);
RUN_TEST(WakeZeroOwnershipTest<OpType::kRequeue>);
RUN_TEST(RequeueOwnershipTest);
// TODO(johngro): Re-enable this test once the root cause of FLK-153 has been
// tracked down and squashed.
#if 0
RUN_TEST(OwnerExitTest);
#endif
END_TEST_CASE(futex_ownership_tests)
