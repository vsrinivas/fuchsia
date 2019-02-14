// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/outgoing.h>

#include "echo_server.h"

#include <fuchsia/io/c/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/zx/channel.h>

#include "gtest/gtest.h"

namespace {

using OutgoingTest = gtest::RealLoopFixture;

TEST_F(OutgoingTest, Control) {
  zx::channel svc_client, svc_server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_client, &svc_server));

  sys::Outgoing outgoing;
  ASSERT_EQ(ZX_OK, outgoing.Serve(std::move(svc_server), dispatcher()));

  EchoImpl impl;

  ASSERT_EQ(ZX_OK, outgoing.AddPublicService(impl.GetHandler(dispatcher())));

  fidl::examples::echo::EchoPtr echo;
  fdio_service_connect_at(
      svc_client.get(), "public/fidl.examples.echo.Echo",
      echo.NewRequest(dispatcher()).TakeChannel().release());

  std::string result;
  echo->EchoString("hello",
                   [&result](fidl::StringPtr value) { result = *value; });

  RunLoopUntilIdle();
  EXPECT_EQ("hello", result);
}

TEST_F(OutgoingTest, AddAndRemove) {
  zx::channel svc_client, svc_server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_client, &svc_server));

  sys::Outgoing outgoing;
  ASSERT_EQ(ZX_OK, outgoing.Serve(std::move(svc_server), dispatcher()));

  ASSERT_EQ(ZX_ERR_NOT_FOUND,
            outgoing.RemovePublicService<fidl::examples::echo::Echo>());

  EchoImpl impl;
  fidl::BindingSet<fidl::examples::echo::Echo> bindings;
  ASSERT_EQ(ZX_OK, outgoing.AddPublicService(
                       bindings.GetHandler(&impl, dispatcher())));
  ASSERT_EQ(
      ZX_ERR_ALREADY_EXISTS,
      outgoing.AddPublicService(bindings.GetHandler(&impl, dispatcher())));

  fidl::examples::echo::EchoPtr echo;
  fdio_service_connect_at(
      svc_client.get(), "public/fidl.examples.echo.Echo",
      echo.NewRequest(dispatcher()).TakeChannel().release());

  std::string result;
  echo->EchoString("hello",
                   [&result](fidl::StringPtr value) { result = *value; });

  RunLoopUntilIdle();
  EXPECT_EQ("hello", result);

  ASSERT_EQ(ZX_OK, outgoing.RemovePublicService<fidl::examples::echo::Echo>());
  ASSERT_EQ(ZX_ERR_NOT_FOUND,
            outgoing.RemovePublicService<fidl::examples::echo::Echo>());

  fdio_service_connect_at(
      svc_client.get(), "public/fidl.examples.echo.Echo",
      echo.NewRequest(dispatcher()).TakeChannel().release());

  result = "no callback";
  echo->EchoString("good-bye",
                   [&result](fidl::StringPtr value) { result = *value; });

  RunLoopUntilIdle();
  EXPECT_EQ("no callback", result);
}

TEST_F(OutgoingTest, Invalid) {
  sys::Outgoing outgoing;
  // TODO: This should return ZX_ERR_BAD_HANDLE.
  ASSERT_EQ(ZX_OK, outgoing.Serve(zx::channel(), dispatcher()));
}

TEST_F(OutgoingTest, AccessDenied) {
  zx::channel svc_client, svc_server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_client, &svc_server));

  svc_server.replace(ZX_RIGHT_NONE, &svc_server);

  sys::Outgoing outgoing;
  ASSERT_EQ(ZX_ERR_ACCESS_DENIED,
            outgoing.Serve(std::move(svc_server), dispatcher()));
}

}  // namespace
