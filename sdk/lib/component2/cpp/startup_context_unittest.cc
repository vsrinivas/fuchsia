// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/component2/cpp/startup_context.h>

#include <fidl/examples/echo/cpp/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/zx/channel.h>

#include "gtest/gtest.h"

using StartupContextTest = gtest::RealLoopFixture;

TEST_F(StartupContextTest, Control) {
  fuchsia::io::DirectoryPtr directory;

  zx::channel svc_client, svc_server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_client, &svc_server));

  component2::StartupContext context(
      std::move(svc_client), directory.NewRequest(dispatcher()).TakeChannel(),
      dispatcher());

  fidl::examples::echo::EchoPtr echo;
  context.Connect(echo.NewRequest());

  fidl::MessageBuffer buffer;
  auto message = buffer.CreateEmptyMessage();
  message.Read(svc_server.get(), 0);

  EXPECT_TRUE(message.has_header());
  EXPECT_EQ(fuchsia_io_DirectoryOpenOrdinal, message.ordinal());
}
