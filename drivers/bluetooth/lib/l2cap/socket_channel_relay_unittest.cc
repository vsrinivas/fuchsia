// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "socket_channel_relay.h"

#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <zircon/compiler.h>

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
    local_socket_handle_ = local_socket_.get();
    remote_socket_handle_ = remote_socket_.get();
    EXPECT_EQ(socket_status, ZX_OK);
  }

  // Writes data on |local_socket| until the socket is full, or an error occurs.
  // Returns the number of bytes written if the socket fills, and zero
  // otherwise.
  __WARN_UNUSED_RESULT size_t StuffSocket() {
    size_t n_total_bytes_written = 0;
    zx_status_t write_res;
    common::StaticByteBuffer<4096> spam_data;
    spam_data.Fill(kSpamChar);
    do {
      size_t n_iter_bytes_written = 0;
      write_res = zx_socket_write(local_socket_handle_, 0, spam_data.data(),
                                  spam_data.size(), &n_iter_bytes_written);
      if (write_res != ZX_OK && write_res != ZX_ERR_SHOULD_WAIT) {
        FXL_LOG(ERROR) << "Failure in zx_socket_write(): "
                       << zx_status_get_string(write_res);
        return 0;
      }
      n_total_bytes_written += n_iter_bytes_written;
    } while (write_res == ZX_OK);
    return n_total_bytes_written;
  }

  // Reads and discards |n_bytes| on |remote_socket|. Returns true if |n_bytes|
  // were successfully discarded.
  __WARN_UNUSED_RESULT bool DiscardFromSocket(size_t n_bytes_requested) {
    common::DynamicByteBuffer received_data(n_bytes_requested);
    zx_status_t read_res;
    size_t n_total_bytes_read = 0;
    while (n_total_bytes_read < n_bytes_requested) {
      size_t n_iter_bytes_read = 0;
      read_res = remote_socket_.read(0, received_data.mutable_data(),
                                     received_data.size(), &n_iter_bytes_read);
      if (read_res != ZX_OK && read_res != ZX_ERR_SHOULD_WAIT) {
        FXL_LOG(ERROR) << "Failure in zx_socket_read(): "
                       << zx_status_get_string(read_res);
        return false;
      }
      n_total_bytes_read += n_iter_bytes_read;
    }
    EXPECT_EQ(n_bytes_requested, n_total_bytes_read);
    return n_bytes_requested == n_total_bytes_read;
  }

 protected:
  static constexpr auto kGoodChar = 'a';
  static constexpr auto kSpamChar = 'b';
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
  zx_handle_t local_socket_handle_;
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
  const char data = kGoodChar;
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

class SocketChannelRelayChannelRxTest : public SocketChannelRelayDataPathTest {
 protected:
  common::DynamicByteBuffer ReadDatagramFromSocket(const size_t dgram_len) {
    common::DynamicByteBuffer socket_read_buffer(
        dgram_len + 1);  // +1 to detect trailing garbage.
    size_t n_bytes_read = 0;
    const auto read_res =
        remote_socket()->read(0, socket_read_buffer.mutable_data(),
                              socket_read_buffer.size(), &n_bytes_read);
    if (read_res != ZX_OK) {
      FXL_LOG(ERROR) << "Failure in zx_socket_read(): "
                     << zx_status_get_string(read_res);
      return {};
    }
    return common::DynamicByteBuffer(
        common::BufferView(socket_read_buffer, n_bytes_read));
  }
};

TEST_F(SocketChannelRelayChannelRxTest,
       MessageFromChannelIsCopiedToSocketSynchronously) {
  const auto kExpectedMessage =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  ASSERT_TRUE(relay()->Activate());
  channel()->Receive(kExpectedMessage);
  // Note: we dispatch one task, to get the data from the FakeChannel to
  // the SocketChannelRelay. We avoid RunUntilIdle(), to ensure that the
  // SocketChannelRelay immediately copies the l2cap::Channel data to the
  // zx::socket.
  RunLoopOnce();

  EXPECT_TRUE(common::ContainersEqual(
      kExpectedMessage, ReadDatagramFromSocket(kExpectedMessage.size())));
}

TEST_F(SocketChannelRelayChannelRxTest,
       MultipleSdusFromChannelAreCopiedToSocketPreservingSduBoundaries) {
  const auto kExpectedMessage1 =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  const auto kExpectedMessage2 =
      common::CreateStaticByteBuffer('g', 'o', 'o', 'd', 'b', 'y', 'e');
  ASSERT_TRUE(relay()->Activate());
  channel()->Receive(kExpectedMessage1);
  channel()->Receive(kExpectedMessage2);
  RunLoopUntilIdle();

  EXPECT_TRUE(common::ContainersEqual(
      kExpectedMessage1, ReadDatagramFromSocket(kExpectedMessage1.size())));
  EXPECT_TRUE(common::ContainersEqual(
      kExpectedMessage2, ReadDatagramFromSocket(kExpectedMessage2.size())));
}

TEST_F(SocketChannelRelayChannelRxTest,
       SdusReceivedBeforeChannelActivationAreCopiedToSocket) {
  const auto kExpectedMessage1 =
      common::CreateStaticByteBuffer('h', 'e', 'l', 'l', 'o');
  const auto kExpectedMessage2 =
      common::CreateStaticByteBuffer('g', 'o', 'o', 'd', 'b', 'y', 'e');
  channel()->Receive(kExpectedMessage1);
  channel()->Receive(kExpectedMessage2);
  ASSERT_TRUE(relay()->Activate());
  // Note: we omit RunLoopOnce()/RunLoopUntilIdle(), as Channel activation
  // delivers the messages synchronously.

  EXPECT_TRUE(common::ContainersEqual(
      kExpectedMessage1, ReadDatagramFromSocket(kExpectedMessage1.size())));
  EXPECT_TRUE(common::ContainersEqual(
      kExpectedMessage2, ReadDatagramFromSocket(kExpectedMessage2.size())));
}

TEST_F(SocketChannelRelayChannelRxTest,
       ReceivingFromChannelBetweenSocketCloseAndCloseWaitTriggerDoesNotCrash) {
  ASSERT_TRUE(relay()->Activate());
  CloseRemoteSocket();
  // Note: We do _not_ run the event loop here, because we want to test the
  // case where the channel data is received _before_ the
  // ZX_SOCKET_PEER_CLOSED wait fires.
  channel()->Receive(common::CreateStaticByteBuffer(kGoodChar));
}

TEST_F(
    SocketChannelRelayChannelRxTest,
    SocketCloseBetweenReceivingFromChannelAndSocketWritabilityDoesNotCrashOrHang) {
  ASSERT_TRUE(relay()->Activate());

  size_t n_junk_bytes = StuffSocket();
  ASSERT_TRUE(n_junk_bytes);
  channel()->Receive(common::CreateStaticByteBuffer(kGoodChar));
  RunLoopUntilIdle();

  ASSERT_TRUE(DiscardFromSocket(n_junk_bytes));
  CloseRemoteSocket();
  RunLoopUntilIdle();
}

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
  large_message.Fill(kGoodChar);
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
