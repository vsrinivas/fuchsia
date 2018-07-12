// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/environment/environment.h"

#include <lib/gtest/test_loop_fixture.h>

namespace ledger {
namespace {

using EnvironmentTest = ::gtest::TestLoopFixture;

TEST_F(EnvironmentTest, InitializationOfAsync) {
  Environment env = EnvironmentBuilder().SetAsync(dispatcher()).Build();

  EXPECT_EQ(dispatcher(), env.dispatcher());
  EXPECT_EQ(nullptr, env.io_dispatcher());
}

TEST_F(EnvironmentTest, InitializationOfAsyncAndIOAsync) {
  Environment env = EnvironmentBuilder()
                        .SetAsync(dispatcher())
                        .SetIOAsync(dispatcher())
                        .Build();

  EXPECT_EQ(dispatcher(), env.dispatcher());
  EXPECT_EQ(dispatcher(), env.io_dispatcher());
}

}  // namespace
}  // namespace ledger
