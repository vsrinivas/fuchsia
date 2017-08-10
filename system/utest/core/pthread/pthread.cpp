// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int process_waked = 0;
int thread_with_lock = 0;

static void log(const char* str) {
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    unittest_printf("[%08lu.%08lu]: %s", time.tv_sec, time.tv_nsec / 1000, str);
}

static void* mutex_thread_1(void* arg) {
    log("thread 1 grabbing mutex\n");
    pthread_mutex_lock(&mutex);
    log("thread 1 got mutex\n");
    thread_with_lock = 1;
    mx_nanosleep(mx_deadline_after(MX_MSEC(300)));

    // Make sure no other thread woke up
    EXPECT_EQ(thread_with_lock, 1, "Only thread 1 should have woken up");
    log("thread 1 releasing mutex\n");
    pthread_mutex_unlock(&mutex);
    log("thread 1 done\n");
    return NULL;
}

static void* mutex_thread_2(void* arg) {
    mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
    log("thread 2 grabbing mutex\n");
    pthread_mutex_lock(&mutex);
    log("thread 2 got mutex\n");
    thread_with_lock = 2;

    mx_nanosleep(mx_deadline_after(MX_MSEC(300)));

    // Make sure no other thread woke up
    EXPECT_EQ(thread_with_lock, 2, "Only thread 2 should have woken up");

    log("thread 2 releasing mutex\n");
    pthread_mutex_unlock(&mutex);
    log("thread 2 done\n");
    return NULL;
}

static void* mutex_thread_3(void* arg) {
    mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
    log("thread 3 grabbing mutex\n");
    pthread_mutex_lock(&mutex);
    log("thread 3 got mutex\n");
    thread_with_lock = 3;

    mx_nanosleep(mx_deadline_after(MX_MSEC(300)));

    // Make sure no other thread woke up
    EXPECT_EQ(thread_with_lock, 3, "Only thread 3 should have woken up");

    log("thread 3 releasing mutex\n");
    pthread_mutex_unlock(&mutex);
    log("thread 3 done\n");
    return NULL;
}

static void* cond_thread1(void* arg) {
    pthread_mutex_lock(&mutex);
    log("thread 1 waiting on condition\n");
    pthread_cond_wait(&cond, &mutex);
    log("thread 2 waiting again\n");
    pthread_cond_wait(&cond, &mutex);
    process_waked++;
    pthread_mutex_unlock(&mutex);
    log("thread 1 done\n");
    return NULL;
}

static void* cond_thread2(void* arg) {
    pthread_mutex_lock(&mutex);
    log("thread 2 waiting on condition\n");
    pthread_cond_wait(&cond, &mutex);
    log("thread 2 waiting again\n");
    pthread_cond_wait(&cond, &mutex);
    process_waked++;
    pthread_mutex_unlock(&mutex);
    log("thread 2 done\n");
    return NULL;
}

static void* cond_thread3(void* arg) {
    pthread_mutex_lock(&mutex);
    log("thread 3 waiting on condition\n");
    pthread_cond_wait(&cond, &mutex);
    log("thread 3 waiting again\n");
    pthread_cond_wait(&cond, &mutex);
    process_waked++;
    pthread_mutex_unlock(&mutex);
    log("thread 3 done\n");
    return NULL;
}

bool pthread_test(void) {

    BEGIN_TEST;
    pthread_t thread1, thread2, thread3;

    log("testing uncontested case\n");
    pthread_mutex_lock(&mutex);
    pthread_mutex_unlock(&mutex);
    log("mutex locked and unlocked\n");

    log("starting cond threads\n");
    pthread_create(&thread1, NULL, cond_thread1, NULL);
    pthread_create(&thread2, NULL, cond_thread2, NULL);
    pthread_create(&thread3, NULL, cond_thread3, NULL);

    mx_nanosleep(mx_deadline_after(MX_MSEC(300)));

    log("calling pthread_cond_broadcast\n");
    pthread_cond_broadcast(&cond);

    mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
    log("calling pthread_cond_signal\n");
    pthread_cond_signal(&cond);
    mx_nanosleep(mx_deadline_after(MX_MSEC(300)));
    EXPECT_EQ(process_waked, 1, "Only 1 process should have woken up");

    log("calling pthread_cond_signal\n");
    pthread_cond_signal(&cond);
    mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
    EXPECT_EQ(process_waked, 2, "Only 2 processes should have woken up");

    log("calling pthread_cond_signal\n");
    pthread_cond_signal(&cond);
    mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
    EXPECT_EQ(process_waked, 3, "Only 3 processes should have woken up");

    log("joining cond threads\n");
    pthread_join(thread1, NULL);
    log("cond_thread 1 joined\n");
    pthread_join(thread2, NULL);
    log("cond_thread 2 joined\n");
    pthread_join(thread3, NULL);
    log("cond_thread 3 joined\n");

    pthread_mutex_lock(&mutex);
    log("waiting on condition with 2 second timeout\n");
    struct timespec delay;
    clock_gettime(CLOCK_REALTIME, &delay);
    delay.tv_sec += 2;
    int result = pthread_cond_timedwait(&cond, &mutex, &delay);
    pthread_mutex_unlock(&mutex);
    log("pthread_cond_timedwait returned\n");
    unittest_printf("pthread_cond_timedwait result: %d\n", result);

    EXPECT_EQ(result, ETIMEDOUT, "Lock should have timeout");

    log("creating mutex threads\n");
    pthread_create(&thread1, NULL, mutex_thread_1, NULL);
    pthread_create(&thread2, NULL, mutex_thread_2, NULL);
    pthread_create(&thread3, NULL, mutex_thread_3, NULL);

    log("joining mutex threads\n");
    pthread_join(thread1, NULL);
    log("thread 1 joined\n");
    pthread_join(thread2, NULL);
    log("thread 2 joined\n");
    pthread_join(thread3, NULL);
    log("thread 3 joined\n");

    END_TEST;
}

bool pthread_self_main_thread_test(void) {
    BEGIN_TEST;
    pthread_t self = pthread_self();
    pthread_t null = 0;
    ASSERT_NE(self, null, "pthread_self() was NULL");
    END_TEST;
}

// Pick a stack size well bigger than the default, which is <1MB.
constexpr size_t stack_size = 16u << 20;

static bool big_stack_check() {
    // Stack allocate a lot, but less than the full stack size.
    volatile uint8_t buffer[stack_size / 2];
    uint8_t value = 0u;
    for (auto& byte : buffer)
        byte = value++;

    uint64_t sum = 0u;
    uint64_t expected_sum = 0u;
    value = 0u;
    for (auto& byte : buffer) {
        sum += byte;
        expected_sum += value++;
    }

    ASSERT_EQ(sum, expected_sum, "buffer corrupted");

    return true;
}

static void* bigger_stack_thread(void* unused) {
    big_stack_check();
    return nullptr;
}

static bool pthread_big_stack_size() {
    BEGIN_TEST;

    pthread_t thread;
    pthread_attr_t attr;
    int result = pthread_attr_init(&attr);
    ASSERT_EQ(result, 0, "failed to initialize pthread attributes");
    result = pthread_attr_setstacksize(&attr, stack_size);
    ASSERT_EQ(result, 0, "failed to set stack size");
    result = pthread_create(&thread, &attr, bigger_stack_thread, nullptr);
    ASSERT_EQ(result, 0, "failed to start thread");
    result = pthread_join(thread, nullptr);
    ASSERT_EQ(result, 0, "failed to join thread");

    END_TEST;
}

static bool pthread_getstack_check() {
    BEGIN_TEST;

    pthread_attr_t attr;
    int result = pthread_getattr_np(pthread_self(), &attr);
    ASSERT_EQ(result, 0, "pthread_getattr_np failed");

    void* stack_base;
    size_t stack_size;
    result = pthread_attr_getstack(&attr, &stack_base, &stack_size);
    ASSERT_EQ(result, 0, "pthread_attr_getstack failed");

    // Convert the reported bounds of the stack into something we can
    // compare against.
    uintptr_t low = reinterpret_cast<uintptr_t>(stack_base);
    uintptr_t high = low + stack_size;

    // This is just some arbitrary address known to be on our thread stack.
    // Note this is the "safe stack".  If using -fsanitize=safe-stack,
    // there is also an "unsafe stack", where e.g. &attr will reside.
    uintptr_t here = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));

    unittest_printf("pthread_attr_getstack reports [%#" PRIxPTR
                    ", %#" PRIxPTR "); SP ~= %#" PRIxPTR "\n",
                    low, high, here);

    ASSERT_LT(low, here, "reported stack base not below actual SP");
    ASSERT_GT(high, here, "reported stack end not above actual SP");

    END_TEST;
}

static bool pthread_getstack_main_thread() {
    BEGIN_TEST;

    ASSERT_TRUE(pthread_getstack_check(),
                "pthread_attr_getstack on main thread");

    END_TEST;
}

static void* getstack_thread(void*) {
    bool ok = pthread_getstack_check();
    return reinterpret_cast<void*>(static_cast<uintptr_t>(ok));
}

static bool pthread_getstack_on_new_thread(const pthread_attr_t* attr) {
    BEGIN_TEST;

    pthread_t thread;
    int result = pthread_create(&thread, attr, &getstack_thread, nullptr);
    ASSERT_EQ(result, 0, "pthread_create failed");
    void* thread_result;
    result = pthread_join(thread, &thread_result);
    ASSERT_EQ(result, 0, "pthread_join failed");

    bool ok = static_cast<bool>(reinterpret_cast<uintptr_t>(thread_result));
    ASSERT_TRUE(ok, "pthread_attr_getstack on another thread");

    END_TEST;
}

static bool pthread_getstack_other_thread() {
    return pthread_getstack_on_new_thread(nullptr);
}

static bool pthread_getstack_other_thread_explicit_size() {
    BEGIN_TEST;

    pthread_attr_t attr;
    int result = pthread_attr_init(&attr);
    ASSERT_EQ(result, 0, "pthread_attr_init failed");
    result = pthread_attr_setstacksize(&attr, 1 << 20);
    ASSERT_EQ(result, 0, "pthread_attr_setstacksize failed");

    ASSERT_TRUE(pthread_getstack_on_new_thread(&attr),
                "pthread_attr_getstack on a thread with explicit stack size");

    END_TEST;
}

BEGIN_TEST_CASE(pthread_tests)
RUN_TEST(pthread_test)
RUN_TEST(pthread_self_main_thread_test)
RUN_TEST(pthread_big_stack_size)
RUN_TEST(pthread_getstack_main_thread)
RUN_TEST(pthread_getstack_other_thread)
END_TEST_CASE(pthread_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
