// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/app.h"

#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace {

using App = gtest::TestLoopFixture;

TEST_F(App, Quit) {
  int quit_count = 0;
  cmd::App app(dispatcher());
  app.Init([&] { ++quit_count; });
  cmd::Command command;
  command.Parse("quit");
  EXPECT_EQ(0, quit_count);
  app.OnConsoleCommand(std::move(command));
  EXPECT_EQ(1, quit_count);
}

}  // namespace
