// Copyright 2016 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <runtime/thread.h>

static mx_handle_t get_root_job(void) {
#ifdef BUILD_COMBINED_TESTS
    extern mx_handle_t root_job;
    return root_job;
#else
    // TODO(kulakowski) Get this from somewhere.
    return MX_HANDLE_INVALID;
#endif
}

static void test_thread_fn(void* arg) {
    // Note: You shouldn't use C standard library functions from this thread.
    mx_nanosleep(MX_MSEC(100));
    mx_thread_exit();
}

static void busy_thread_fn(void* arg) {
    volatile uint64_t i = 0;
    while (true) {
        ++i;
    }
}

static void sleep_thread_fn(void* arg) {
    mx_nanosleep(MX_TIME_INFINITE);
}

static void wait_thread_fn(void* arg) {
    mx_handle_t event = *(mx_handle_t*)arg;
    mx_handle_wait_one(event, MX_USER_SIGNAL_0, MX_TIME_INFINITE, NULL);
}

static bool start_thread(mxr_thread_entry_t entry, void* arg, mxr_thread_t** thread_out) {
    const size_t stack_size = 256u << 10;
    mx_handle_t thread_stack_vmo;
    ASSERT_EQ(mx_vmo_create(stack_size, 0, &thread_stack_vmo), NO_ERROR, "");
    ASSERT_GT(thread_stack_vmo, 0, "");

    uintptr_t stack = 0u;
    ASSERT_EQ(mx_vmar_map(mx_vmar_root_self(), 0, thread_stack_vmo, 0, stack_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &stack), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(thread_stack_vmo), NO_ERROR, "");

    ASSERT_EQ(mxr_thread_create("test_thread", thread_out), NO_ERROR, "");
    ASSERT_EQ(mxr_thread_start(*thread_out, stack, stack_size, entry, arg), NO_ERROR, "");
    return true;
}

static bool start_and_kill_thread(mxr_thread_entry_t entry, void* arg) {
    mxr_thread_t* thread = NULL;
    ASSERT_TRUE(start_thread(entry, arg, &thread), "");
    mx_nanosleep(MX_MSEC(100));
    ASSERT_EQ(mx_task_kill(mxr_thread_get_handle(thread)), NO_ERROR, "");
    ASSERT_EQ(mxr_thread_join(thread), NO_ERROR, "");
    return true;
}

static bool threads_test(void) {
    BEGIN_TEST;

    mxr_thread_t* thread = NULL;
    ASSERT_TRUE(start_thread(test_thread_fn, NULL, &thread), "");

    ASSERT_EQ(mx_handle_wait_one(mxr_thread_get_handle(thread), MX_THREAD_SIGNALED,
                                 MX_TIME_INFINITE, NULL), NO_ERROR, "");

    mxr_thread_destroy(thread);

    // Creating a thread with a super long name should fail.
    thread = NULL;
    EXPECT_LT(mxr_thread_create(
        "01234567890123456789012345678901234567890123456789012345678901234567890123456789",
        &thread), 0, "Thread creation should have failed (name too long)");

    END_TEST;
}

// mx_thread_start() is not supposed to be usable for creating a
// process's first thread.  That's what mx_process_start() is for.
// Check that mx_thread_start() returns an error in this case.
static bool test_thread_start_on_initial_thread(void) {
    BEGIN_TEST;

    static const char kProcessName[] = "Test process";
    static const char kThreadName[] = "Test thread";
    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t thread;
    ASSERT_EQ(mx_process_create(get_root_job(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");
    ASSERT_EQ(mx_thread_create(process, kThreadName, sizeof(kThreadName) - 1,
                               0, &thread), NO_ERROR, "");
    ASSERT_EQ(mx_thread_start(thread, 1, 1, 1, 1), ERR_BAD_STATE, "");

    ASSERT_EQ(mx_handle_close(thread), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Test that we don't get an assertion failure (and kernel panic) if we
// pass a zero instruction pointer when starting a thread (in this case via
// mx_process_start()).
static bool test_thread_start_with_zero_instruction_pointer(void) {
    BEGIN_TEST;

    static const char kProcessName[] = "Test process";
    static const char kThreadName[] = "Test thread";
    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t thread;
    ASSERT_EQ(mx_process_create(get_root_job(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");
    ASSERT_EQ(mx_thread_create(process, kThreadName, sizeof(kThreadName) - 1,
                               0, &thread), NO_ERROR, "");
    ASSERT_EQ(mx_process_start(process, thread, 0, 0, thread, 0), NO_ERROR, "");

    // Give crashlogger a little time to print info about the new thread
    // (since it will start and crash), otherwise that output gets
    // interleaved with the test runner's output.
    mx_nanosleep(MX_MSEC(100));

    ASSERT_EQ(mx_handle_close(process), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(vmar), NO_ERROR, "");

    END_TEST;
}

static bool test_kill_busy_thread(void) {
    BEGIN_TEST;

    ASSERT_TRUE(start_and_kill_thread(busy_thread_fn, NULL), "");

    END_TEST;
}

static bool test_kill_sleep_thread(void) {
    BEGIN_TEST;

    ASSERT_TRUE(start_and_kill_thread(sleep_thread_fn, NULL), "");

    END_TEST;
}

static bool test_kill_wait_thread(void) {
    BEGIN_TEST;

    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0, &event), NO_ERROR, "");
    ASSERT_TRUE(start_and_kill_thread(wait_thread_fn, &event), "");
    ASSERT_EQ(mx_handle_close(event), NO_ERROR, "");

    END_TEST;
}

BEGIN_TEST_CASE(threads_tests)
RUN_TEST(threads_test)
RUN_TEST(test_thread_start_on_initial_thread)
RUN_TEST(test_thread_start_with_zero_instruction_pointer)
RUN_TEST(test_kill_busy_thread)
RUN_TEST(test_kill_sleep_thread)
RUN_TEST(test_kill_wait_thread)
END_TEST_CASE(threads_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
