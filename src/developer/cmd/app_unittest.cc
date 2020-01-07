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

TEST_F(App, InitializePWD) {
  const char* args[] = {"/boot/bin/cmd", nullptr};
  int quit_count = 0;
  cmd::App app(dispatcher());

  unsetenv("PWD");
  EXPECT_TRUE(app.Init(1, args, [&] { ++quit_count; }));
  EXPECT_NE(nullptr, getenv("PWD"));
}

TEST_F(App, Quit) {
  const char* args[] = {"/boot/bin/cmd", nullptr};
  int quit_count = 0;
  cmd::App app(dispatcher());
  EXPECT_TRUE(app.Init(1, args, [&] { ++quit_count; }));
  cmd::Command command;
  command.Parse("quit");
  EXPECT_EQ(0, quit_count);
  app.OnConsoleCommand(std::move(command));
  EXPECT_EQ(1, quit_count);
}

TEST_F(App, BogusArgs) {
  const char* args[] = {"/boot/bin/cmd", "-w", nullptr};
  int quit_count = 0;
  cmd::App app(dispatcher());
  EXPECT_FALSE(app.Init(2, args, [&] { ++quit_count; }));
  EXPECT_EQ(0, quit_count);
}

TEST_F(App, CommandGetenvArg) {
  const char* args[] = {"/boot/bin/cmd", "-c", "getenv PWD", nullptr};
  int quit_count = 0;
  cmd::App app(dispatcher());
  EXPECT_TRUE(app.Init(3, args, [&] { ++quit_count; }));
  EXPECT_EQ(1, quit_count);
}

TEST_F(App, CommandQuitArg) {
  const char* args[] = {"/boot/bin/cmd", "-c", "quit", nullptr};
  int quit_count = 0;
  cmd::App app(dispatcher());
  EXPECT_TRUE(app.Init(3, args, [&] { ++quit_count; }));
  EXPECT_EQ(1, quit_count);
}

}  // namespace
