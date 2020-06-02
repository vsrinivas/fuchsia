// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/default.h>

#include <threads.h>

#include <lib/async-testing/dispatcher_stub.h>
#include <zxtest/zxtest.h>

namespace {

int default_test_thread(void*) {
  EXPECT_NULL(async_get_default_dispatcher(), "other thread's default is initially null");

  async::DispatcherStub async;
  async_set_default_dispatcher(&async);
  EXPECT_EQ(&async, async_get_default_dispatcher(), "other thread's default can be changed");
  return 1;
}

TEST(DefaultTests, get_set_default_test) {
  // Default is initially null.
  EXPECT_NULL(async_get_default_dispatcher(), "default is initially null");

  // Default can be changed.
  async::DispatcherStub async;
  async_set_default_dispatcher(&async);
  EXPECT_EQ(&async, async_get_default_dispatcher(), "default can be changed");

  // Default is thread-local.
  thrd_t thread;
  ASSERT_EQ(thrd_success, thrd_create(&thread, default_test_thread, nullptr), "thrd_create");
  int result;
  ASSERT_EQ(thrd_success, thrd_join(thread, &result), "thrd_join");
  EXPECT_EQ(1, result, "other thread has its own default");
  EXPECT_EQ(&async, async_get_default_dispatcher(), "this thread's default is unchanged");

  async_set_default_dispatcher(nullptr);
}

}  // namespace
