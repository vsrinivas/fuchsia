// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/misc/cpp/fidl.h>
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
#include "test/test_util.h"
#include "testing/fidl/async_loop_for_test.h"
#include "testing/fidl/frobinator_impl.h"

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

TEST(DecodeTest, V1HeaderCompatibilityTest) {
  // Old versions of the C bindings do not set the V2 wire format bit.
  // This test verifies that HLCPP will successfully decode messages as V2 that lack the
  // V2 wire format bit.
  constexpr fidl_message_header_t kV1Header = {
      .magic_number = kFidlWireFormatMagicNumberInitial,
  };
  std::vector<uint8_t> bytes = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // header pt1
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // header pt2
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // body
  };
  memcpy(bytes.data(), &kV1Header, sizeof(kV1Header));
  const char* error;
  fidl::HLCPPIncomingMessage msg(BytePart(bytes.data(), static_cast<uint32_t>(bytes.size()),
                                          static_cast<uint32_t>(bytes.size())),
                                 HandleInfoPart());
  ASSERT_OK(msg.Decode(fidl::test::misc::Int64Struct::FidlType, &error));
  ASSERT_EQ(2, msg.GetBodyViewAs<fidl::test::misc::Int64Struct>()->x);
}

}  // namespace
}  // namespace fidl
