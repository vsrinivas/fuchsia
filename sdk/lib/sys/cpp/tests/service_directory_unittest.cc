// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/channel.h>

#include <gtest/gtest.h>
#include <test/placeholders/cpp/fidl.h>

namespace fio = fuchsia_io;

TEST(ServiceDirectoryTest, Control) {
  zx::channel svc_client, svc_server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_client, &svc_server));

  sys::ServiceDirectory directory(std::move(svc_client));

  fidl::InterfaceHandle<test::placeholders::Echo> echo;
  EXPECT_EQ(ZX_OK, directory.Connect(echo.NewRequest()));

  fidl::IncomingMessageBuffer buffer;
  auto message = buffer.CreateEmptyIncomingMessage();
  message.Read(svc_server.get(), 0);

  EXPECT_EQ(fidl::internal::WireOrdinal<fio::Directory::Open>::value, message.ordinal());
}

TEST(ServiceDirectoryTest, CreateWithRequest) {
  zx::channel svc_server;

  auto directory = sys::ServiceDirectory::CreateWithRequest(&svc_server);

  fidl::InterfaceHandle<test::placeholders::Echo> echo;
  EXPECT_EQ(ZX_OK, directory->Connect(echo.NewRequest()));

  fidl::IncomingMessageBuffer buffer;
  auto message = buffer.CreateEmptyIncomingMessage();
  message.Read(svc_server.get(), 0);

  EXPECT_EQ(fidl::internal::WireOrdinal<fio::Directory::Open>::value, message.ordinal());
}

TEST(ServiceDirectoryTest, Clone) {
  zx::channel svc_server;

  auto directory = sys::ServiceDirectory::CreateWithRequest(&svc_server);

  fidl::InterfaceHandle<test::placeholders::Echo> echo;
  EXPECT_TRUE(directory->CloneChannel().is_valid());

  fidl::IncomingMessageBuffer buffer;
  auto message = buffer.CreateEmptyIncomingMessage();
  message.Read(svc_server.get(), 0);

  EXPECT_EQ(fidl::internal::WireOrdinal<fio::Directory::Clone>::value, message.ordinal());
}

TEST(ServiceDirectoryTest, Invalid) {
  sys::ServiceDirectory directory((zx::channel()));

  fidl::InterfaceHandle<test::placeholders::Echo> echo;
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, directory.Connect(echo.NewRequest()));
}

TEST(ServiceDirectoryTest, AccessDenied) {
  zx::channel svc_client, svc_server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_client, &svc_server));

  svc_client.replace(ZX_RIGHT_NONE, &svc_client);

  sys::ServiceDirectory directory(std::move(svc_client));

  fidl::InterfaceHandle<test::placeholders::Echo> echo;
  EXPECT_EQ(ZX_ERR_ACCESS_DENIED, directory.Connect(echo.NewRequest()));
}
