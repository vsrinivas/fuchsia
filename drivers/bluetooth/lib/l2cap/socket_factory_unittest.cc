// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "socket_factory.h"

#include <memory>

#include <lib/async-loop/cpp/loop.h>

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel.h"

namespace btlib {
namespace l2cap {
namespace {

class L2CAP_SocketFactoryTest : public ::testing::Test {
 public:
  L2CAP_SocketFactoryTest() : loop_(&kAsyncLoopConfigAttachToThread) {
    EXPECT_EQ(ASYNC_LOOP_RUNNABLE, loop_.GetState());
    channel_ = fbl::MakeRefCounted<testing::FakeChannel>(
        kDynamicChannelIdMin, kRemoteChannelId, kDefaultConnectionHandle,
        hci::Connection::LinkType::kACL);
    EXPECT_TRUE(channel_);
  }

  void TearDown() {
    // Process any pending events, to tickle any use-after-free bugs.
    RunLoopUntilIdle();
  }

 protected:
  static constexpr ChannelId kDynamicChannelIdMin = 0x0040;
  static constexpr ChannelId kRemoteChannelId = 0x0050;
  static constexpr hci::ConnectionHandle kDefaultConnectionHandle = 0x0001;
  fbl::RefPtr<testing::FakeChannel> channel() { return channel_; }
  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }
  void RunLoopUntilIdle() { loop_.RunUntilIdle(); }

 private:
  async::Loop loop_;
  fbl::RefPtr<testing::FakeChannel> channel_;
};

constexpr ChannelId L2CAP_SocketFactoryTest::kDynamicChannelIdMin;
constexpr ChannelId L2CAP_SocketFactoryTest::kRemoteChannelId;
constexpr hci::ConnectionHandle
    L2CAP_SocketFactoryTest::kDefaultConnectionHandle;

TEST_F(L2CAP_SocketFactoryTest, CanCreateSocket) {
  SocketFactory socket_factory;
  EXPECT_TRUE(socket_factory.MakeSocketForChannel(channel()));
}

TEST_F(L2CAP_SocketFactoryTest, SocketCreationFailsIfChannelAlreadyHasASocket) {
  SocketFactory socket_factory;
  zx::socket socket = socket_factory.MakeSocketForChannel(channel());
  ASSERT_TRUE(socket);

  EXPECT_FALSE(socket_factory.MakeSocketForChannel(channel()));
}

TEST_F(L2CAP_SocketFactoryTest, SocketCreationFailsIfChannelActivationFails) {
  channel()->set_activate_fails(true);
  EXPECT_FALSE(SocketFactory().MakeSocketForChannel(channel()));
}

TEST_F(L2CAP_SocketFactoryTest, CanCreateSocketForNewChannelWithRecycledId) {
  SocketFactory socket_factory;
  auto original_channel = fbl::MakeRefCounted<testing::FakeChannel>(
      kDynamicChannelIdMin + 1, kRemoteChannelId, kDefaultConnectionHandle,
      hci::Connection::LinkType::kACL);
  zx::socket socket = socket_factory.MakeSocketForChannel(original_channel);
  ASSERT_TRUE(socket);
  original_channel->Close();
  RunLoopUntilIdle();  // Process any events related to channel closure.

  auto new_channel = fbl::MakeRefCounted<testing::FakeChannel>(
      kDynamicChannelIdMin + 1, kRemoteChannelId, kDefaultConnectionHandle,
      hci::Connection::LinkType::kACL);
  EXPECT_TRUE(socket_factory.MakeSocketForChannel(new_channel));
}

TEST_F(L2CAP_SocketFactoryTest, DestructionWithActiveRelayDoesNotCrash) {
  SocketFactory socket_factory;
  zx::socket socket = socket_factory.MakeSocketForChannel(channel());
  ASSERT_TRUE(socket);
  // |socket_factory| is destroyed implicitly.
}

TEST_F(L2CAP_SocketFactoryTest, DestructionAfterDeactivatingRelayDoesNotCrash) {
  SocketFactory socket_factory;
  zx::socket socket = socket_factory.MakeSocketForChannel(channel());
  ASSERT_TRUE(socket);
  channel()->Close();
  RunLoopUntilIdle();  // Process any events related to channel closure.
  // |socket_factory| is destroyed implicitly.
}

}  // namespace
}  // namespace l2cap
}  // namespace btlib
