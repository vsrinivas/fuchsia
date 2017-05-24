// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <async/async.h>
#include <unittest/unittest.h>

#include "async_stub.h"

namespace {

int default_test_thread(void*) {
    BEGIN_TEST;

    EXPECT_NULL(async_get_default(), "other thread's default is initially null");

    AsyncStub async;
    async_set_default(&async);
    EXPECT_EQ(&async, async_get_default(), "other thread's default can be changed");

    END_TEST;
    return 1;
}

bool default_test() {
    BEGIN_TEST;

    // Default is initially null.
    EXPECT_NULL(async_get_default(), "default is initially null");

    // Default can be changed.
    AsyncStub async;
    async_set_default(&async);
    EXPECT_EQ(&async, async_get_default(), "default can be changed");

    // Default is thread-local.
    thrd_t thread;
    ASSERT_EQ(thrd_success, thrd_create(&thread, default_test_thread, nullptr), "thrd_create");
    int result;
    ASSERT_EQ(thrd_success, thrd_join(thread, &result), "thrd_join");
    EXPECT_EQ(1, result, "other thread has its own default");
    EXPECT_EQ(&async, async_get_default(), "this thread's default is unchanged");

    async_set_default(nullptr);
    END_TEST;
}

bool begin_wait_test() {
    BEGIN_TEST;

    AsyncStub async;
    async_wait_t wait{};
    EXPECT_EQ(MX_ERR_NOT_SUPPORTED, async_begin_wait(&async, &wait), "valid args");

    END_TEST;
}

bool cancel_wait_test() {
    BEGIN_TEST;

    AsyncStub async;
    async_wait_t wait{};
    EXPECT_EQ(MX_ERR_NOT_SUPPORTED, async_cancel_wait(&async, &wait), "valid args");

    END_TEST;
}

bool post_task_test() {
    BEGIN_TEST;

    AsyncStub async;
    async_task_t task{};
    EXPECT_EQ(MX_ERR_NOT_SUPPORTED, async_post_task(&async, &task), "valid args");

    END_TEST;
}

bool cancel_task_test() {
    BEGIN_TEST;

    AsyncStub async;
    async_task_t task{};
    EXPECT_EQ(MX_ERR_NOT_SUPPORTED, async_cancel_task(&async, &task), "valid args");

    END_TEST;
}

bool queue_packet_test() {
    BEGIN_TEST;

    AsyncStub async;
    async_receiver_t receiver{};
    EXPECT_EQ(MX_ERR_NOT_SUPPORTED, async_queue_packet(&async, &receiver, nullptr), "valid args without data");
    mx_packet_user_t data;
    EXPECT_EQ(MX_ERR_NOT_SUPPORTED, async_queue_packet(&async, &receiver, &data), "valid args with data");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(async_tests)
RUN_TEST(default_test)
RUN_TEST(begin_wait_test)
RUN_TEST(cancel_wait_test)
RUN_TEST(post_task_test)
RUN_TEST(cancel_task_test)
RUN_TEST(queue_packet_test)
END_TEST_CASE(async_tests)
