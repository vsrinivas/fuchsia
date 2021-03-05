// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pty/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/zx/channel.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace fpty = fuchsia_hardware_pty;

TEST(PtyTest, WindowSize) {
  auto endpoints = fidl::CreateEndpoints<fpty::Device>();
  ASSERT_OK(endpoints.status_value());

  auto client = fidl::BindSyncClient(std::move(endpoints->client));

  std::string path = "/svc/";
  path.append(fpty::Device::Name);
  ASSERT_OK(fdio_service_connect(path.c_str(), endpoints->server.channel().release()));

  zx::channel pty_client_end, pty_server_end;
  ASSERT_OK(zx::channel::create(0, &pty_client_end, &pty_server_end));
  auto result0 = client.OpenClient(0, std::move(pty_server_end));
  ASSERT_OK(result0.status());
  ASSERT_OK(result0->s);

  fbl::unique_fd controlling_client;
  ASSERT_OK(fdio_fd_create(pty_client_end.release(), controlling_client.reset_and_get_address()));

  ASSERT_OK(zx::channel::create(0, &pty_client_end, &pty_server_end));
  auto result1 = client.OpenClient(1, std::move(pty_server_end));
  ASSERT_OK(result1.status());
  ASSERT_OK(result1->s);

  fbl::unique_fd fd;
  ASSERT_OK(fdio_fd_create(pty_client_end.release(), fd.reset_and_get_address()));

  struct winsize set_size = {
      .ws_row = 7,
      .ws_col = 5,
  };
  ASSERT_EQ(0, ioctl(controlling_client.get(), TIOCSWINSZ, &set_size));

  struct winsize get_size = {
      .ws_row = 9783,
      .ws_col = 7573,
  };
  ASSERT_EQ(0, ioctl(fd.get(), TIOCGWINSZ, &get_size));
  ASSERT_EQ(get_size.ws_row, set_size.ws_row);
  ASSERT_EQ(get_size.ws_col, set_size.ws_col);
}
