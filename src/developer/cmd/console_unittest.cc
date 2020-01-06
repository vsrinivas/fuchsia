// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/console.h"

#include <lib/fdio/fd.h>
#include <lib/fit/function.h>
#include <lib/zx/socket.h>
#include <zircon/status.h>

#include <utility>

#include <fbl/unique_fd.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace {

using Console = gtest::RealLoopFixture;

class CallbackClient : public cmd::Console::Client {
 public:
  ~CallbackClient() override = default;

  zx_status_t OnConsoleCommand(cmd::Command command) override {
    return console_command_callback(std::move(command));
  }
  void OnConsoleError(zx_status_t status) override { console_error_callback(status); }
  void OnConsoleAutocomplete(cmd::Autocomplete* autocomplete) override {
    return console_autocomplete_callback(autocomplete);
  }

  fit::function<zx_status_t(cmd::Command)> console_command_callback;
  fit::function<void(zx_status_t status)> console_error_callback;
  fit::function<void(cmd::Autocomplete* autocomplete)> console_autocomplete_callback;
};

TEST_F(Console, Control) {
  zx::socket h0, h1;
  EXPECT_EQ(ZX_OK, zx::socket::create(0, &h0, &h1));

  fbl::unique_fd input_fd;
  EXPECT_EQ(ZX_OK, fdio_fd_create(h1.release(), input_fd.reset_and_get_address()));

  auto unexpected_command_callback = [](cmd::Command command) {
    EXPECT_TRUE(false) << "OnConsoleCommand called unexpectedly";
    return ZX_ERR_BAD_STATE;
  };
  auto unexpected_error_callback = [](zx_status_t status) {
    EXPECT_TRUE(false) << "OnConsoleError called unexpectedly; status = " << status << " ("
                       << zx_status_get_string(status) << ")";
  };
  auto unexpected_autocomplete_callback = [](cmd::Autocomplete* autocomplete) {
    EXPECT_TRUE(false) << "OnConsoleAutocomplete called unexpectedly";
  };

  CallbackClient client;
  client.console_command_callback = unexpected_command_callback;
  client.console_error_callback = unexpected_error_callback;
  client.console_autocomplete_callback = unexpected_autocomplete_callback;

  cmd::Console console(&client, dispatcher(), input_fd.get());
  console.Init("test> ");
  console.GetNextCommand();

  const char* input = "command1 arg0 arg1\ncommand2 xxx yyy zzz\ncommand3";
  size_t actual = 0;
  EXPECT_EQ(ZX_OK, h0.write(0, input, strlen(input), &actual));
  EXPECT_EQ(strlen(input), actual);

  int command_count = 0;
  client.console_command_callback = [&command_count](cmd::Command command) {
    command_count++;
    if (command_count == 1) {
      EXPECT_EQ(3u, command.args().size());
      EXPECT_EQ("command1", command.args()[0]);
      return ZX_ERR_NEXT;
    } else {
      EXPECT_EQ(4u, command.args().size());
      EXPECT_EQ("command2", command.args()[0]);
      return ZX_ERR_ASYNC;
    }
  };

  RunLoopUntilIdle();

  EXPECT_EQ(2, command_count);

  int error_count = 0;
  client.console_command_callback = unexpected_command_callback;
  client.console_error_callback = [&error_count](zx_status_t status) {
    error_count++;
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, status);
  };

  h0.reset();
  console.GetNextCommand();

  RunLoopUntilIdle();

  EXPECT_EQ(1, error_count);
}

}  // namespace
