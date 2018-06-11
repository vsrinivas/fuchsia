// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "signaling_channel.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"

#include "fake_channel_test.h"

namespace btlib {
namespace l2cap {
namespace internal {
namespace {

constexpr CommandCode kUnknownCommandCode = 0x00;
constexpr CommandCode kCommandCode = 0xFF;
constexpr hci::ConnectionHandle kTestHandle = 0x0001;
constexpr uint16_t kTestMTU = 100;

class TestSignalingChannel : public SignalingChannel {
 public:
  explicit TestSignalingChannel(fbl::RefPtr<Channel> chan)
      : SignalingChannel(std::move(chan), hci::Connection::Role::kMaster) {
    set_mtu(kTestMTU);
  }
  ~TestSignalingChannel() override = default;

  using PacketCallback = fit::function<void(const SignalingPacket& packet)>;
  void set_packet_callback(PacketCallback cb) { packet_cb_ = std::move(cb); }

  // Expose GetNextCommandId() as public so it can be called by tests below.
  using SignalingChannel::GetNextCommandId;

 private:
  // SignalingChannel overrides
  void DecodeRxUnit(const SDU& sdu, const PacketDispatchCallback& cb) override {
    SDU::Reader sdu_reader(&sdu);

    // Callback dumbly re-casts read data as a command packet and forwards it to
    // the dispatch function.
    sdu_reader.ReadNext(sdu.length(), [&cb](const common::ByteBuffer& data) {
      cb(SignalingPacket(&data, data.size() - sizeof(CommandHeader)));
    });
  }

  bool HandlePacket(const SignalingPacket& packet) override {
    if (packet.header().code == kUnknownCommandCode)
      return false;
    if (packet_cb_)
      packet_cb_(packet);
    return true;
  }

  PacketCallback packet_cb_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestSignalingChannel);
};

class L2CAP_SignalingChannelTest : public testing::FakeChannelTest {
 public:
  L2CAP_SignalingChannelTest() = default;
  ~L2CAP_SignalingChannelTest() override = default;

 protected:
  void SetUp() override {
    ChannelOptions options(kLESignalingChannelId);
    options.conn_handle = kTestHandle;

    fake_channel_inst_ = CreateFakeChannel(options);
    sig_ = std::make_unique<TestSignalingChannel>(fake_channel_inst_);
  }

  void TearDown() override {
    // Unless a test called DestroySig(), the signaling channel will outlive the
    // underlying channel.
    fake_channel_inst_ = nullptr;
    DestroySig();
  }

  TestSignalingChannel* sig() const { return sig_.get(); }

  void DestroySig() { sig_ = nullptr; }

 private:
  std::unique_ptr<TestSignalingChannel> sig_;

  // Own the fake channel so that its lifetime can span beyond that of |sig_|.
  fbl::RefPtr<Channel> fake_channel_inst_;

  FXL_DISALLOW_COPY_AND_ASSIGN(L2CAP_SignalingChannelTest);
};

TEST_F(L2CAP_SignalingChannelTest, IgnoreEmptyFrame) {
  bool send_cb_called = false;
  auto send_cb = [&send_cb_called](auto) { send_cb_called = true; };

  fake_chan()->SetSendCallback(std::move(send_cb), dispatcher());
  fake_chan()->Receive(common::BufferView());

  RunUntilIdle();
  EXPECT_FALSE(send_cb_called);
}

TEST_F(L2CAP_SignalingChannelTest, Reject) {
  constexpr uint8_t kTestId = 14;

  // Command Reject packet.
  auto expected = common::CreateStaticByteBuffer(
      // Command header
      0x01, kTestId, 0x02, 0x00,

      // Reason (Command not understood)
      0x00, 0x00);

  // A command that TestSignalingChannel does not support.
  auto cmd = common::CreateStaticByteBuffer(
      // header
      kUnknownCommandCode, kTestId, 0x04, 0x00,

      // data
      'L', 'O', 'L', 'Z');

  EXPECT_TRUE(ReceiveAndExpect(cmd, expected));
}

TEST_F(L2CAP_SignalingChannelTest, RejectCommandCodeZero) {
  constexpr uint8_t kTestId = 14;

  // Command Reject packet.
  auto expected = common::CreateStaticByteBuffer(
      // Command header
      0x01, kTestId, 0x02, 0x00,

      // Reason (Command not understood)
      0x00, 0x00);

  // A command that TestSignalingChannel does not support.
  auto cmd = common::CreateStaticByteBuffer(
      // header
      0x00, kTestId, 0x04, 0x00,

      // data
      'L', 'O', 'L', 'Z');

  EXPECT_TRUE(ReceiveAndExpect(cmd, expected));
}

TEST_F(L2CAP_SignalingChannelTest, InvalidMTU) {
  constexpr uint8_t kTestId = 14;
  constexpr uint16_t kTooSmallMTU = 7;

  // Command Reject packet.
  auto expected = common::CreateStaticByteBuffer(
      // Command header
      0x01, kTestId, 0x04, 0x00,

      // Reason (Signaling MTU exceeded)
      0x01, 0x00,

      // The supported MTU
      static_cast<uint8_t>(kTooSmallMTU), 0x00);

  // A command that is one octet larger than the MTU.
  auto cmd = common::CreateStaticByteBuffer(
      // header
      kCommandCode, kTestId, 0x04, 0x00,

      // data
      'L', 'O', 'L', 'Z');

  sig()->set_mtu(kTooSmallMTU);
  EXPECT_TRUE(ReceiveAndExpect(cmd, expected));
}

TEST_F(L2CAP_SignalingChannelTest, HandlePacket) {
  constexpr uint8_t kTestId = 14;

  // A command that TestSignalingChannel supports.
  auto cmd = common::CreateStaticByteBuffer(
      // header
      kCommandCode, kTestId, 0x04, 0x00,

      // data
      'L', 'O', 'L', 'Z');

  bool called = false;
  sig()->set_packet_callback([&cmd, &called, this](auto packet) {
    EXPECT_TRUE(common::ContainersEqual(cmd, packet.data()));
    called = true;
  });

  fake_chan()->Receive(cmd);

  RunUntilIdle();
  EXPECT_TRUE(called);
}

TEST_F(L2CAP_SignalingChannelTest, UseChannelAfterSignalFree) {
  // Destroy the underlying channel's user (SignalingChannel).
  DestroySig();

  // Ensure that the underlying channel is still alive.
  ASSERT_TRUE(static_cast<bool>(fake_chan()));

  // SignalingChannel is expected to deactivate the channel if it doesn't own
  // it. Either way, the channel isn't in a state that can receive test data.
  EXPECT_FALSE(fake_chan()->activated());

  // Ensure that closing the channel (possibly firing callback) is OK.
  fake_chan()->Close();

  RunUntilIdle();
}

TEST_F(L2CAP_SignalingChannelTest, ValidRequestCommandIds) {
  EXPECT_EQ(0x01, sig()->GetNextCommandId());
  for (int i = 0; i < 256; i++) {
    EXPECT_NE(0x00, sig()->GetNextCommandId());
  }
}

}  // namespace
}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
