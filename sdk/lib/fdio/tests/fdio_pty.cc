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

namespace fpty = ::llcpp::fuchsia::hardware::pty;

TEST(PtyTest, WindowSize) {
  zx::channel device_client_end, device_server_end;
  ASSERT_OK(zx::channel::create(0, &device_client_end, &device_server_end));

  std::string path = "/svc/";
  path.append(fpty::Device::Name);
  ASSERT_OK(fdio_service_connect(path.c_str(), device_server_end.release()));

  zx::channel pty_client_end, pty_server_end;
  ASSERT_OK(zx::channel::create(0, &pty_client_end, &pty_server_end));
  auto result0 =
      fpty::Device::Call::OpenClient(device_client_end.borrow(), 0, std::move(pty_server_end));
  ASSERT_OK(result0.status());
  ASSERT_OK(result0->s);

  fbl::unique_fd controlling_client;
  ASSERT_OK(fdio_fd_create(pty_client_end.release(), controlling_client.reset_and_get_address()));

  ASSERT_OK(zx::channel::create(0, &pty_client_end, &pty_server_end));
  auto result1 =
      fpty::Device::Call::OpenClient(device_client_end.borrow(), 1, std::move(pty_server_end));
  ASSERT_OK(result1.status());
  ASSERT_OK(result1->s);

  fbl::unique_fd client;
  ASSERT_OK(fdio_fd_create(pty_client_end.release(), client.reset_and_get_address()));

  struct winsize size = {};
  size.ws_row = 7;
  size.ws_col = 5;
  ASSERT_EQ(0, ioctl(controlling_client.get(), TIOCSWINSZ, &size));

  size.ws_row = 9783;
  size.ws_col = 7573;

  ASSERT_EQ(0, ioctl(client.get(), TIOCGWINSZ, &size));
  ASSERT_EQ(7, size.ws_row);
  ASSERT_EQ(5, size.ws_col);
}
