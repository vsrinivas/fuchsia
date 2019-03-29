// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <fbl/auto_call.h>
#include <fbl/futex.h>
#include <inttypes.h>
#include <lib/zx/suspend_token.h>
#include <lib/zx/thread.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>
#include <zircon/time.h>
#include <zircon/types.h>

static constexpr zx::duration DEFAULT_TIMEOUT = zx::sec(5);
static constexpr zx::duration DEFAULT_POLL_INTERVAL = zx::usec(100);

// Poll until the user provided Callable |should_stop| tells us to stop by
// returning true.
template <typename Callable>
static zx_status_t WaitFor(const Callable& should_stop,
                           zx::duration timeout = DEFAULT_TIMEOUT,
                           zx::duration poll_interval = DEFAULT_POLL_INTERVAL) {
    static_assert(std::is_same_v<decltype(should_stop()), bool>,
                  "should_stop() must return a bool!");

    zx::time deadline = zx::deadline_after(timeout);
    zx::time now;

    while ((now = zx::clock::get_monotonic()) < deadline) {
        if (should_stop()) {
            return ZX_OK;
        }

        zx::nanosleep(zx::deadline_after(DEFAULT_POLL_INTERVAL));
    }

    return ZX_ERR_TIMED_OUT;
}

// This starts a thread which waits on a futex.  We can do futex_wake()
// operations and then test whether or not this thread has been woken up.
class TestThread {
public:
    TestThread() = default;
    ~TestThread() { Shutdown(); }

    TestThread(const TestThread &) = delete;
    TestThread& operator=(const TestThread &) = delete;

    static bool AssertWokeThreadCount(const TestThread threads[],
                                      uint32_t total_thread_count,
                                      uint32_t target_woke_count) {
        BEGIN_HELPER;

        ASSERT_LE(target_woke_count, total_thread_count);

        auto CountWoken = [&threads, total_thread_count]() -> uint32_t {
            uint32_t ret = 0;
            for (uint32_t i = 0; i < total_thread_count; ++i) {
                if (threads[i].state() == State::WAIT_RETURNED) {
                    ++ret;
                }
            }
            return ret;
        };

        // Wait forever until we achieve the target count.  If threads are not
        // waking up as they should, the test framework should eventually kill
        // us.
        uint32_t woken;
        do {
            woken = CountWoken();
        } while (woken < target_woke_count);

        ASSERT_EQ(CountWoken(), target_woke_count);

        // Wait an arbitrary amount of time to be sure that no one else wakes
        // up.
        //
        // TODO(johngro) : It would be really nice if we didn't have to have an
        // arbitrary wait here.  Unfortunately, I'm not sure that there is any
        // amount of time that we can wait and prove that a thread might not
        // spuriously wake up in the future.
        zx::nanosleep(zx::deadline_after(zx::msec(300)));
        ASSERT_EQ(CountWoken(), target_woke_count);

        END_HELPER;
    }

    bool Start(zx_futex_t* futex_addr, zx::duration timeout = zx::duration::infinite()) {
        BEGIN_HELPER;

        ASSERT_FALSE(thread_handle_.is_valid());

        futex_addr_.store(futex_addr);
        timeout_ = timeout;
        wait_result_.store(ZX_ERR_INTERNAL);

        auto ret = thrd_create_with_name(&thread_,
                [](void* ctx) -> int { return reinterpret_cast<TestThread*>(ctx)->ThreadFunc(); },
                this, "wakeup_test_thread");
        ASSERT_EQ(ret, thrd_success, "Error during thread creation");

        // Make a copy of our thread's handle so that we have something to query
        // re: the thread's status, even if the thread exits out from under us
        // (which will invalidate the handled returned by thrd_get_zx_handle
        zx::unowned_thread(thrd_get_zx_handle(thread_))->duplicate(ZX_RIGHT_SAME_RIGHTS,
                                                                   &thread_handle_);

        EXPECT_EQ(WaitFor([this]() { return state() != State::WAITING_TO_START; }), ZX_OK);

        // Note that this could fail if futex_wait() gets a spurious wakeup.
        EXPECT_EQ(state(), State::ABOUT_TO_WAIT, "wrong state");

        // We should only do this after state_ is State::ABOUT_TO_WAIT,
        // otherwise it could return when the thread has temporarily
        // blocked on a libc-internal futex.
        EXPECT_TRUE(WaitForKernelState(ZX_THREAD_STATE_BLOCKED_FUTEX));

        // This could also fail if futex_wait() gets a spurious wakeup.
        EXPECT_EQ(state(), State::ABOUT_TO_WAIT, "wrong state");

        END_HELPER;
    }

    bool Shutdown() {
        BEGIN_HELPER;

        if (thread_handle_.is_valid()) {
            zx_status_t res = thread_handle_.wait_one(ZX_THREAD_TERMINATED,
                                                      zx::deadline_after(zx::sec(10)),
                                                      NULL);
            EXPECT_EQ(res, ZX_OK, "Thread did not terminate in a timely fashion!\n");
            if (res == ZX_OK) {
                // If we have already explicitly killed this thread, do not
                // attempt to join it.
                //
                // The ZXR runtime currently depends on the thread thread_t
                // exiting via it's trampoline in order to properly clean up and
                // signal it's join waiters.  Attempting to join a thread which
                // was explicitly killed using a task syscall will result in a
                // hang.
                //
                // Note: This means that if we kill a thread created using the
                // C11 thrd_t routines, then we are fundamentally leaking
                // resources (such as the thread's internal handle, the stack,
                // the thread's copy of the root VMAR handle, and so on).
                // Generally speaking, this is Bad and we should not be doing
                // it.  Unfortunately, ZXR does not provide a way to kill a
                // thread directly, so it is difficult to construct our kill
                // test in a way which says in sync with ZXR.
                //
                // At some point, either we need to figure out a way to keep ZXR
                // in sync with direct kill methods (seems difficult), provide a
                // way to kill and clean up from ZXR, or come back to this code
                // and create threads without using the ZXR routines at all in
                // order to avoid this situation.  Right now, we just deal with
                // the fact that we are leaking resources and that they will be
                // cleaned up when the test exits.
                if (!explicitly_killed_) {
                    EXPECT_EQ(thrd_join(thread_, NULL), thrd_success, "thrd_join failed");
                }
            } else {
                EXPECT_EQ(thread_handle_.kill(), ZX_OK, "Failed to kill unresponsive thread!\n");
            }

            thread_handle_.reset();
        }

        END_HELPER;
    }

    bool GetThreadState(zx_thread_state_t* out_state) const {
        BEGIN_HELPER;

        zx_info_thread_t info;

        ASSERT_NONNULL(out_state);
        ASSERT_TRUE(thread_handle_.is_valid());
        ASSERT_EQ(thread_handle_.get_info(ZX_INFO_THREAD, &info,
                                          sizeof(info), nullptr, nullptr), ZX_OK);
        *out_state = info.state;

        END_HELPER;
    }

    bool WaitForKernelState(zx_thread_state_t target_state,
                            zx::duration timeout = zx::duration::infinite()) {
        BEGIN_HELPER;

        zx_status_t wait_res = ZX_ERR_INTERNAL;
        bool check_res = false;
        zx_thread_state_t state = 0;

        wait_res = WaitFor([&]() {
            check_res = GetThreadState(&state);
            // stop if we have hit the state we want, or we have an error attempting
            // to fetch our kernel thread state.
            return !check_res || (state == target_state);
        });

        EXPECT_EQ(wait_res, ZX_OK);
        EXPECT_TRUE(check_res);
        EXPECT_EQ(state, target_state);

        END_HELPER;
    }

    bool WaitThreadWoken() const {
        BEGIN_HELPER;

        zx_status_t res = WaitFor([this]() { return state() == State::WAIT_RETURNED; });
        EXPECT_EQ(res, ZX_OK);
        EXPECT_EQ(state(), State::WAIT_RETURNED, "wrong state");

        END_HELPER;
    }

    bool WaitThreadInvoluntarilyTerminated() const {
        BEGIN_HELPER;

        ASSERT_TRUE(thread_handle_.is_valid());
        ASSERT_TRUE(explicitly_killed_);

        zx_status_t res = thread_handle_.wait_one(ZX_THREAD_TERMINATED,
                                                  zx::time::infinite(),
                                                  NULL);
        EXPECT_EQ(res, ZX_OK, "failed to wait for thread temination.");
        EXPECT_EQ(state(), State::ABOUT_TO_WAIT);
        EXPECT_EQ(wait_result(), ZX_ERR_INTERNAL);

        END_HELPER;
    }

    bool AssertThreadBlockedOnFutex() const {
        BEGIN_HELPER;
        zx_thread_state_t state;

        ASSERT_TRUE(GetThreadState(&state));
        ASSERT_EQ(state, ZX_THREAD_STATE_BLOCKED_FUTEX);

        END_HELPER;
    }

    bool Kill() {
        BEGIN_HELPER;
        ASSERT_TRUE(thread_handle_.is_valid());
        EXPECT_EQ(thread_handle_.kill(), ZX_OK, "zx_task_kill() failed");
        explicitly_killed_ = true;
        END_HELPER;
    }

    const zx::thread& get_thread_handle() const { return thread_handle_; }
    zx_status_t wait_result() const { return wait_result_.load(); }

private:
    enum class State {
        WAITING_TO_START = 100,
        ABOUT_TO_WAIT = 200,
        WAIT_RETURNED = 300,
    };

    int ThreadFunc() {
        state_.store(State::ABOUT_TO_WAIT);

        zx::time deadline = zx::deadline_after(timeout_);
        wait_result_.store(zx_futex_wait(futex_addr(), *futex_addr(),
                                         ZX_HANDLE_INVALID, deadline.get()));

        state_.store(State::WAIT_RETURNED);
        return 0;
    }

    State state() const { return state_.load(); }
    zx_futex_t* futex_addr() const { return futex_addr_.load(); }

    thrd_t thread_;
    std::atomic<zx_status_t> wait_result_{ ZX_ERR_INTERNAL };
    std::atomic<zx_futex_t*> futex_addr_{ nullptr };
    zx::duration timeout_ = zx::duration::infinite();
    zx::thread thread_handle_;
    bool explicitly_killed_ = false;

    std::atomic<State> state_{ State::WAITING_TO_START };
};

static bool TestFutexWaitValueMismatch() {
    BEGIN_TEST;
    int32_t futex_value = 123;
    zx_status_t rc = zx_futex_wait(&futex_value, futex_value + 1,
                                   ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
    ASSERT_EQ(rc, ZX_ERR_BAD_STATE, "Futex wait should have reurned bad state");
    END_TEST;
}

static bool TestFutexWaitTimeout() {
    BEGIN_TEST;

    int32_t futex_value = 123;
    zx_status_t rc = zx_futex_wait(&futex_value, futex_value, ZX_HANDLE_INVALID, 0);
    ASSERT_EQ(rc, ZX_ERR_TIMED_OUT, "Futex wait should have reurned timeout");

    END_TEST;
}

// This test checks that the timeout in futex_wait() is respected
static bool TestFutexWaitTimeoutElapsed() {
    BEGIN_TEST;

    int32_t futex_value = 0;
    constexpr zx::duration kRelativeDeadline = zx::msec(100);

    for (int i = 0; i < 5; ++i) {
        zx::time deadline = zx::deadline_after(kRelativeDeadline);
        zx_status_t rc = zx_futex_wait(&futex_value, 0, ZX_HANDLE_INVALID, deadline.get());

        ASSERT_EQ(rc, ZX_ERR_TIMED_OUT, "wait should time out");
        EXPECT_GE(zx::clock::get_monotonic().get(), deadline.get(), "wait returned early");
    }

    END_TEST;
}

static bool TestFutexWaitBadAddress() {
    BEGIN_TEST;
    // Check that the wait address is checked for validity.
    zx_status_t rc = zx_futex_wait(nullptr, 123, ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
    ASSERT_EQ(rc, ZX_ERR_INVALID_ARGS, "Futex wait should have reurned invalid_arg");
    END_TEST;
}

// Test that we can wake up a single thread.
bool TestFutexWakeup() {
    BEGIN_TEST;

    fbl::futex_t futex_value(1);
    TestThread thread;

    ASSERT_TRUE(thread.Start(&futex_value));

    // If something goes wrong and we bail out early, do our best to shut down as cleanly as we can.
    auto cleanup = fbl::MakeAutoCall([&]() {
        zx_futex_wake(&futex_value, INT_MAX);
        thread.Shutdown();
    });

    ASSERT_EQ(zx_futex_wake(&futex_value, INT_MAX), ZX_OK);
    ASSERT_TRUE(thread.WaitThreadWoken());
    ASSERT_EQ(thread.wait_result(), ZX_OK);
    ASSERT_TRUE(thread.Shutdown());

    END_TEST;
}

// Test that we can wake up multiple threads, and that futex_wake() heeds
// the wakeup limit.
bool TestFutexWakeupLimit() {
    BEGIN_TEST;

    fbl::futex_t futex_value(1);
    TestThread threads[4];

    // If something goes wrong and we bail out early, do our best to shut down as cleanly as we can.
    auto cleanup = fbl::MakeAutoCall([&]() {
        zx_futex_wake(&futex_value, INT_MAX);
        for (auto& t : threads) {
            t.Shutdown();
        }
    });

    for (auto& t : threads) {
        ASSERT_TRUE(t.Start(&futex_value));
    }

    ASSERT_EQ(zx_futex_wake(&futex_value, 2), ZX_OK);

    // Test that exactly two threads wake up from the queue.  We do not know
    // which threads are going to wake up, just that two threads are going to
    // wake up.
    ASSERT_TRUE(TestThread::AssertWokeThreadCount(threads, countof(threads), 2));

    // Clean up: Wake the remaining threads so that they can exit.
    ASSERT_EQ(zx_futex_wake(&futex_value, INT_MAX), ZX_OK);
    ASSERT_TRUE(TestThread::AssertWokeThreadCount(threads, countof(threads), countof(threads)));

    for (auto& t : threads) {
        ASSERT_EQ(t.wait_result(), ZX_OK);
        ASSERT_TRUE(t.Shutdown());
    }

    cleanup.cancel();
    END_TEST;
}

// Check that futex_wait() and futex_wake() heed their address arguments
// properly.  A futex_wait() call on one address should not be woken by a
// futex_wake() call on another address.
bool TestFutexWakeupAddress() {
    BEGIN_TEST;
    fbl::futex_t futex_value1(1);
    fbl::futex_t futex_value2(1);
    fbl::futex_t dummy_value(1);
    TestThread threads[2];

    // If something goes wrong and we bail out early, do our best to shut down as cleanly as we can.
    auto cleanup = fbl::MakeAutoCall([&]() {
        zx_futex_wake(&futex_value1, INT_MAX);
        zx_futex_wake(&futex_value2, INT_MAX);
        for (auto& t : threads) {
            t.Shutdown();
        }
    });

    ASSERT_TRUE(threads[0].Start(&futex_value1));
    ASSERT_TRUE(threads[1].Start(&futex_value2));

    ASSERT_EQ(zx_futex_wake(&dummy_value, INT_MAX), ZX_OK);
    ASSERT_TRUE(threads[0].AssertThreadBlockedOnFutex());
    ASSERT_TRUE(threads[1].AssertThreadBlockedOnFutex());

    ASSERT_EQ(zx_futex_wake(&futex_value1, INT_MAX), ZX_OK);
    ASSERT_TRUE(threads[0].WaitThreadWoken());
    ASSERT_TRUE(threads[1].AssertThreadBlockedOnFutex());

    // Clean up: Wake the remaining thread so that it can exit.
    ASSERT_EQ(zx_futex_wake(&futex_value2, INT_MAX), ZX_OK);
    ASSERT_TRUE(threads[1].WaitThreadWoken());

    for (auto& t : threads) {
        ASSERT_EQ(t.wait_result(), ZX_OK);
        ASSERT_TRUE(t.Shutdown());
    }

    cleanup.cancel();
    END_TEST;
}

bool TestFutexRequeueValueMismatch() {
    BEGIN_TEST;
    zx_futex_t futex_value1 = 100;
    zx_futex_t futex_value2 = 200;
    zx_status_t rc = zx_futex_requeue(&futex_value1, 1, futex_value1 + 1,
                                      &futex_value2, 1, ZX_HANDLE_INVALID);
    ASSERT_EQ(rc, ZX_ERR_BAD_STATE, "requeue should have returned bad state");
    END_TEST;
}

bool TestFutexRequeueSameAddr() {
    BEGIN_TEST;
    zx_futex_t futex_value = 100;
    zx_status_t rc = zx_futex_requeue(&futex_value, 1, futex_value,
                                      &futex_value, 1, ZX_HANDLE_INVALID);
    ASSERT_EQ(rc, ZX_ERR_INVALID_ARGS, "requeue should have returned invalid args");
    END_TEST;
}

// Test that futex_requeue() can wake up some threads and requeue others.
bool TestFutexRequeue() {
    BEGIN_TEST;
    fbl::futex_t futex_value1(100);
    fbl::futex_t futex_value2(200);
    TestThread threads[6];

    // If something goes wrong and we bail out early, do our best to shut down as cleanly as we can.
    auto cleanup = fbl::MakeAutoCall([&]() {
        zx_futex_wake(&futex_value1, INT_MAX);
        zx_futex_wake(&futex_value2, INT_MAX);
        for (auto& t : threads) {
            t.Shutdown();
        }
    });

    for (auto& t : threads) {
        ASSERT_TRUE(t.Start(&futex_value1));
    }

    zx_status_t rc = zx_futex_requeue(&futex_value1, 3, 100,
                                      &futex_value2, 2, ZX_HANDLE_INVALID);
    ASSERT_EQ(rc, ZX_OK, "Error in requeue");

    // 3 of the threads should have been woken.
    ASSERT_TRUE(TestThread::AssertWokeThreadCount(threads, countof(threads), 3));

    // Since 2 of the threads should have been requeued, waking all the
    // threads on futex_value2 should wake 2 more threads.
    ASSERT_EQ(zx_futex_wake(&futex_value2, INT_MAX), ZX_OK);
    ASSERT_TRUE(TestThread::AssertWokeThreadCount(threads, countof(threads), 5));

    // Clean up: Wake the remaining thread so that it can exit.
    ASSERT_EQ(zx_futex_wake(&futex_value1, 1), ZX_OK);
    ASSERT_TRUE(TestThread::AssertWokeThreadCount(threads, countof(threads), countof(threads)));

    for (auto& t : threads) {
        ASSERT_TRUE(t.Shutdown());
    }

    cleanup.cancel();
    END_TEST;
}

// Test the case where futex_wait() times out after having been moved to a
// different queue by futex_requeue().  Check that futex_wait() removes
// itself from the correct queue in that case.
bool TestFutexRequeueUnqueuedOnTimeout() {
    BEGIN_TEST;

    fbl::futex_t futex_value1(100);
    fbl::futex_t futex_value2(200);
    TestThread threads[2];

    // If something goes wrong and we bail out early, do our best to shut down as cleanly as we can.
    auto cleanup = fbl::MakeAutoCall([&]() {
        zx_futex_wake(&futex_value1, INT_MAX);
        zx_futex_wake(&futex_value2, INT_MAX);
        for (auto& t : threads) {
            t.Shutdown();
        }
    });

    ASSERT_TRUE(threads[0].Start(&futex_value1, zx::msec(300)));
    zx_status_t rc = zx_futex_requeue(&futex_value1, 0, 100,
                                      &futex_value2, INT_MAX, ZX_HANDLE_INVALID);
    ASSERT_EQ(rc, ZX_OK, "Error in requeue");
    ASSERT_TRUE(threads[1].Start(&futex_value2));

    // thread 0 and 1 should now both be waiting on futex_value2.  Thread 0
    // should timeout in a short while, but thread 1 should still be waiting.

    ASSERT_TRUE(threads[0].WaitThreadWoken());
    ASSERT_EQ(threads[0].wait_result(), ZX_ERR_TIMED_OUT);
    ASSERT_TRUE(threads[1].AssertThreadBlockedOnFutex());

    // thread 0 should have removed itself from futex_value2's wait queue,
    // so only thread 1 should be waiting on futex_value2.  We can test that
    // by doing futex_wake() with count=1.
    ASSERT_EQ(zx_futex_wake(&futex_value2, 1), ZX_OK);
    ASSERT_TRUE(threads[1].WaitThreadWoken());

    for (auto& t : threads) {
        ASSERT_TRUE(t.Shutdown());
    }

    cleanup.cancel();
    END_TEST;
}

// Test that we can successfully kill a thread that is waiting on a futex,
// and that we can join the thread afterwards.  This checks that waiting on
// a futex does not leave the thread in an unkillable state.
bool TestFutexThreadKilled() {
    BEGIN_TEST;
    fbl::futex_t futex_value1(1);

    // TODO(johngro): Is this statement true?  It does not seem like it should
    // be.   The MUSL thread handle should still be valid and keeping the thread
    // object alive, even though the thread was killed.  In order to avoid
    // leaking the handle, it seems like a user would _have_ to join the dead
    // thread if we killed it directly using the zircon syscall.
    //
    // Note: TestThread will ensure the kernel thread died, though
    // it's not possible to thrd_join after killing the thread.
    TestThread thread;

    // If something goes wrong and we bail out early, do our best to shut down as cleanly as we can.
    auto cleanup = fbl::MakeAutoCall([&]() {
        zx_futex_wake(&futex_value1, INT_MAX);
        thread.Shutdown();
    });

    ASSERT_TRUE(thread.Start(&futex_value1));
    ASSERT_TRUE(thread.AssertThreadBlockedOnFutex());
    ASSERT_TRUE(thread.Kill());

    // Wait for the thread to make it to the DEAD state, and verify that it has
    // not managed to update either its wait_result_ or state_ members.
    ASSERT_TRUE(thread.WaitThreadInvoluntarilyTerminated());

    // TODO: update the way shutdown and kill work so that this is correct.
    ASSERT_TRUE(thread.Shutdown());

    cleanup.cancel();
    END_TEST;
}

// Test that the futex_wait() syscall is restarted properly if the thread
// calling it gets suspended and resumed.  (This tests for a bug where the
// futex_wait() syscall would return ZX_ERR_TIMED_OUT and not get restarted by
// the syscall wrapper in the VDSO.)
static bool TestFutexThreadSuspended() {
    BEGIN_TEST;
    fbl::futex_t futex_value1(1);

    TestThread thread;

    // If something goes wrong and we bail out early, do our best to shut down as cleanly as we can.
    auto cleanup = fbl::MakeAutoCall([&]() {
        zx_futex_wake(&futex_value1, INT_MAX);
        thread.Shutdown();
    });

    ASSERT_TRUE(thread.Start(&futex_value1));

    zx::suspend_token suspend_token;
    ASSERT_EQ(thread.get_thread_handle().suspend(&suspend_token), ZX_OK);

    // Wait until the thread is suspended.
    ASSERT_TRUE(thread.WaitForKernelState(ZX_THREAD_STATE_SUSPENDED));
    ASSERT_EQ(zx_handle_close(suspend_token.release()), ZX_OK);

    // Wait some time for the thread to resume and execute.
    ASSERT_TRUE(thread.WaitForKernelState(ZX_THREAD_STATE_BLOCKED_FUTEX));
    ASSERT_TRUE(thread.AssertThreadBlockedOnFutex());

    ASSERT_EQ(zx_futex_wake(&futex_value1, 1), ZX_OK);
    ASSERT_TRUE(TestThread::AssertWokeThreadCount(&thread, 1, 1));
    ASSERT_TRUE(thread.Shutdown());

    cleanup.cancel();
    END_TEST;
}

// Test that misaligned pointers cause futex syscalls to return a failure.
static bool TestFutexMisaligned() {
    BEGIN_TEST;

    // Make sure the whole thing is aligned, so the 'futex' member will
    // definitely be misaligned.
    alignas(zx_futex_t) struct {
        uint8_t misalign;
        zx_futex_t futex[2];
    } __attribute__((packed)) buffer;
    zx_futex_t* const futex = &buffer.futex[0];
    zx_futex_t* const futex_2 = &buffer.futex[1];
    ASSERT_GT(alignof(zx_futex_t), 1);
    ASSERT_NE((uintptr_t)futex % alignof(zx_futex_t), 0);
    ASSERT_NE((uintptr_t)futex_2 % alignof(zx_futex_t), 0);

    // zx_futex_requeue might check the waited-for value before it
    // checks the second futex's alignment, so make sure the call is
    // valid other than the alignment.  (Also don't ask anybody to
    // look at uninitialized stack space!)
    memset(&buffer, 0, sizeof(buffer));

    ASSERT_EQ(zx_futex_wait(futex, 0, ZX_HANDLE_INVALID, ZX_TIME_INFINITE), ZX_ERR_INVALID_ARGS);
    ASSERT_EQ(zx_futex_wake(futex, 1), ZX_ERR_INVALID_ARGS);
    ASSERT_EQ(zx_futex_requeue(futex, 1, 0, futex_2, 1, ZX_HANDLE_INVALID), ZX_ERR_INVALID_ARGS);

    END_TEST;
}

static void log(const char* str) {
    zx::time now = zx::clock::get_monotonic();
    unittest_printf("[%08" PRIu64 ".%08" PRIu64 "]: %s",
                    now.get() / 1000000000, now.get() % 1000000000, str);
}

class Event {
public:
    Event()
        : signaled_(0) {}

    void wait() {
        if (signaled_ == 0) {
            zx_futex_wait(&signaled_, signaled_, ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
        }
    }

    void signal() {
        if (signaled_ == 0) {
            signaled_ = 1;
            zx_futex_wake(&signaled_, UINT32_MAX);
        }
    }

private:
    int32_t signaled_;
};

static Event event;

static int signal_thread1(void* arg) {
    log("thread 1 waiting on event\n");
    event.wait();
    log("thread 1 done\n");
    return 0;
}

static int signal_thread2(void* arg) {
    log("thread 2 waiting on event\n");
    event.wait();
    log("thread 2 done\n");
    return 0;
}

static int signal_thread3(void* arg) {
    log("thread 3 waiting on event\n");
    event.wait();
    log("thread 3 done\n");
    return 0;
}

static bool wait_blocked_on_futex(thrd_t thread) {
    BEGIN_HELPER;

    zx_handle_t thrd_handle = thrd_get_zx_handle(thread);
    ASSERT_NE(thrd_handle, ZX_HANDLE_INVALID);

    zx_info_thread_t info;
    zx_status_t get_info_res = ZX_ERR_INTERNAL;
    zx_status_t wait_res = ZX_ERR_INTERNAL;

    wait_res = WaitFor([&]() {
        get_info_res = zx_object_get_info(thrd_handle, ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr);
        return (get_info_res != ZX_OK) || (info.state == ZX_THREAD_STATE_BLOCKED_FUTEX);
    });

    EXPECT_EQ(get_info_res, ZX_OK);
    EXPECT_EQ(wait_res, ZX_OK);
    EXPECT_EQ(info.state, ZX_THREAD_STATE_BLOCKED_FUTEX);

    END_HELPER;
}

static bool TestEventSignaling() {
    BEGIN_TEST;
    thrd_t thread1, thread2, thread3;

    log("starting signal threads\n");
    thrd_create_with_name(&thread1, signal_thread1, NULL, "thread 1");
    thrd_create_with_name(&thread2, signal_thread2, NULL, "thread 2");
    thrd_create_with_name(&thread3, signal_thread3, NULL, "thread 3");

    ASSERT_TRUE(wait_blocked_on_futex(thread1));
    ASSERT_TRUE(wait_blocked_on_futex(thread2));
    ASSERT_TRUE(wait_blocked_on_futex(thread3));

    log("signaling event\n");
    event.signal();

    log("joining signal threads\n");
    thrd_join(thread1, NULL);
    log("signal_thread 1 joined\n");
    thrd_join(thread2, NULL);
    log("signal_thread 2 joined\n");
    thrd_join(thread3, NULL);
    log("signal_thread 3 joined\n");
    END_TEST;
}

BEGIN_TEST_CASE(futex_tests)
RUN_TEST(TestFutexWaitValueMismatch);
RUN_TEST(TestFutexWaitTimeout);
RUN_TEST(TestFutexWaitTimeoutElapsed);
RUN_TEST(TestFutexWaitBadAddress);
RUN_TEST(TestFutexWakeup);
RUN_TEST(TestFutexWakeupLimit);
RUN_TEST(TestFutexWakeupAddress);
RUN_TEST(TestFutexRequeueValueMismatch);
RUN_TEST(TestFutexRequeueSameAddr);
RUN_TEST(TestFutexRequeue);
RUN_TEST(TestFutexRequeueUnqueuedOnTimeout);
RUN_TEST(TestFutexThreadKilled);
RUN_TEST(TestFutexThreadSuspended);
RUN_TEST(TestFutexMisaligned);
RUN_TEST(TestEventSignaling);
END_TEST_CASE(futex_tests)
