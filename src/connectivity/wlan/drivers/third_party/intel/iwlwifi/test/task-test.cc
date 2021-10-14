// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/test_loop.h>
#include <zircon/time.h>

#include <memory>
#include <thread>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/task-internal.h"

namespace wlan::testing {
namespace {

TEST(TaskTest, TaskLifecycle) {
  auto test_loop = std::make_unique<::async::TestLoop>();
  bool called = false;
  auto task = std::make_unique<::wlan::iwlwifi::TaskInternal>(
      test_loop->dispatcher(), [](void* data) { *reinterpret_cast<bool*>(data) = true; }, &called);

  EXPECT_FALSE(called);
  EXPECT_OK(task->Post(ZX_MSEC(1)));
  EXPECT_FALSE(called);

  test_loop->RunFor(zx::usec(500));
  EXPECT_FALSE(called);

  test_loop->RunFor(zx::usec(500));
  EXPECT_TRUE(called);

  called = false;
  task.reset();
  EXPECT_FALSE(called);
}

TEST(TaskTest, RepostTask) {
  auto test_loop = std::make_unique<::async::TestLoop>();
  bool called = false;
  auto task = std::make_unique<::wlan::iwlwifi::TaskInternal>(
      test_loop->dispatcher(), [](void* data) { *reinterpret_cast<bool*>(data) = true; }, &called);

  EXPECT_OK(task->Post(ZX_MSEC(1)));
  EXPECT_FALSE(called);

  test_loop->RunFor(zx::usec(500));
  EXPECT_FALSE(called);

  // Repost the task with a later deadline.
  EXPECT_OK(task->Post(ZX_MSEC(1)));
  EXPECT_FALSE(called);

  // This will pass the original deadline.
  test_loop->RunFor(zx::usec(500));
  EXPECT_FALSE(called);

  // This will pass the new deadline.
  test_loop->RunFor(zx::usec(500));
  EXPECT_TRUE(called);
}

TEST(TaskTest, CancelTask) {
  auto test_loop = std::make_unique<::async::TestLoop>();
  bool called = false;
  auto task = std::make_unique<::wlan::iwlwifi::TaskInternal>(
      test_loop->dispatcher(), [](void* data) { *reinterpret_cast<bool*>(data) = true; }, &called);

  EXPECT_OK(task->Post(ZX_MSEC(1)));
  EXPECT_FALSE(called);

  test_loop->RunFor(zx::usec(500));
  EXPECT_FALSE(called);

  // Cancel the task.
  EXPECT_OK(task->CancelSync());
  EXPECT_FALSE(called);
  test_loop->RunFor(zx::usec(500));
  EXPECT_FALSE(called);

  // Cancelling the task a second time will not find the task.
  EXPECT_EQ(ZX_ERR_NOT_FOUND, task->Cancel());
  EXPECT_FALSE(called);
}

TEST(TaskTest, WaitTask) {
  auto test_loop = std::make_unique<::async::TestLoop>();
  bool called = false;
  auto task = std::make_unique<::wlan::iwlwifi::TaskInternal>(
      test_loop->dispatcher(), [](void* data) { *reinterpret_cast<bool*>(data) = true; }, &called);

  bool run_waited = false;
  EXPECT_OK(task->Post(ZX_MSEC(1)));
  std::thread run_wait_thread([&]() {
    EXPECT_OK(task->Wait());
    run_waited = true;
  });
  EXPECT_FALSE(called);
  EXPECT_FALSE(run_waited);

  test_loop->RunFor(zx::usec(500));
  EXPECT_FALSE(called);
  EXPECT_FALSE(run_waited);

  // Once the full deadline has passed, the wait will unblock and complete.
  test_loop->RunFor(zx::usec(500));
  run_wait_thread.join();
  EXPECT_TRUE(called);
  EXPECT_TRUE(run_waited);

  called = false;
  bool cancel_waited = false;
  EXPECT_OK(task->Post(ZX_MSEC(1)));
  std::thread cancel_wait_thread([&]() {
    EXPECT_OK(task->Wait());
    cancel_waited = true;
  });
  EXPECT_FALSE(called);
  EXPECT_FALSE(cancel_waited);

  test_loop->RunFor(zx::usec(500));
  EXPECT_FALSE(called);
  EXPECT_FALSE(cancel_waited);

  // Once the task has been cancelled, the wait will unblock and complete.
  EXPECT_OK(task->CancelSync());
  cancel_wait_thread.join();
  EXPECT_FALSE(called);
  EXPECT_TRUE(cancel_waited);
}

}  // namespace
}  // namespace wlan::testing
