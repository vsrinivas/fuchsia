// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "socket_channel_relay.h"

#include <memory>

#include <lib/async-loop/cpp/loop.h>

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
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
        zx::socket::create(ZX_SOCKET_DATAGRAM, &local_socket_, &remote_socket_);
    remote_socket_handle_ = remote_socket_.get();
    EXPECT_EQ(socket_status, ZX_OK);
  }

 protected:
  fbl::RefPtr<testing::FakeChannel> channel() { return channel_; }
  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }
  zx::socket* remote_socket() { return &remote_socket_; }
  zx::socket ConsumeLocalSocket() { return std::move(local_socket_); }
  void CloseRemoteSocket() { remote_socket_.reset(); }
  // Note: A call to RunLoopOnce() may cause multiple timer-based tasks
  // to be dispatched. (When the timer expires, async_loop_run_once() dispatches
  // all expired timer-based tasks.)
  void RunLoopOnce() { loop_.Run(zx::time::infinite(), true); }
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
  internal::SocketChannelRelay* relay() {
    FXL_DCHECK(relay_);
    return relay_.get();
  }
  void DestroyRelay() { relay_ = nullptr; }

 private:
  bool was_deactivation_callback_invoked_;
  std::unique_ptr<internal::SocketChannelRelay> relay_;
};

TEST_F(SocketChannelRelayLifetimeTest, ActivateFailsIfGivenStoppedDispatcher) {
  ShutdownLoop();
  EXPECT_FALSE(relay()->Activate());
}

TEST_F(SocketChannelRelayLifetimeTest,
       ActivateDoesNotInvokeDeactivationCallbackOnSuccess) {
  ASSERT_TRUE(relay()->Activate());
  EXPECT_FALSE(was_deactivation_callback_invoked());
}

TEST_F(SocketChannelRelayLifetimeTest,
       ActivateDoesNotInvokeDeactivationCallbackOnFailure) {
  ShutdownLoop();
  ASSERT_FALSE(relay()->Activate());
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
  ASSERT_TRUE(relay()->Activate());

  ShutdownLoop();
  EXPECT_TRUE(was_deactivation_callback_invoked());
}

TEST_F(SocketChannelRelayLifetimeTest,
       RelayActivationFailsIfChannelActivationFails) {
  channel()->set_activate_fails(true);
  EXPECT_FALSE(relay()->Activate());
}

TEST_F(SocketChannelRelayLifetimeTest,
       DestructionWithPendingSdusFromChannelDoesNotCrash) {
  ASSERT_TRUE(relay()->Activate());
  channel()->Receive(common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o'));
  DestroyRelay();
  RunLoopUntilIdle();
}

TEST_F(SocketChannelRelayLifetimeTest, RelayIsDeactivatedWhenChannelIsClosed) {
  ASSERT_TRUE(relay()->Activate());

  channel()->Close();
  EXPECT_TRUE(was_deactivation_callback_invoked());
}

TEST_F(SocketChannelRelayLifetimeTest,
       RelayIsDeactivatedWhenRemoteSocketIsClosed) {
  ASSERT_TRUE(relay()->Activate());

  CloseRemoteSocket();
  RunLoopUntilIdle();
  EXPECT_TRUE(was_deactivation_callback_invoked());
}

TEST_F(SocketChannelRelayLifetimeTest, OversizedDatagramDeactivatesRelay) {
  const size_t kMessageBufSize = channel()->tx_mtu() * 5;
  common::DynamicByteBuffer large_message(kMessageBufSize);
  large_message.Fill('a');
  ASSERT_TRUE(relay()->Activate());

  size_t n_bytes_written_to_socket = 0;
  const auto write_res =
      remote_socket()->write(0, large_message.data(), large_message.size(),
                             &n_bytes_written_to_socket);
  ASSERT_EQ(ZX_OK, write_res);
  ASSERT_EQ(large_message.size(), n_bytes_written_to_socket);
  RunLoopUntilIdle();

  EXPECT_TRUE(was_deactivation_callback_invoked());
}

class SocketChannelRelayDataPathTest : public SocketChannelRelayTest {
 public:
  SocketChannelRelayDataPathTest()
      : relay_(ConsumeLocalSocket(), channel(), nullptr /* deactivation_cb */) {
    channel()->SetSendCallback(
        [&](auto data) { sent_to_channel_.push_back(std::move(data)); },
        dispatcher());
  }

 protected:
  internal::SocketChannelRelay* relay() { return &relay_; }
  auto& sent_to_channel() { return sent_to_channel_; }

 private:
  internal::SocketChannelRelay relay_;
  std::vector<std::unique_ptr<const common::ByteBuffer>> sent_to_channel_;
};

using SocketChannelRelaySocketRxTest = SocketChannelRelayDataPathTest;

TEST_F(SocketChannelRelaySocketRxTest, SduFromSocketIsCopiedToChannel) {
  const auto kExpectedMessage =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  ASSERT_TRUE(relay()->Activate());

  size_t n_bytes_written = 0;
  const auto write_res = remote_socket()->write(
      0, kExpectedMessage.data(), kExpectedMessage.size(), &n_bytes_written);
  ASSERT_EQ(ZX_OK, write_res);
  ASSERT_EQ(kExpectedMessage.size(), n_bytes_written);
  RunLoopUntilIdle();

  const auto& sdus = sent_to_channel();
  ASSERT_FALSE(sdus.empty());
  EXPECT_EQ(1U, sdus.size());
  ASSERT_TRUE(sdus[0]);
  EXPECT_EQ(kExpectedMessage.size(), sdus[0]->size());
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[0]));
}

TEST_F(SocketChannelRelaySocketRxTest,
       MultipleSdusFromSocketAreCopiedToChannel) {
  const auto kExpectedMessage =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  const size_t kNumMessages = 3;
  ASSERT_TRUE(relay()->Activate());

  for (size_t i = 0; i < kNumMessages; ++i) {
    size_t n_bytes_written = 0;
    const auto write_res = remote_socket()->write(
        0, kExpectedMessage.data(), kExpectedMessage.size(), &n_bytes_written);
    ASSERT_EQ(ZX_OK, write_res);
    ASSERT_EQ(kExpectedMessage.size(), n_bytes_written);
    RunLoopUntilIdle();
  }

  const auto& sdus = sent_to_channel();
  ASSERT_FALSE(sdus.empty());
  ASSERT_EQ(3U, sdus.size());
  ASSERT_TRUE(sdus[0]);
  ASSERT_TRUE(sdus[1]);
  ASSERT_TRUE(sdus[2]);
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[0]));
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[1]));
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[2]));
}

TEST_F(SocketChannelRelaySocketRxTest,
       MultipleSdusAreCopiedToChannelInOneRelayTask) {
  const auto kExpectedMessage =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  const size_t kNumMessages = 3;
  ASSERT_TRUE(relay()->Activate());

  for (size_t i = 0; i < kNumMessages; ++i) {
    size_t n_bytes_written = 0;
    const auto write_res = remote_socket()->write(
        0, kExpectedMessage.data(), kExpectedMessage.size(), &n_bytes_written);
    ASSERT_EQ(ZX_OK, write_res);
    ASSERT_EQ(kExpectedMessage.size(), n_bytes_written);
  }

  RunLoopOnce();  // Runs SocketChannelRelay::OnSocketReadable().
  RunLoopOnce();  // Runs all tasks queued by FakeChannel::Send().

  const auto& sdus = sent_to_channel();
  ASSERT_FALSE(sdus.empty());
  ASSERT_EQ(3U, sdus.size());
  ASSERT_TRUE(sdus[0]);
  ASSERT_TRUE(sdus[1]);
  ASSERT_TRUE(sdus[2]);
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[0]));
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[1]));
  EXPECT_TRUE(common::ContainersEqual(kExpectedMessage, *sdus[2]));
}

TEST_F(SocketChannelRelaySocketRxTest, OversizedSduIsDropped) {
  const size_t kMessageBufSize = channel()->tx_mtu() * 5;
  common::DynamicByteBuffer large_message(kMessageBufSize);
  large_message.Fill('a');
  ASSERT_TRUE(relay()->Activate());

  size_t n_bytes_written_to_socket = 0;
  const auto write_res =
      remote_socket()->write(0, large_message.data(), large_message.size(),
                             &n_bytes_written_to_socket);
  ASSERT_EQ(ZX_OK, write_res);
  ASSERT_EQ(large_message.size(), n_bytes_written_to_socket);
  RunLoopUntilIdle();

  ASSERT_TRUE(sent_to_channel().empty());
}

}  // namespace
}  // namespace l2cap
}  // namespace btlib
