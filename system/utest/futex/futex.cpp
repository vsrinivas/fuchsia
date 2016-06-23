// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <limits.h>
#include <magenta/syscalls.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void assert_eq_check(int lhs_value, int rhs_value,
                            const char* lhs_expr, const char* rhs_expr,
                            const char* source_file,
                            int source_line) {
    if (lhs_value != rhs_value) {
        printf("Error at %s, line %d:\n", source_file, source_line);
        printf("Assertion failed: %s != %s\n", lhs_expr, rhs_expr);
        printf("Got values: %d != %d\n", lhs_value, rhs_value);
        abort();
    }
}

#define ASSERT_EQ(x, y) assert_eq_check((x), (y), #x, #y, __FILE__, __LINE__)

static void test_futex_wait_value_mismatch() {
    int futex_value = 123;
    mx_status_t rc = _magenta_futex_wait(&futex_value, futex_value + 1,
                                         MX_TIME_INFINITE);
    ASSERT_EQ(rc, ERR_BUSY);
}

static void test_futex_wait_timeout() {
    int futex_value = 123;
    mx_status_t rc = _magenta_futex_wait(&futex_value, futex_value, 0);
    ASSERT_EQ(rc, ERR_TIMED_OUT);
}

static void test_futex_wait_bad_address() {
    // Check that the wait address is checked for validity.
    mx_status_t rc = _magenta_futex_wait(nullptr, 123, MX_TIME_INFINITE);
    ASSERT_EQ(rc, ERR_INVALID_ARGS);
}

// This starts a thread which waits on a futex.  We can do futex_wake()
// operations and then test whether or not this thread has been woken up.
class TestThread {
public:
    TestThread(volatile int* futex_addr,
               mx_time_t timeout_in_us = MX_TIME_INFINITE)
        : futex_addr_(futex_addr),
          timeout_in_us_(timeout_in_us),
          state_(STATE_STARTED) {
        thread_handle_ = _magenta_thread_create(wakeup_test_thread, this,
                                                "wakeup_test_thread", 19);
        ASSERT_EQ(thread_handle_ > 0, true);
        while (state_ == STATE_STARTED) {
            sched_yield();
        }
        // Note that this could fail if futex_wait() gets a spurious wakeup.
        ASSERT_EQ(state_, STATE_ABOUT_TO_WAIT);
        // This should be long enough for wakeup_test_thread() to enter
        // futex_wait() and add the thread to the wait queue.
        struct timespec wait_time = {0, 100 * 1000000 /* nanoseconds */};
        ASSERT_EQ(nanosleep(&wait_time, NULL), 0);
        // This could also fail if futex_wait() gets a spurious wakeup.
        ASSERT_EQ(state_, STATE_ABOUT_TO_WAIT);
    }

    ~TestThread() {
        ASSERT_EQ(_magenta_handle_wait_one(thread_handle_, MX_SIGNAL_SIGNALED,
                                           MX_TIME_INFINITE, NULL, NULL),
                  NO_ERROR);
    }

    void assert_thread_woken() {
        while (state_ == STATE_ABOUT_TO_WAIT) {
            sched_yield();
        }
        ASSERT_EQ(state_, STATE_WAIT_RETURNED);
    }

    void assert_thread_not_woken() {
        ASSERT_EQ(state_, STATE_ABOUT_TO_WAIT);
    }

    void wait_for_timeout() {
        ASSERT_EQ(state_, STATE_ABOUT_TO_WAIT);
        while (state_ == STATE_ABOUT_TO_WAIT) {
            struct timespec wait_time = {0, 50 * 1000000 /* nanoseconds */};
            ASSERT_EQ(nanosleep(&wait_time, NULL), 0);
        }
        ASSERT_EQ(state_, STATE_WAIT_RETURNED);
    }

private:
    static int wakeup_test_thread(void* thread_arg) {
        TestThread* thread = reinterpret_cast<TestThread*>(thread_arg);
        thread->state_ = STATE_ABOUT_TO_WAIT;
        mx_status_t rc =
            _magenta_futex_wait(const_cast<int*>(thread->futex_addr_),
                                *thread->futex_addr_, thread->timeout_in_us_);
        if (thread->timeout_in_us_ == MX_TIME_INFINITE) {
            ASSERT_EQ(rc, NO_ERROR);
        } else {
            ASSERT_EQ(rc, ERR_TIMED_OUT);
        }
        thread->state_ = STATE_WAIT_RETURNED;
        _magenta_thread_exit();
        return 0;
    }

    mx_handle_t thread_handle_;
    volatile int* futex_addr_;
    mx_time_t timeout_in_us_;
    volatile enum {
        STATE_STARTED = 100,
        STATE_ABOUT_TO_WAIT = 200,
        STATE_WAIT_RETURNED = 300,
    } state_;
};

void check_futex_wake(volatile int* futex_addr, int nwake) {
    // Change *futex_addr just in case our nanosleep() call did not wait
    // long enough for futex_wait() to enter the wait queue, although that
    // is unlikely.  This prevents the test from hanging if that happens,
    // though the test will fail because futex_wait() will not return a
    // success result.
    (*futex_addr)++;

    mx_status_t rc = _magenta_futex_wake(const_cast<int*>(futex_addr), nwake);
    ASSERT_EQ(rc, NO_ERROR);
}

// Test that we can wake up a single thread.
void test_futex_wakeup() {
    volatile int futex_value = 1;
    TestThread thread(&futex_value);
    check_futex_wake(&futex_value, INT_MAX);
    thread.assert_thread_woken();
}

// Test that we can wake up multiple threads, and that futex_wake() heeds
// the wakeup limit.
void test_futex_wakeup_limit() {
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
}

// Check that futex_wait() and futex_wake() heed their address arguments
// properly.  A futex_wait() call on one address should not be woken by a
// futex_wake() call on another address.
void test_futex_wakeup_address() {
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
}

// Check that when futex_wait() times out, it removes the thread from
// the futex wait queue.
void test_futex_unqueued_on_timeout() {
    volatile int futex_value = 1;
    mx_status_t rc = _magenta_futex_wait(const_cast<int*>(&futex_value),
                                         futex_value, 1);
    ASSERT_EQ(rc, ERR_TIMED_OUT);
    TestThread thread(&futex_value);
    // If the earlier futex_wait() did not remove itself from the wait
    // queue properly, the following futex_wake() call will attempt to wake
    // a thread that is no longer waiting, rather than waking the child
    // thread.
    check_futex_wake(&futex_value, 1);
    thread.assert_thread_woken();
}

// This tests for a specific bug in list handling.
void test_futex_unqueued_on_timeout_2() {
    volatile int futex_value = 10;
    TestThread thread1(&futex_value);
    TestThread thread2(&futex_value, 200 * 1000 * 1000);
    thread2.wait_for_timeout();
    // With the bug present, thread2 was removed but the futex wait queue's
    // tail pointer still points to thread2.  When another thread is
    // enqueued, it gets added to the thread2 node and lost.

    TestThread thread3(&futex_value);
    check_futex_wake(&futex_value, 2);
    thread1.assert_thread_woken();
    thread3.assert_thread_woken();
}

// This tests for a specific bug in list handling.
void test_futex_unqueued_on_timeout_3() {
    volatile int futex_value = 10;
    TestThread thread1(&futex_value, 400 * 1000 * 1000);
    TestThread thread2(&futex_value);
    TestThread thread3(&futex_value);
    thread1.wait_for_timeout();
    // With the bug present, thread1 was removed but the futex wait queue
    // is set to the thread2 node, which has an invalid (null) tail
    // pointer.  When another thread is enqueued, we get a null pointer
    // dereference or an assertion failure.

    TestThread thread4(&futex_value);
    check_futex_wake(&futex_value, 3);
    thread2.assert_thread_woken();
    thread3.assert_thread_woken();
    thread4.assert_thread_woken();
}

void test_futex_requeue_value_mismatch() {
    int futex_value1 = 100;
    int futex_value2 = 200;
    mx_status_t rc = _magenta_futex_requeue(&futex_value1, 1, futex_value1 + 1,
                                            &futex_value2, 1);
    ASSERT_EQ(rc, ERR_BUSY);
}

void test_futex_requeue_same_addr() {
    int futex_value = 100;
    mx_status_t rc = _magenta_futex_requeue(&futex_value, 1, futex_value,
                                            &futex_value, 1);
    ASSERT_EQ(rc, ERR_INVALID_ARGS);
}

// Test that futex_requeue() can wake up some threads and requeue others.
void test_futex_requeue() {
    volatile int futex_value1 = 100;
    volatile int futex_value2 = 200;
    TestThread thread1(&futex_value1);
    TestThread thread2(&futex_value1);
    TestThread thread3(&futex_value1);
    TestThread thread4(&futex_value1);
    TestThread thread5(&futex_value1);
    TestThread thread6(&futex_value1);

    mx_status_t rc = _magenta_futex_requeue(
        const_cast<int*>(&futex_value1), 3, futex_value1,
        const_cast<int*>(&futex_value2), 2);
    ASSERT_EQ(rc, NO_ERROR);
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
}

// Test the case where futex_wait() times out after having been moved to a
// different queue by futex_requeue().  Check that futex_wait() removes
// itself from the correct queue in that case.
void test_futex_requeue_unqueued_on_timeout() {
    mx_time_t timeout_in_us = 300 * 1000 * 1000;
    volatile int futex_value1 = 100;
    volatile int futex_value2 = 200;
    TestThread thread1(&futex_value1, timeout_in_us);
    mx_status_t rc = _magenta_futex_requeue(
        const_cast<int*>(&futex_value1), 0, futex_value1,
        const_cast<int*>(&futex_value2), INT_MAX);
    ASSERT_EQ(rc, NO_ERROR);
    TestThread thread2(&futex_value2);
    // thread1 and thread2 should now both be waiting on futex_value2.

    thread1.wait_for_timeout();
    thread2.assert_thread_not_woken();
    // thread1 should have removed itself from futex_value2's wait queue,
    // so only thread2 should be waiting on futex_value2.  We can test that
    // by doing futex_wake() with count=1.

    check_futex_wake(&futex_value2, 1);
    thread2.assert_thread_woken();
}

static void log(const char* str) {
    uint64_t now = _magenta_current_time();
    printf("[%08llu.%08llu]: %s", now / 1000000000, now % 1000000000, str);
}

class Event {
public:
    Event()
        : signalled_(0) {}

    void wait() {
        if (signalled_ == 0) {
            _magenta_futex_wait(&signalled_, signalled_, MX_TIME_INFINITE);
        }
    }

    void signal() {
        if (signalled_ == 0) {
            signalled_ = 1;
            _magenta_futex_wake(&signalled_, UINT32_MAX);
        }
    }

private:
    int signalled_;
};

Event event;

static int signal_thread1(void* arg) {
    log("thread 1 waiting on event\n");
    event.wait();
    log("thread 1 done\n");
    _magenta_thread_exit();
    return 0;
}

static int signal_thread2(void* arg) {
    log("thread 2 waiting on event\n");
    event.wait();
    log("thread 2 done\n");
    _magenta_thread_exit();
    return 0;
}

static int signal_thread3(void* arg) {
    log("thread 3 waiting on event\n");
    event.wait();
    log("thread 3 done\n");
    _magenta_thread_exit();
    return 0;
}

static void test_event_signalling() {
    mx_handle_t handle1, handle2, handle3;

    log("starting signal threads\n");
    handle1 = _magenta_thread_create(signal_thread1, NULL, "thread 1", 9);
    handle2 = _magenta_thread_create(signal_thread2, NULL, "thread 2", 9);
    handle3 = _magenta_thread_create(signal_thread3, NULL, "thread 3", 9);

    _magenta_nanosleep(300 * 1000 * 1000);
    log("signalling event\n");
    event.signal();

    log("joining signal threads\n");
    _magenta_handle_wait_one(handle1, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL);
    log("signal_thread 1 joined\n");
    _magenta_handle_wait_one(handle2, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL);
    log("signal_thread 2 joined\n");
    _magenta_handle_wait_one(handle3, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL, NULL);
    log("signal_thread 3 joined\n");

    _magenta_handle_close(handle1);
    _magenta_handle_close(handle2);
    _magenta_handle_close(handle3);
}

static void run_test(const char* test_name, void (*test_func)()) {
    printf("Running %s...\n", test_name);
    test_func();
}

#define RUN_TEST(test_func) (run_test(#test_func, test_func))

extern "C" int main(void) {
    RUN_TEST(test_futex_wait_value_mismatch);
    RUN_TEST(test_futex_wait_timeout);
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

    RUN_TEST(test_event_signalling);

    return 0;
}
