// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <magenta/syscalls.h>
#include <pthread.h>
#include <runtime/tls.h>
#include <stdint.h>
#include <threads.h>
#include <unistd.h>
#include <unittest/unittest.h>

// We request one-page stacks, so collisions are easy to catch.
static uintptr_t page_of(const void* ptr) {
    return (uintptr_t)ptr & -PAGE_SIZE;
}

static bool do_stack_tests(bool one_page_stack) {
    BEGIN_TEST;

    const void* safe_stack = __builtin_frame_address(0);

    // The compiler sees this pointer escape, so it should know
    // that this belongs on the unsafe stack.
    char unsafe_stack[64];
    (void)mx_system_get_version(unsafe_stack, sizeof(unsafe_stack));

    // Likewise, the tls_buf is used.
    static thread_local char tls_buf[64];
    (void)mx_system_get_version(tls_buf, sizeof(tls_buf));

    const void* tp = mxr_tp_get();

    EXPECT_NONNULL(environ, "environ unset");
    EXPECT_NONNULL(safe_stack, "CFA is null");
    EXPECT_NONNULL(unsafe_stack, "local's taken address is null");
    EXPECT_NONNULL(tls_buf, "thread_local's taken address is null");
    EXPECT_NONNULL(tp, "thread pointer is null");

    EXPECT_NE(page_of(safe_stack), page_of(environ),
              "safe stack collides with environ");

    EXPECT_NE(page_of(unsafe_stack), page_of(environ),
              "unsafe stack collides with environ");

    EXPECT_NE(page_of(tls_buf), page_of(environ),
              "TLS collides with environ");

    EXPECT_NE(page_of(tls_buf), page_of(safe_stack),
              "TLS collides with safe stack");

    EXPECT_NE(page_of(tls_buf), page_of(unsafe_stack),
              "TLS collides with unsafe stack");

    EXPECT_NE(page_of(tp), page_of(environ),
              "thread pointer collides with environ");

    EXPECT_NE(page_of(tp), page_of(safe_stack),
              "thread pointer collides with safe stack");

    EXPECT_NE(page_of(tp), page_of(unsafe_stack),
              "thread pointer collides with unsafe stack");

#ifdef __clang__
# if __has_feature(safe_stack)
    const void* unsafe_start = __builtin___get_unsafe_stack_start();
    const void* unsafe_ptr = __builtin___get_unsafe_stack_ptr();

    if (one_page_stack) {
        EXPECT_EQ(page_of(unsafe_start), page_of(unsafe_ptr),
                  "reported unsafe start and ptr not nearby");
    }

    EXPECT_EQ(page_of(unsafe_stack), page_of(unsafe_ptr),
              "unsafe stack and reported ptr not nearby");

    EXPECT_NE(page_of(unsafe_stack), page_of(safe_stack),
              "unsafe stack collides with safe stack");
# endif
#endif

    END_TEST;
}

// This instance of the test is lossy, because it's possible
// one of our single stacks spans multiple pages.  We can't
// get the main thread's stack down to a single page because
// the unittest machinery needs more than that.
static bool main_thread_stack_tests(void) {
    return do_stack_tests(false);
}

static void* thread_stack_tests(void* arg) {
    return (void*)(uintptr_t)do_stack_tests(true);
}

// Spawn a thread with a one-page stack.
static bool other_thread_stack_tests(void) {
    BEGIN_TEST;

    EXPECT_LE(PTHREAD_STACK_MIN, PAGE_SIZE, "");

    pthread_attr_t attr;
    ASSERT_EQ(0, pthread_attr_init(&attr), "");
    ASSERT_EQ(0, pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN), "");
    pthread_t thread;
    ASSERT_EQ(0, pthread_create(&thread, &attr, &thread_stack_tests, 0), "");
    void* result;
    ASSERT_EQ(0, pthread_join(thread, &result), "");
    bool other_thread_ok = (uintptr_t)result;
    EXPECT_TRUE(other_thread_ok, "");

    END_TEST;
}

BEGIN_TEST_CASE(stack_tests)
RUN_TEST(main_thread_stack_tests)
RUN_TEST(other_thread_stack_tests)
END_TEST_CASE(stack_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
