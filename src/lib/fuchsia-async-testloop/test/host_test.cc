// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-testing/test_loop.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

extern "C" {
// Sets |return_status| to zero when successfully run.
async_test_subloop_t* make_rust_loop(int* return_status);
}

namespace {

using RustGuestTest = gtest::TestLoopFixture;

TEST_F(RustGuestTest, Run) {
  int return_status = -1;
  auto subloop = test_loop().RegisterLoop(make_rust_loop(&return_status));
  ASSERT_TRUE(subloop);
  EXPECT_EQ(return_status, -1);
  RunLoopUntilIdle();
  EXPECT_EQ(return_status, -1);
  RunLoopUntil(zx::time(10000));
  EXPECT_EQ(return_status, 0);
}

}  // namespace
