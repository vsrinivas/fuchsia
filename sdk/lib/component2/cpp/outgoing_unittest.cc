// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/component2/cpp/outgoing.h>

#include <fidl/examples/echo/cpp/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/zx/channel.h>

#include "gtest/gtest.h"

namespace {

using OutgoingTest = gtest::RealLoopFixture;

class EchoImpl : public fidl::examples::echo::Echo {
 public:
  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override {
    callback(std::move(value));
  }
};

TEST_F(OutgoingTest, Control) {
  zx::channel svc_client, svc_server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_client, &svc_server));

  component2::Outgoing outgoing;
  ASSERT_EQ(ZX_OK, outgoing.Serve(std::move(svc_server), dispatcher()));

  EchoImpl impl;

  fidl::BindingSet<fidl::examples::echo::Echo> bindings;
  ASSERT_EQ(ZX_OK, outgoing.AddPublicService(
                       bindings.GetHandler(&impl, dispatcher())));

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

}  // namespace
