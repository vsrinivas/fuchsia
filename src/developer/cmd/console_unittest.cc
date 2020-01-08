// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/console.h"

#include <fcntl.h>
#include <fuchsia/hardware/pty/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <poll.h>
#include <unistd.h>
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
  void OnConsoleInterrupt() override { console_interrupt_callback(); }
  void OnConsoleError(zx_status_t status) override { console_error_callback(status); }
  void OnConsoleAutocomplete(cmd::Autocomplete* autocomplete) override {
    return console_autocomplete_callback(autocomplete);
  }

  fit::function<zx_status_t(cmd::Command)> console_command_callback;
  fit::closure console_interrupt_callback;
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
  auto unexpected_interrupt_callback = []() {
    EXPECT_TRUE(false) << "OnConsoleInterrupt called unexpectedly";
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
  client.console_interrupt_callback = unexpected_interrupt_callback;
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

static zx_status_t open_client(int fd, uint32_t client_id, int* out_fd) {
  if (!out_fd) {
    return ZX_ERR_INVALID_ARGS;
  }

  fdio_t* io = fdio_unsafe_fd_to_io(fd);
  if (!io) {
    return ZX_ERR_INTERNAL;
  }

  zx::channel device_channel, client_channel;
  zx_status_t status = zx::channel::create(0, &device_channel, &client_channel);
  if (status != ZX_OK) {
    return status;
  }

  zx_status_t fidl_status = fuchsia_hardware_pty_DeviceOpenClient(
      fdio_unsafe_borrow_channel(io), client_id, device_channel.release(), &status);
  if (status != ZX_OK) {
    return status;
  }
  fdio_unsafe_release(io);
  if (fidl_status != ZX_OK) {
    return fidl_status;
  }
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_fd_create(client_channel.release(), out_fd);
  if (status != ZX_OK) {
    return status;
  }
  return fcntl(*out_fd, F_SETFL, O_NONBLOCK);
}

TEST_F(Console, Interrupt) {
  fbl::unique_fd ps;
  {
    zx::channel local, remote;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &local, &remote));
    ASSERT_EQ(ZX_OK, fdio_service_connect("/svc/fuchsia.hardware.pty.Device", remote.release()));
    int fd;
    ASSERT_EQ(ZX_OK, fdio_fd_create(local.release(), &fd));
    ps.reset(fd);
    ASSERT_TRUE(ps.is_valid());
    int flags = fcntl(ps.get(), F_GETFL);
    ASSERT_LE(0, flags);
    ASSERT_EQ(0, fcntl(ps.get(), F_SETFL, flags | O_NONBLOCK));
  }

  int pc_fd;
  ASSERT_EQ(ZX_OK, open_client(ps.get(), 0, &pc_fd));
  ASSERT_LE(0, pc_fd);

  fbl::unique_fd pc(pc_fd);
  ASSERT_EQ(true, bool(pc));

  auto unexpected_command_callback = [](cmd::Command command) {
    EXPECT_TRUE(false) << "OnConsoleCommand called unexpectedly";
    return ZX_ERR_BAD_STATE;
  };
  auto unexpected_interrupt_callback = []() {
    EXPECT_TRUE(false) << "OnConsoleInterrupt called unexpectedly";
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
  client.console_interrupt_callback = unexpected_interrupt_callback;
  client.console_error_callback = unexpected_error_callback;
  client.console_autocomplete_callback = unexpected_autocomplete_callback;

  cmd::Console console(&client, dispatcher(), pc.get());
  console.Init("test> ");
  console.GetNextCommand();

  ASSERT_EQ(write(ps.get(), "xyzzy", 5), 5);
  ASSERT_EQ(fdio_wait_fd(pc.get(), POLLIN, nullptr, ZX_TIME_INFINITE), ZX_OK);

  RunLoopUntilIdle();

  ASSERT_EQ(write(ps.get(), "\x3", 1), 1);
  ASSERT_EQ(fdio_wait_fd(pc.get(), POLLPRI, nullptr, ZX_TIME_INFINITE), ZX_OK);

  RunLoopUntilIdle();

  int command_count = 0;
  client.console_command_callback = [&command_count](cmd::Command command) {
    command_count++;
    return ZX_ERR_ASYNC;
  };

  ASSERT_EQ(write(ps.get(), "abc\n", 4), 4);
  ASSERT_EQ(fdio_wait_fd(pc.get(), POLLIN, nullptr, ZX_TIME_INFINITE), ZX_OK);

  RunLoopUntilIdle();

  ASSERT_EQ(1, command_count);

  int interrupt_count = 0;
  client.console_interrupt_callback = [&interrupt_count]() { interrupt_count++; };

  ASSERT_EQ(write(ps.get(), "\x3", 1), 1);
  ASSERT_EQ(fdio_wait_fd(pc.get(), POLLPRI, nullptr, ZX_TIME_INFINITE), ZX_OK);

  RunLoopUntilIdle();

  ASSERT_EQ(1, interrupt_count);
}

}  // namespace
