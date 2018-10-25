// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>

#include <zircon/syscalls.h>
#include <zircon/threads.h>
#include <unittest/unittest.h>
#include <sched.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define ITERATIONS 64

static int sync_completion_thread_wait(void* arg) {
    auto completion = static_cast<sync_completion_t*>(arg);
    for (int iteration = 0u; iteration < ITERATIONS; iteration++) {
        zx_status_t status = sync_completion_wait(completion, ZX_TIME_INFINITE);
        ASSERT_EQ(status, ZX_OK, "completion wait failed!");
    }

    return 0;
}

static int sync_completion_thread_signal(void* arg) {
    auto completion = static_cast<sync_completion_t*>(arg);
    for (int iteration = 0u; iteration < ITERATIONS; iteration++) {
        sync_completion_reset(completion);
        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
        sync_completion_signal(completion);
    }

    return 0;
}

struct CompletionAndCounters {
    sync_completion_t completion;
    int started = 0;
    int finished = 0;
};

static int sync_completion_thread_wait_once(void* ctx) {
    auto cc = static_cast<CompletionAndCounters*>(ctx);
    __atomic_fetch_add(&cc->started, 1, __ATOMIC_SEQ_CST);
    zx_status_t status = sync_completion_wait(&cc->completion, ZX_TIME_INFINITE);
    ASSERT_EQ(status, ZX_OK, "completion wait failed!");
    __atomic_fetch_add(&cc->finished, 1, __ATOMIC_SEQ_CST);
    return 0;
}

static bool test_initializer(void) {
    BEGIN_TEST;
    // Let's not accidentally break .bss'd completions
    static sync_completion_t static_completion;
    sync_completion_t completion;
    int status = memcmp(&static_completion, &completion, sizeof(sync_completion_t));
    EXPECT_EQ(status, 0, "completion's initializer is not all zeroes");
    END_TEST;
}

#define NUM_THREADS 16

static bool test_completions(void) {
    BEGIN_TEST;
    sync_completion_t completion;
    thrd_t signal_thread;
    thrd_t wait_thread[NUM_THREADS];

    for (int idx = 0; idx < NUM_THREADS; idx++) {
        auto result = thrd_create_with_name(wait_thread + idx, sync_completion_thread_wait,
                                            &completion, "completion wait");
        ASSERT_EQ(result, thrd_success);
    }
    auto result = thrd_create_with_name(&signal_thread, sync_completion_thread_signal, &completion,
                                        "completion signal");
    ASSERT_EQ(result, thrd_success);

    for (int idx = 0; idx < NUM_THREADS; idx++) {
        ASSERT_EQ(thrd_join(wait_thread[idx], NULL), thrd_success);
    }
    ASSERT_EQ(thrd_join(signal_thread, NULL), thrd_success);

    END_TEST;
}

static bool test_timeout(void) {
    BEGIN_TEST;
    zx_time_t timeout = 0u;
    sync_completion_t completion;
    for (int iteration = 0; iteration < 1000; iteration++) {
        timeout += 2000u;
        zx_status_t status = sync_completion_wait(&completion, timeout);
        ASSERT_EQ(status, ZX_ERR_TIMED_OUT, "wait returned spuriously!");
    }
    END_TEST;
}

static bool is_blocked_on_futex(thrd_t thread) {
    zx_info_thread_t info;
    zx_status_t status = zx_object_get_info(thrd_get_zx_handle(thread), ZX_INFO_THREAD, &info,
                                            sizeof(info), nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK);
    return info.state == ZX_THREAD_STATE_BLOCKED_FUTEX;
}

static bool all_blocked_on_futex(thrd_t threads[], int num) {
    for (int i = 0; i < num; ++i) {
        if (!is_blocked_on_futex(threads[i])) {
            return false;
        }
    }
    return true;
}

// This test would flake if spurious wake ups from zx_futex_wake() were possible.
// However, the documentation states that "Zircon's implementation of
// futexes currently does not generate spurious wakeups itself". If this changes,
// this test could be relaxed to only assert that threads wake up in the end.
static bool test_signal_requeue() {
    BEGIN_TEST;
    CompletionAndCounters cc;

    thrd_t wait_thread[NUM_THREADS];
    for (int idx = 0; idx < NUM_THREADS; idx++) {
        auto result = thrd_create(wait_thread + idx, sync_completion_thread_wait_once, &cc);
        ASSERT_EQ(result, thrd_success);
    }

    // Make sure all threads have started
    while (__atomic_load_n(&cc.started, __ATOMIC_SEQ_CST) != NUM_THREADS) {
        sched_yield();
    }

    // Make sure all threads are blocking on a futex now
    while (!all_blocked_on_futex(wait_thread, NUM_THREADS)) {
        sched_yield();
    }

    zx_futex_t futex = 0;
    sync_completion_signal_requeue(&cc.completion, &futex);

    // The threads should still be blocked on a futex
    ASSERT_TRUE(all_blocked_on_futex(wait_thread, NUM_THREADS));

    // Wait for a bit and make sure no one has woken up yet
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    ASSERT_EQ(__atomic_load_n(&cc.finished, __ATOMIC_SEQ_CST), 0);

    // Now, wake the threads via the requeued futex
    zx_futex_wake(&futex, UINT32_MAX);

    // Now the threads should be done
    for (int idx = 0; idx < NUM_THREADS; idx++) {
        ASSERT_EQ(thrd_join(wait_thread[idx], NULL), thrd_success);
    }
    ASSERT_EQ(__atomic_load_n(&cc.finished, __ATOMIC_SEQ_CST), NUM_THREADS);

    END_TEST;
}

BEGIN_TEST_CASE(sync_completion_tests)
RUN_TEST(test_initializer)
RUN_TEST(test_completions)
RUN_TEST(test_timeout)
RUN_TEST(test_signal_requeue)
END_TEST_CASE(sync_completion_tests)
