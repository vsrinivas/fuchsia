// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/promise.h"

#include <gtest/gtest.h>

struct fake_context : fit::context {
  fit::executor* executor() const override {
    EXPECT_TRUE(false);
    return nullptr;
  }

  fit::suspended_task suspend_task() override { return fit::suspended_task(); }
};

// Test that driver::Continue() correctly returns fit::results, and can be
// resumed using driver::ContinueWith.
TEST(PromiseTest, Continue) {
  fake_context context;

  auto success = fit::make_promise(driver::Continue(
      [](driver::ContinueWith<fit::result<>>& with) -> fit::result<> { return fit::ok(); }));
  EXPECT_TRUE(success(context).is_ok());

  auto failure = fit::make_promise(driver::Continue(
      [](driver::ContinueWith<fit::result<>>& with) -> fit::result<> { return fit::error(); }));
  EXPECT_TRUE(failure(context).is_error());

  fit::function<void()> callback;
  auto pending = fit::make_promise(
      driver::Continue([&callback](driver::ContinueWith<fit::result<>>& with) -> fit::result<> {
        callback = [&with] { with.Return(fit::ok()); };
        return fit::pending();
      }));
  EXPECT_TRUE(pending(context).is_pending());
  callback();
  EXPECT_TRUE(pending(context).is_ok());
}
