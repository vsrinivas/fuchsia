// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fsl/tasks/fd_waiter.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-testing/test_loop.h>
#include <lib/fit/defer.h>
#include <poll.h>

#include <gtest/gtest.h>

namespace fsl {
namespace {

// Test disabled because it's hanging.
TEST(FDWaiter, DISABLED_WaitStdOut) {
  async::TestLoop loop;

  FDWaiter waiter;
  EXPECT_TRUE(waiter.Wait(
      [&](zx_status_t status, uint32_t events) {
        EXPECT_EQ(ZX_OK, status);
        EXPECT_TRUE(events & POLLOUT);
        loop.Quit();
      },
      STDOUT_FILENO, POLLOUT));

  loop.RunUntilIdle();
}

TEST(FDWaiter, WaitFailed) {
  async::TestLoop loop;
  FDWaiter waiter;
  EXPECT_FALSE(waiter.Wait([](zx_status_t status, uint32_t events) {}, -1, POLLOUT));
}

// Verify that FDWaiter can be used with a separate loop thread.
TEST(FDWaiter, UseLoopThread) {
  async::Loop async_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async_loop.StartThread("UseLoopThread");
  auto cleanup = fit::defer([&async_loop] { async_loop.Shutdown(); });

  fsl::FDWaiter fd_waiter(async_loop.dispatcher());
  std::atomic<bool> stdout_is_writable{false};
  ASSERT_TRUE(fd_waiter.Wait(
      [&stdout_is_writable](zx_status_t, uint32_t) { stdout_is_writable.store(true); }, 1,
      POLLOUT));
  while (!stdout_is_writable.load()) {
    zx::nanosleep(zx::deadline_after(zx::usec(100)));
  }
}

// Verify that we don't deadlock when destroying an FDWaiter containing callback whose destructor
// invokes FDWaiter::Cancel.
TEST(FDWaiter, DtorCancelDeadlock) {
  async::Loop async_loop(&kAsyncLoopConfigAttachToCurrentThread);
  std::atomic<bool> about_to_call_cancel = false;

  {
    fsl::FDWaiter fd_waiter(async_loop.dispatcher());

    // Create a callback object that upon destruction calls FDWaiter::Cancel.
    auto cancel_on_destruction = fit::defer([&about_to_call_cancel, &fd_waiter]() {
      about_to_call_cancel.store(true);
      fd_waiter.Cancel();
    });
    auto callback = [cod = std::move(cancel_on_destruction)](zx_status_t, uint32_t) {
      // This callback will never be invoked, but when it is destroyed, |cod| will be destroyed,
      // thereby invoking FDWaiter::Cancel.
      abort();
    };

    // The callback will never execute because stdout (1) will never become readable.
    ASSERT_TRUE(fd_waiter.Wait(std::move(callback), 1, POLLIN));

    // See that Cancel hasn't been called.  Once |fd_waiter| goes out of scope, the callback will be
    // destroyed and Cancel will be called in the context of FDWaiter's dtor.
    ASSERT_FALSE(about_to_call_cancel.load());
  }

  ASSERT_TRUE(about_to_call_cancel.load());
}

}  // namespace
}  // namespace fsl
