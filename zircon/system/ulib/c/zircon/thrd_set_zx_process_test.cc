// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <zircon/process.h>
#include <zircon/threads.h>

#include <zxtest/zxtest.h>

TEST(ThrdSetZxProcessTest, SetBasic) {
  zx_handle_t previous = thrd_set_zx_process(ZX_HANDLE_INVALID);
  auto reset_handle = fit::defer([previous]() { thrd_set_zx_process(previous); });

  EXPECT_EQ(previous, zx_process_self());

  previous = thrd_set_zx_process(zx_process_self());
  EXPECT_EQ(previous, ZX_HANDLE_INVALID);
}

TEST(ThrdSetZxProcessTest, SetInvalidAndCreate) {
  // Create a new thread with the default process handle.
  thrd_t t1;
  ASSERT_EQ(thrd_create(
                &t1, [](void* arg) { return 0; }, nullptr),
            thrd_success);

  int result;
  ASSERT_EQ(thrd_join(t1, &result), thrd_success);

  // Create a new thread with an invalid process handle.
  zx_handle_t previous = thrd_set_zx_process(ZX_HANDLE_INVALID);
  auto reset_handle = fit::defer([previous]() { thrd_set_zx_process(previous); });

  thrd_t t2;
  ASSERT_EQ(thrd_create(
                &t2, [](void* arg) { return 0; }, nullptr),
            thrd_nomem);
}
