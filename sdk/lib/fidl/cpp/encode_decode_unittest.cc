// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/test/test_util.h>
#include <zircon/types.h>

#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <zxtest/zxtest.h>

#include "fidl/test/unionmigration/cpp/fidl.h"
#include "lib/fidl/cpp/event_sender.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/internal/message_reader.h"
#include "lib/fidl/cpp/internal/proxy_controller.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/frobinator_impl.h"

namespace fidl {
namespace {

class MagicNumberMessageHandler : public internal::MessageHandler {
 public:
  zx_status_t OnMessage(HLCPPIncomingMessage message) override {
    is_supported = message.is_supported_version();
    return ZX_OK;
  }

  bool is_supported = false;
};

TEST(EncodeTest, EventMagicNumber) {
  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  MagicNumberMessageHandler handler;
  internal::MessageReader client(&handler);
  client.Bind(std::move(h1));
  EXPECT_TRUE(client.is_bound());

  EventSender<fidl::test::frobinator::Frobinator> sender(std::move(h2));
  EXPECT_TRUE(sender.channel().is_valid());

  auto background = std::thread([&sender]() { sender.events().Hrob("one"); });
  background.join();
  loop.RunUntilIdle();

  ASSERT_TRUE(handler.is_supported);
}

TEST(EncodeTest, RequestMagicNumber) {
  fidl::test::AsyncLoopForTest loop;
  fidl::test::frobinator::FrobinatorPtr client;

  MagicNumberMessageHandler handler;
  internal::MessageReader server(&handler);
  server.Bind(client.NewRequest().TakeChannel());
  EXPECT_TRUE(client.is_bound());
  EXPECT_TRUE(server.is_bound());

  client->Frob("one");
  loop.RunUntilIdle();

  ASSERT_TRUE(handler.is_supported);
}

}  // namespace
}  // namespace fidl
