// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/console/app.h"

#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace {

using App = gtest::TestLoopFixture;

llcpp::fuchsia::shell::Shell::SyncClient Client() {
  zx_handle_t client_ch = 0;
  zx::channel client_channel(client_ch);
  return llcpp::fuchsia::shell::Shell::SyncClient(std::move(client_channel));
}

TEST_F(App, BogusArgs) {
  const char* args[] = {"/boot/bin/cliff", "-w", nullptr};
  int quit_count = 0;

  ::llcpp::fuchsia::shell::Shell::SyncClient client = Client();
  shell::console::App app(&client, dispatcher());
  EXPECT_FALSE(app.Init(2, args, [&quit_count] { ++quit_count; }));
  EXPECT_EQ(0, quit_count);
}

TEST_F(App, SimpleDeclArg) {
  const char* args[] = {"/boot/bin/cliff", "-c", "var a = 1", nullptr};
  int quit_count = 0;
  llcpp::fuchsia::shell::Shell::SyncClient client = Client();
  shell::console::App app(&client, dispatcher());
  EXPECT_TRUE(app.Init(3, args, [&] { ++quit_count; }));
  EXPECT_EQ(1, quit_count);
}

}  // namespace
