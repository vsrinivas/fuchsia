// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/promise.h"

#include <gtest/gtest.h>

#include "src/devices/lib/driver2/test_base.h"

// Test that driver::Continue() correctly returns fpromise::results, and can be
// resumed using driver::ContinueWith.
TEST(PromiseTest, Continue) {
  driver::testing::FakeContext context;

  auto success = fpromise::make_promise(
      driver::Continue([](driver::ContinueWith<fpromise::result<>>& with) -> fpromise::result<> {
        return fpromise::ok();
      }));
  EXPECT_TRUE(success(context).is_ok());

  auto failure = fpromise::make_promise(
      driver::Continue([](driver::ContinueWith<fpromise::result<>>& with) -> fpromise::result<> {
        return fpromise::error();
      }));
  EXPECT_TRUE(failure(context).is_error());

  fit::function<void()> callback;
  auto pending = fpromise::make_promise(driver::Continue(
      [&callback](driver::ContinueWith<fpromise::result<>>& with) -> fpromise::result<> {
        callback = [&with] { with.Return(fpromise::ok()); };
        return fpromise::pending();
      }));
  EXPECT_TRUE(pending(context).is_pending());
  callback();
  EXPECT_TRUE(pending(context).is_ok());
}
