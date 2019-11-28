// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/fidl_helpers/message_relay.h"

#include <lib/zx/channel.h>

#include <memory>

#include "gtest/gtest.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace ledger {
namespace fidl_helpers {
namespace {

using MessageRelayTest = gtest::TestLoopFixture;

TEST_F(MessageRelayTest, DestructionInCallback) {
  auto message_relay = std::make_unique<MessageRelay>();

  zx::channel c1, c2;
  ASSERT_EQ(zx::channel::create(0u, &c1, &c2), ZX_OK);

  message_relay->SetChannel(std::move(c2));
  message_relay->SetMessageReceivedCallback(
      [&](std::vector<uint8_t> data) { message_relay.reset(); });
  c1.write(0u, "0", 1, nullptr, 0u);
  RunLoopUntilIdle();
  EXPECT_FALSE(message_relay);
}

TEST_F(MessageRelayTest, SendReceiveMessage) {
  auto message_relay_1 = std::make_unique<MessageRelay>();
  auto message_relay_2 = std::make_unique<MessageRelay>();

  zx::channel c1, c2;
  ASSERT_EQ(zx::channel::create(0u, &c1, &c2), ZX_OK);

  message_relay_1->SetChannel(std::move(c1));
  message_relay_2->SetChannel(std::move(c2));

  bool called;
  std::vector<uint8_t> data;
  message_relay_2->SetMessageReceivedCallback(
      callback::Capture(callback::SetWhenCalled(&called), &data));
  message_relay_1->SendMessage(convert::ToArray("some data"));

  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(convert::ToString(data), "some data");
}

}  // namespace
}  // namespace fidl_helpers
}  // namespace ledger
