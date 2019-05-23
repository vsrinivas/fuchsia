// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon-internal/default_stack_size.h>
#include <pthread.h>
#include <zxtest/zxtest.h>

namespace {

#ifdef STACK_TEST_EXPECTED_SIZE
constexpr size_t kExpectedSize = STACK_TEST_EXPECTED_SIZE;
static_assert(kExpectedSize != ZIRCON_DEFAULT_STACK_SIZE);
#else
constexpr size_t kExpectedSize = ZIRCON_DEFAULT_STACK_SIZE;
#endif

void FetchStackSize(pthread_t thread, size_t* out_size) {
    pthread_attr_t attr;
    ASSERT_EQ(0, pthread_getattr_np(thread, &attr));
    ASSERT_EQ(0, pthread_attr_getstacksize(&attr, out_size));
}

TEST(StackSizeTests, MainThreadStackSize) {
    size_t size;
    ASSERT_NO_FATAL_FAILURES(FetchStackSize(pthread_self(), &size),
                             "Cannot retrieve main thread's stack size");
    EXPECT_EQ(kExpectedSize, size);
}

TEST(StackSizeTests, NewThreadStackSize) {
    pthread_t th;
    ASSERT_EQ(0, pthread_create(
                     &th, nullptr,
                     [](void*) -> void* { return nullptr; },
                     nullptr));

    size_t size;
    ASSERT_NO_FATAL_FAILURES(FetchStackSize(th, &size),
                             "Cannot retrieve new thread's stack size");
    EXPECT_EQ(kExpectedSize, size);

    void* result = &result;
    EXPECT_EQ(0, pthread_join(th, &result));
    EXPECT_NULL(result);
}

} // anonymous namespace
