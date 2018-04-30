// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "run_or_post.h"

#include <lib/async-testutils/test_loop.h>

#include "gtest/gtest.h"

namespace btlib {
namespace common {
namespace {

TEST(RunOrPostTest, WithoutDispatcher) {
  bool run = false;
  RunOrPost([&run] { run = true; }, nullptr);
  EXPECT_TRUE(run);
}

TEST(RunOrPostTest, WithDispatcher) {
  async::TestLoop loop;

  bool run = false;
  RunOrPost([&run] { run = true; }, loop.async());
  EXPECT_FALSE(run);

  loop.RunUntilIdle();
  EXPECT_TRUE(run);
}

}  // namespace
}  // namespace common
}  // namespace btlib
