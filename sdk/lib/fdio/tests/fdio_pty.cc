// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <sys/ioctl.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace fpty = fuchsia_hardware_pty;

TEST(PtyTest, WindowSize) {
  auto endpoints = fidl::CreateEndpoints<fpty::Device>();
  ASSERT_OK(endpoints.status_value());

  auto client = fidl::BindSyncClient(std::move(endpoints->client));

  ASSERT_OK(fdio_service_connect(fidl::DiscoverableProtocolDefaultPath<fpty::Device>,
                                 endpoints->server.channel().release()));

  auto endpoints0 = fidl::CreateEndpoints<fpty::Device>();
  ASSERT_OK(endpoints0.status_value());
  auto result0 = client.OpenClient(0, std::move(endpoints0->server));
  ASSERT_OK(result0.status());
  ASSERT_OK(result0->s);

  fbl::unique_fd controlling_client;
  ASSERT_OK(fdio_fd_create(endpoints0->client.channel().release(),
                           controlling_client.reset_and_get_address()));

  auto endpoints1 = fidl::CreateEndpoints<fpty::Device>();
  ASSERT_OK(endpoints1.status_value());

  auto result1 = client.OpenClient(1, std::move(endpoints1->server));
  ASSERT_OK(result1.status());
  ASSERT_OK(result1->s);

  fbl::unique_fd fd;
  ASSERT_OK(fdio_fd_create(endpoints1->client.channel().release(), fd.reset_and_get_address()));

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
