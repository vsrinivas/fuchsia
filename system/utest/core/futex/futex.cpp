// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <limits.h>
#include <magenta/syscalls.h>
#include <magenta/threads.h>
#include <unittest/unittest.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>


static bool test_futex_wait_value_mismatch() {
    BEGIN_TEST;
    int futex_value = 123;
    mx_status_t rc = mx_futex_wait(&futex_value, futex_value + 1,
                                         MX_TIME_INFINITE);
    ASSERT_EQ(rc, ERR_BAD_STATE, "Futex wait should have reurned bad state");
    END_TEST;
}

static bool test_futex_wait_timeout() {
    BEGIN_TEST;
    int futex_value = 123;
    mx_status_t rc = mx_futex_wait(&futex_value, futex_value, 0);
    ASSERT_EQ(rc, ERR_TIMED_OUT, "Futex wait should have reurned timeout");
    END_TEST;
}

// This test checks that the timeout in futex_wait() is respected
static bool test_futex_wait_timeout_elapsed() {
    BEGIN_TEST;
    int futex_value = 0;
    constexpr mx_duration_t kRelativeDeadline = MX_MSEC(500);
    for (int i = 0; i < 5; ++i) {
        mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);
        mx_status_t rc = mx_futex_wait(&futex_value, 0, mx_deadline_after(kRelativeDeadline));
        ASSERT_EQ(rc, ERR_TIMED_OUT, "wait should time out");
        mx_time_t elapsed = mx_time_get(MX_CLOCK_MONOTONIC) - now;
        if (elapsed < kRelativeDeadline) {
            unittest_printf("\nelapsed %" PRIu64
                            " < kRelativeDeadline: %" PRIu64 "\n",
                            elapsed, kRelativeDeadline);
            EXPECT_TRUE(false, "wait returned early");
        }
    }
    END_TEST;
}


static bool test_futex_wait_bad_address() {
    BEGIN_TEST;
    // Check that the wait address is checked for validity.
    mx_status_t rc = mx_futex_wait(nullptr, 123, MX_TIME_INFINITE);
    ASSERT_EQ(rc, ERR_INVALID_ARGS, "Futex wait should have reurned invalid_arg");
    END_TEST;
}

// This starts a thread which waits on a futex.  We can do futex_wake()
// operations and then test whether or not this thread has been woken up.
class TestThread {
public:
    TestThread(volatile int* futex_addr,
               mx_duration_t timeout_in_us = MX_TIME_INFINITE)
        : futex_addr_(futex_addr),
          timeout_in_ns_(timeout_in_us) {
        auto ret = thrd_create_with_name(&thread_, wakeup_test_thread, this, "wakeup_test_thread");
        EXPECT_EQ(ret, thrd_success, "Error during thread creation");
        while (state_ == STATE_STARTED) {
            sched_yield();
        }
        // Note that this could fail if futex_wait() gets a spurious wakeup.
        EXPECT_EQ(state_, STATE_ABOUT_TO_WAIT, "wrong state");
        // This should be long enough for wakeup_test_thread() to enter
        // futex_wait() and add the thread to the wait queue.
        struct timespec wait_time = {0, 100 * 1000000 /* nanoseconds */};
        EXPECT_EQ(nanosleep(&wait_time, NULL), 0, "Error in nanosleep");
        // This could also fail if futex_wait() gets a spurious wakeup.
        EXPECT_EQ(state_, STATE_ABOUT_TO_WAIT, "wrong state");
    }

    TestThread(const TestThread &) = delete;
    TestThread& operator=(const TestThread &) = delete;

    ~TestThread() {
        if (handle_ != MX_HANDLE_INVALID) {
            // kill_thread() was used, so the thrd_t is in undefined state.
            // Use the kernel handle to ensure the thread has died.
            EXPECT_EQ(mx_object_wait_one(handle_, MX_THREAD_TERMINATED,
                                         MX_TIME_INFINITE, NULL), NO_ERROR,
                      "mx_object_wait_one failed on killed thread");
            EXPECT_EQ(mx_handle_close(handle_), NO_ERROR,
                      "mx_handle_close failed on killed thread's handle");
            // The thrd_t and state associated with it is leaked at this point.
        } else {
            EXPECT_EQ(thrd_join(thread_, NULL), thrd_success,
                      "thrd_join failed");
        }
    }

    void assert_thread_woken() {
        while (state_ == STATE_ABOUT_TO_WAIT) {
            sched_yield();
        }
        EXPECT_EQ(state_, STATE_WAIT_RETURNED, "wrong state");
    }

    void assert_thread_not_woken() {
        EXPECT_EQ(state_, STATE_ABOUT_TO_WAIT, "wrong state");
    }

    bool wait_for_timeout() {
        ASSERT_EQ(state_, STATE_ABOUT_TO_WAIT, "wrong state");
        while (state_ == STATE_ABOUT_TO_WAIT) {
            struct timespec wait_time = {0, 50 * 1000000 /* nanoseconds */};
            ASSERT_EQ(nanosleep(&wait_time, NULL), 0, "Error during sleep");
        }
        EXPECT_EQ(state_, STATE_WAIT_RETURNED, "wrong state");
        return true;
    }

    void kill_thread() {
        EXPECT_EQ(handle_, MX_HANDLE_INVALID, "kill_thread called twice??");
        EXPECT_EQ(mx_handle_duplicate(thrd_get_mx_handle(thread_),
                                      MX_RIGHT_SAME_RIGHTS, &handle_),
                  NO_ERROR, "mx_handle_duplicate failed on thread handle");
        EXPECT_EQ(mx_task_kill(handle_), NO_ERROR, "mx_task_kill() failed");
    }

private:
    static int wakeup_test_thread(void* thread_arg) {
        TestThread* thread = reinterpret_cast<TestThread*>(thread_arg);
        thread->state_ = STATE_ABOUT_TO_WAIT;
        mx_time_t deadline = thread->timeout_in_ns_ == MX_TIME_INFINITE ? MX_TIME_INFINITE :
                mx_deadline_after(thread->timeout_in_ns_);
        mx_status_t rc = mx_futex_wait(const_cast<int*>(thread->futex_addr_),
                                       *thread->futex_addr_, deadline);
        if (thread->timeout_in_ns_ == MX_TIME_INFINITE) {
            EXPECT_EQ(rc, NO_ERROR, "Error while wait");
        } else {
            EXPECT_EQ(rc, ERR_TIMED_OUT, "wait should have timedout");
        }
        thread->state_ = STATE_WAIT_RETURNED;
        return 0;
    }

    thrd_t thread_;
    volatile int* futex_addr_;
    mx_duration_t timeout_in_ns_;
    mx_handle_t handle_ = MX_HANDLE_INVALID;
    volatile enum {
        STATE_STARTED = 100,
        STATE_ABOUT_TO_WAIT = 200,
        STATE_WAIT_RETURNED = 300,
    } state_ = STATE_STARTED;
};

void check_futex_wake(volatile int* futex_addr, int nwake) {
    // Change *futex_addr just in case our nanosleep() call did not wait
    // long enough for futex_wait() to enter the wait queue, although that
    // is unlikely.  This prevents the test from hanging if that happens,
    // though the test will fail because futex_wait() will not return a
    // success result.
    (*futex_addr)++;

    mx_status_t rc = mx_futex_wake(const_cast<int*>(futex_addr), nwake);
    EXPECT_EQ(rc, NO_ERROR, "error during futex wait");
}

// Test that we can wake up a single thread.
bool test_futex_wakeup() {
    BEGIN_TEST;
    volatile int futex_value = 1;
    TestThread thread(&futex_value);
    check_futex_wake(&futex_value, INT_MAX);
    thread.assert_thread_woken();
    END_TEST;
}

// Test that we can wake up multiple threads, and that futex_wake() heeds
// the wakeup limit.
bool test_futex_wakeup_limit() {
    BEGIN_TEST;
    volatile int futex_value = 1;
    TestThread thread1(&futex_value);
    TestThread thread2(&futex_value);
    TestThread thread3(&futex_value);
    TestThread thread4(&futex_value);
    check_futex_wake(&futex_value, 2);
    // Test that threads are woken up in the order that they were added to
    // the wait queue.  This is not necessarily true for the Linux
    // implementation of futexes, but it is true for Magenta's
    // implementation.
    thread1.assert_thread_woken();
    thread2.assert_thread_woken();
    thread3.assert_thread_not_woken();
    thread4.assert_thread_not_woken();

    // Clean up: Wake the remaining threads so that they can exit.
    check_futex_wake(&futex_value, INT_MAX);
    thread3.assert_thread_woken();
    thread4.assert_thread_woken();
    END_TEST;
}

// Check that futex_wait() and futex_wake() heed their address arguments
// properly.  A futex_wait() call on one address should not be woken by a
// futex_wake() call on another address.
bool test_futex_wakeup_address() {
    BEGIN_TEST;
    volatile int futex_value1 = 1;
    volatile int futex_value2 = 1;
    volatile int dummy_addr = 1;
    TestThread thread1(&futex_value1);
    TestThread thread2(&futex_value2);

    check_futex_wake(&dummy_addr, INT_MAX);
    thread1.assert_thread_not_woken();
    thread2.assert_thread_not_woken();

    check_futex_wake(&futex_value1, INT_MAX);
    thread1.assert_thread_woken();
    thread2.assert_thread_not_woken();

    // Clean up: Wake the remaining thread so that it can exit.
    check_futex_wake(&futex_value2, INT_MAX);
    thread2.assert_thread_woken();
    END_TEST;
}

// Check that when futex_wait() times out, it removes the thread from
// the futex wait queue.
bool test_futex_unqueued_on_timeout() {
    BEGIN_TEST;
    volatile int futex_value = 1;
    mx_status_t rc = mx_futex_wait(const_cast<int*>(&futex_value),
                                   futex_value, mx_deadline_after(1));
    ASSERT_EQ(rc, ERR_TIMED_OUT, "wait should have timedout");
    TestThread thread(&futex_value);
    // If the earlier futex_wait() did not remove itself from the wait
    // queue properly, the following futex_wake() call will attempt to wake
    // a thread that is no longer waiting, rather than waking the child
    // thread.
    check_futex_wake(&futex_value, 1);
    thread.assert_thread_woken();
    END_TEST;
}

// This tests for a specific bug in list handling.
bool test_futex_unqueued_on_timeout_2() {
    BEGIN_TEST;
    volatile int futex_value = 10;
    TestThread thread1(&futex_value);
    TestThread thread2(&futex_value, MX_MSEC(200));
    ASSERT_TRUE(thread2.wait_for_timeout(), "");
    // With the bug present, thread2 was removed but the futex wait queue's
    // tail pointer still points to thread2.  When another thread is
    // enqueued, it gets added to the thread2 node and lost.

    TestThread thread3(&futex_value);
    check_futex_wake(&futex_value, 2);
    thread1.assert_thread_woken();
    thread3.assert_thread_woken();
    END_TEST;
}

// This tests for a specific bug in list handling.
bool test_futex_unqueued_on_timeout_3() {
    BEGIN_TEST;
    volatile int futex_value = 10;
    TestThread thread1(&futex_value, MX_MSEC(400));
    TestThread thread2(&futex_value);
    TestThread thread3(&futex_value);
    ASSERT_TRUE(thread1.wait_for_timeout(), "");
    // With the bug present, thread1 was removed but the futex wait queue
    // is set to the thread2 node, which has an invalid (null) tail
    // pointer.  When another thread is enqueued, we get a null pointer
    // dereference or an assertion failure.

    TestThread thread4(&futex_value);
    check_futex_wake(&futex_value, 3);
    thread2.assert_thread_woken();
    thread3.assert_thread_woken();
    thread4.assert_thread_woken();
    END_TEST;
}

bool test_futex_requeue_value_mismatch() {
    BEGIN_TEST;
    int futex_value1 = 100;
    int futex_value2 = 200;
    mx_status_t rc = mx_futex_requeue(&futex_value1, 1, futex_value1 + 1,
                                            &futex_value2, 1);
    ASSERT_EQ(rc, ERR_BAD_STATE, "requeue should have returned bad state");
    END_TEST;
}

bool test_futex_requeue_same_addr() {
    BEGIN_TEST;
    int futex_value = 100;
    mx_status_t rc = mx_futex_requeue(&futex_value, 1, futex_value,
                                            &futex_value, 1);
    ASSERT_EQ(rc, ERR_INVALID_ARGS, "requeue should have returned invalid args");
    END_TEST;
}

// Test that futex_requeue() can wake up some threads and requeue others.
bool test_futex_requeue() {
    BEGIN_TEST;
    volatile int futex_value1 = 100;
    volatile int futex_value2 = 200;
    TestThread thread1(&futex_value1);
    TestThread thread2(&futex_value1);
    TestThread thread3(&futex_value1);
    TestThread thread4(&futex_value1);
    TestThread thread5(&futex_value1);
    TestThread thread6(&futex_value1);

    mx_status_t rc = mx_futex_requeue(
        const_cast<int*>(&futex_value1), 3, futex_value1,
        const_cast<int*>(&futex_value2), 2);
    ASSERT_EQ(rc, NO_ERROR, "Error in requeue");
    // 3 of the threads should have been woken.
    thread1.assert_thread_woken();
    thread2.assert_thread_woken();
    thread3.assert_thread_woken();
    thread4.assert_thread_not_woken();
    thread5.assert_thread_not_woken();
    thread6.assert_thread_not_woken();

    // Since 2 of the threads should have been requeued, waking all the
    // threads on futex_value2 should wake 2 threads.
    check_futex_wake(&futex_value2, INT_MAX);
    thread4.assert_thread_woken();
    thread5.assert_thread_woken();
    thread6.assert_thread_not_woken();

    // Clean up: Wake the remaining thread so that it can exit.
    check_futex_wake(&futex_value1, 1);
    thread6.assert_thread_woken();
    END_TEST;
}

// Test the case where futex_wait() times out after having been moved to a
// different queue by futex_requeue().  Check that futex_wait() removes
// itself from the correct queue in that case.
bool test_futex_requeue_unqueued_on_timeout() {
    BEGIN_TEST;
    mx_duration_t timeout_in_ns = MX_MSEC(300);
    volatile int futex_value1 = 100;
    volatile int futex_value2 = 200;
    TestThread thread1(&futex_value1, timeout_in_ns);
    mx_status_t rc = mx_futex_requeue(
        const_cast<int*>(&futex_value1), 0, futex_value1,
        const_cast<int*>(&futex_value2), INT_MAX);
    ASSERT_EQ(rc, NO_ERROR, "Error in requeue");
    TestThread thread2(&futex_value2);
    // thread1 and thread2 should now both be waiting on futex_value2.

    ASSERT_TRUE(thread1.wait_for_timeout(), "");
    thread2.assert_thread_not_woken();
    // thread1 should have removed itself from futex_value2's wait queue,
    // so only thread2 should be waiting on futex_value2.  We can test that
    // by doing futex_wake() with count=1.

    check_futex_wake(&futex_value2, 1);
    thread2.assert_thread_woken();
    END_TEST;
}

// Test that we can successfully kill a thread that is waiting on a futex,
// and that we can join the thread afterwards.  This checks that waiting on
// a futex does not leave the thread in an unkillable state.
bool test_futex_thread_killed() {
    BEGIN_TEST;
    volatile int futex_value = 1;
    // Note: TestThread will ensure the kernel thread died, though
    // it's not possible to thrd_join after killing the thread.
    TestThread thread(&futex_value);
    thread.kill_thread();

    // Check that the futex_wait() syscall does not return control to
    // userland before the thread gets killed.
    struct timespec wait_time = {0, 10 * 1000000 /* nanoseconds */};
    ASSERT_EQ(nanosleep(&wait_time, NULL), 0, "Error during sleep");
    thread.assert_thread_not_woken();

    END_TEST;
}

// Test that misaligned pointers cause futex syscalls to return a failure.
static bool test_futex_misaligned() {
  BEGIN_TEST;

  // Make sure the whole thing is aligned, so the 'futex' member will
  // definitely be misaligned.
  alignas(mx_futex_t) struct {
      uint8_t misalign;
      mx_futex_t futex[2];
  } __attribute__((packed)) buffer;
  mx_futex_t* const futex = &buffer.futex[0];
  mx_futex_t* const futex_2 = &buffer.futex[1];
  ASSERT_GT(alignof(mx_futex_t), 1, "");
  ASSERT_NEQ((uintptr_t)futex % alignof(mx_futex_t), 0, "");
  ASSERT_NEQ((uintptr_t)futex_2 % alignof(mx_futex_t), 0, "");

  // mx_futex_requeue might check the waited-for value before it
  // checks the second futex's alignment, so make sure the call is
  // valid other than the alignment.  (Also don't ask anybody to
  // look at uninitialized stack space!)
  memset(&buffer, 0, sizeof(buffer));

  ASSERT_EQ(mx_futex_wait(futex, 0, MX_TIME_INFINITE), ERR_INVALID_ARGS, "");
  ASSERT_EQ(mx_futex_wake(futex, 1), ERR_INVALID_ARGS, "");
  ASSERT_EQ(mx_futex_requeue(futex, 1, 0, futex_2, 1), ERR_INVALID_ARGS, "");

  END_TEST;
}

static void log(const char* str) {
    uint64_t now = mx_time_get(MX_CLOCK_MONOTONIC);
    unittest_printf("[%08" PRIu64 ".%08" PRIu64 "]: %s",
                    now / 1000000000, now % 1000000000, str);
}

class Event {
public:
    Event()
        : signaled_(0) {}

    void wait() {
        if (signaled_ == 0) {
            mx_futex_wait(&signaled_, signaled_, MX_TIME_INFINITE);
        }
    }

    void signal() {
        if (signaled_ == 0) {
            signaled_ = 1;
            mx_futex_wake(&signaled_, UINT32_MAX);
        }
    }

private:
    int signaled_;
};

Event event;

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

static bool test_event_signaling() {
    BEGIN_TEST;
    thrd_t thread1, thread2, thread3;

    log("starting signal threads\n");
    thrd_create_with_name(&thread1, signal_thread1, NULL, "thread 1");
    thrd_create_with_name(&thread2, signal_thread2, NULL, "thread 2");
    thrd_create_with_name(&thread3, signal_thread3, NULL, "thread 3");

    mx_nanosleep(mx_deadline_after(MX_MSEC(300)));
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
RUN_TEST(test_futex_wait_value_mismatch);
RUN_TEST(test_futex_wait_timeout);
RUN_TEST(test_futex_wait_timeout_elapsed);
RUN_TEST(test_futex_wait_bad_address);
RUN_TEST(test_futex_wakeup);
RUN_TEST(test_futex_wakeup_limit);
RUN_TEST(test_futex_wakeup_address);
RUN_TEST(test_futex_unqueued_on_timeout);
RUN_TEST(test_futex_unqueued_on_timeout_2);
RUN_TEST(test_futex_unqueued_on_timeout_3);
RUN_TEST(test_futex_requeue_value_mismatch);
RUN_TEST(test_futex_requeue_same_addr);
RUN_TEST(test_futex_requeue);
RUN_TEST(test_futex_requeue_unqueued_on_timeout);
RUN_TEST(test_futex_thread_killed);
RUN_TEST(test_futex_misaligned);
RUN_TEST(test_event_signaling);
END_TEST_CASE(futex_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
#endif
