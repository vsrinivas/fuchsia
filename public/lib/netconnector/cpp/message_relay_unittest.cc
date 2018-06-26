// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/netconnector/cpp/message_relay.h"

#include <memory>

#include <lib/zx/channel.h>

#include "garnet/public/lib/gtest/test_loop_fixture.h"
#include "gtest/gtest.h"

namespace netconnector {
namespace {

using MessageRelayTest = gtest::TestLoopFixture;

TEST_F(MessageRelayTest, DestructionInCallback) {
  auto message_relay = std::make_unique<MessageRelay>();

  zx::channel c1, c2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0u, &c1, &c2));

  message_relay->SetChannel(std::move(c2));
  message_relay->SetMessageReceivedCallback([&] (std::vector<uint8_t> data) {
    message_relay.reset();
  });
  c1.write(0u, "0", 1, nullptr, 0u);
  RunLoopUntilIdle();
  EXPECT_FALSE(message_relay);
}

}  // namespace
}  // namespace netconnector
