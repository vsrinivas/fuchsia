// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "socket_channel_relay.h"

#include <memory>

#include <lib/async-loop/cpp/loop.h>

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel.h"

namespace btlib {
namespace l2cap {
namespace {

class SocketChannelRelayTest : public ::testing::Test {
 public:
  SocketChannelRelayTest() : loop_(&kAsyncLoopConfigAttachToThread) {
    EXPECT_EQ(loop_.GetState(), ASYNC_LOOP_RUNNABLE);

    constexpr ChannelId kDynamicChannelIdMin = 0x0040;
    constexpr ChannelId kRemoteChannelId = 0x0050;
    constexpr hci::ConnectionHandle kDefaultConnectionHandle = 0x0001;
    channel_ = fbl::AdoptRef(new testing::FakeChannel(
        kDynamicChannelIdMin, kRemoteChannelId, kDefaultConnectionHandle,
        hci::Connection::LinkType::kACL));
    EXPECT_TRUE(channel_);

    const auto socket_status =
        zx::socket::create(ZX_SOCKET_STREAM, &local_socket_, &remote_socket_);
    remote_socket_handle_ = remote_socket_.get();
    EXPECT_EQ(socket_status, ZX_OK);
  }

 protected:
  fbl::RefPtr<testing::FakeChannel> channel() { return channel_; }
  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }
  zx::socket* remote_socket() { return &remote_socket_; }
  zx::socket ConsumeLocalSocket() { return std::move(local_socket_); }
  void RunLoopUntilIdle() { loop_.RunUntilIdle(); }
  void ShutdownLoop() { loop_.Shutdown(); }

 private:
  fbl::RefPtr<testing::FakeChannel> channel_;
  zx::socket local_socket_;
  zx::socket remote_socket_;
  zx_handle_t remote_socket_handle_;
  // TODO(NET-1526): Move to FakeChannelTest, which incorporates
  // async::TestLoop.
  async::Loop loop_;
};

class SocketChannelRelayLifetimeTest : public SocketChannelRelayTest {
 public:
  SocketChannelRelayLifetimeTest()
      : was_deactivation_callback_invoked_(false),
        relay_(std::make_unique<internal::SocketChannelRelay>(
            ConsumeLocalSocket(), channel(),
            [this](ChannelId) { was_deactivation_callback_invoked_ = true; })) {
  }

 protected:
  bool was_deactivation_callback_invoked() {
    return was_deactivation_callback_invoked_;
  }
  internal::SocketChannelRelay& relay() {
    FXL_DCHECK(relay_);
    return *relay_;
  }
  void DestroyRelay() { relay_ = nullptr; }

 private:
  bool was_deactivation_callback_invoked_;
  std::unique_ptr<internal::SocketChannelRelay> relay_;
};

TEST_F(SocketChannelRelayLifetimeTest, ActivateFailsIfGivenStoppedDispatcher) {
  ShutdownLoop();
  EXPECT_FALSE(relay().Activate());
}

TEST_F(SocketChannelRelayLifetimeTest,
       ActivateDoesNotInvokeDeactivationCallbackOnSuccess) {
  ASSERT_TRUE(relay().Activate());
  EXPECT_FALSE(was_deactivation_callback_invoked());
}

TEST_F(SocketChannelRelayLifetimeTest,
       ActivateDoesNotInvokeDeactivationCallbackOnFailure) {
  ShutdownLoop();
  ASSERT_FALSE(relay().Activate());
  EXPECT_FALSE(was_deactivation_callback_invoked());
}

TEST_F(SocketChannelRelayLifetimeTest, SocketIsClosedWhenRelayIsDestroyed) {
  const char data = 'a';
  ASSERT_EQ(ZX_OK, remote_socket()->write(0, &data, sizeof(data), nullptr));
  DestroyRelay();
  EXPECT_EQ(ZX_ERR_PEER_CLOSED,
            remote_socket()->write(0, &data, sizeof(data), nullptr));
}

TEST_F(SocketChannelRelayLifetimeTest,
       RelayIsDeactivatedWhenDispatcherIsShutDown) {
  ASSERT_TRUE(relay().Activate());

  ShutdownLoop();
  EXPECT_TRUE(was_deactivation_callback_invoked());
}

TEST_F(SocketChannelRelayLifetimeTest,
       RelayActivationFailsIfChannelActivationFails) {
  channel()->set_activate_fails(true);
  EXPECT_FALSE(relay().Activate());
}

TEST_F(SocketChannelRelayLifetimeTest,
       DestructionWithPendingSdusFromChannelDoesNotCrash) {
  ASSERT_TRUE(relay().Activate());
  channel()->Receive(common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o'));
  DestroyRelay();
  RunLoopUntilIdle();
}

}  // namespace
}  // namespace l2cap
}  // namespace btlib
