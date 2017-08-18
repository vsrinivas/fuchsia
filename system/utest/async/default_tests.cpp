// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/default.h>

#include <threads.h>

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

bool get_set_default_test() {
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

} // namespace

BEGIN_TEST_CASE(default_tests)
RUN_TEST(get_set_default_test)
END_TEST_CASE(default_tests)
