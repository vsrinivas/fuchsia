// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bredr_signaling_channel.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"

#include "fake_channel_test.h"

namespace btlib {
namespace l2cap {
namespace internal {
namespace {

constexpr hci::ConnectionHandle kTestHandle = 0x0001;
constexpr uint8_t kTestCmdId = 97;
constexpr hci::Connection::Role kDeviceRole = hci::Connection::Role::kMaster;

class L2CAP_BrEdrSignalingChannelTest : public testing::FakeChannelTest {
 public:
  L2CAP_BrEdrSignalingChannelTest() = default;
  ~L2CAP_BrEdrSignalingChannelTest() override = default;

  L2CAP_BrEdrSignalingChannelTest(const L2CAP_BrEdrSignalingChannelTest&) =
      delete;
  L2CAP_BrEdrSignalingChannelTest& operator=(
      const L2CAP_BrEdrSignalingChannelTest&) = delete;

 protected:
  void SetUp() override {
    ChannelOptions options(kSignalingChannelId);
    options.conn_handle = kTestHandle;

    auto fake_chan = CreateFakeChannel(options);
    sig_ = std::make_unique<BrEdrSignalingChannel>(std::move(fake_chan),
                                                   kDeviceRole);
  }

  void TearDown() override { sig_ = nullptr; }

  BrEdrSignalingChannel* sig() const { return sig_.get(); }

 private:
  std::unique_ptr<BrEdrSignalingChannel> sig_;
};

TEST_F(L2CAP_BrEdrSignalingChannelTest, RespondsToEchoRequest) {
  auto cmd = common::CreateStaticByteBuffer(
      // Command header (Echo Request, length 1)
      0x08, kTestCmdId, 0x01, 0x00,

      // Payload
      0x23);

  bool called = false;
  auto cb = [&called, &cmd](auto packet) {
    called = true;
    EXPECT_EQ((*packet)[0], 0x09);  // ID for Echo Response
    // Command ID, payload length, and payload should match those of request.
    EXPECT_TRUE(ContainersEqual(cmd.view(1), packet->view(1)));
  };

  fake_chan()->SetSendCallback(std::move(cb), dispatcher());
  fake_chan()->Receive(cmd);

  RunUntilIdle();
  EXPECT_TRUE(called);
}

TEST_F(L2CAP_BrEdrSignalingChannelTest, RejectUnsolicitedEchoResponse) {
  auto cmd = common::CreateStaticByteBuffer(
      // Command header (Echo Response, length 1)
      0x09, kTestCmdId, 0x01, 0x00,

      // Payload
      0x23);

  auto expected = common::CreateStaticByteBuffer(
      // Command header (Command rejected, length 2)
      0x01, kTestCmdId, 0x02, 0x00,

      // Reason (Command not understood)
      0x00, 0x00);

  EXPECT_TRUE(ReceiveAndExpect(cmd, expected));
}

TEST_F(L2CAP_BrEdrSignalingChannelTest, IgnoreEmptyFrame) {
  bool send_cb_called = false;
  auto send_cb = [&send_cb_called](auto) { send_cb_called = true; };

  fake_chan()->SetSendCallback(std::move(send_cb), dispatcher());
  fake_chan()->Receive(common::BufferView());

  RunUntilIdle();
  EXPECT_FALSE(send_cb_called);
}

TEST_F(L2CAP_BrEdrSignalingChannelTest, RejectMalformedAdditionalCommand) {
  constexpr uint8_t kTestId0 = 14;
  constexpr uint8_t kTestId1 = 15;

  // Echo Request (see other test for command support), followed by an
  // incomplete command packet
  auto cmd = common::CreateStaticByteBuffer(
      // Command header (length 3)
      0x08, kTestId0, 0x03, 0x00,

      // Payload data
      'L', 'O', 'L',

      // Second command header
      0x08, kTestId1, 0x01, 0x00);

  // Echo Response packet
  auto rsp0 = common::CreateStaticByteBuffer(
      // Command header (length 3)
      0x09, kTestId0, 0x03, 0x00,

      // Payload data
      'L', 'O', 'L');

  // Command Reject packet
  auto rsp1 = common::CreateStaticByteBuffer(
      // Command header
      0x01, kTestId1, 0x02, 0x00,

      // Reason (Command not understood)
      0x00, 0x00);

  int cb_times_called = 0;
  auto send_cb = [&rsp0, &rsp1, &cb_times_called](auto packet) {
    if (cb_times_called == 0) {
      EXPECT_TRUE(common::ContainersEqual(rsp0, *packet));
    } else if (cb_times_called == 1) {
      EXPECT_TRUE(common::ContainersEqual(rsp1, *packet));
    }

    cb_times_called++;
  };

  fake_chan()->SetSendCallback(std::move(send_cb), dispatcher());
  fake_chan()->Receive(cmd);

  RunUntilIdle();
  EXPECT_EQ(2, cb_times_called);
}

TEST_F(L2CAP_BrEdrSignalingChannelTest, HandleMultipleCommands) {
  constexpr uint8_t kTestId0 = 14;
  constexpr uint8_t kTestId1 = 15;
  constexpr uint8_t kTestId2 = 16;

  auto cmd = common::CreateStaticByteBuffer(
      // Command header (Echo Request)
      0x08, kTestId0, 0x04, 0x00,

      // Payload data
      'L', 'O', 'L', 'Z',

      // Header with command to be rejected
      0xFF, kTestId1, 0x03, 0x00,

      // Payload data
      'L', 'O', 'L',

      // Command header (Echo Request, no payload data)
      0x08, kTestId2, 0x00, 0x00,

      // Additional command fragment to be dropped
      0xFF, 0x00);

  auto echo_rsp0 = common::CreateStaticByteBuffer(
      // Command header (Echo Response)
      0x09, kTestId0, 0x04, 0x00,

      // Payload data
      'L', 'O', 'L', 'Z');

  auto reject_rsp1 = common::CreateStaticByteBuffer(
      // Command header (Command Rejected)
      0x01, kTestId1, 0x02, 0x00,

      // Reason (Command not understood)
      0x00, 0x00);

  auto echo_rsp2 = common::CreateStaticByteBuffer(
      // Command header (Echo Response)
      0x09, kTestId2, 0x00, 0x00);

  int cb_times_called = 0;
  auto send_cb = [&echo_rsp0, &reject_rsp1, &echo_rsp2,
                  &cb_times_called](auto packet) {
    if (cb_times_called == 0) {
      EXPECT_TRUE(common::ContainersEqual(echo_rsp0, *packet));
    } else if (cb_times_called == 1) {
      EXPECT_TRUE(common::ContainersEqual(reject_rsp1, *packet));
    } else if (cb_times_called == 2) {
      EXPECT_TRUE(common::ContainersEqual(echo_rsp2, *packet));
    }

    cb_times_called++;
  };

  fake_chan()->SetSendCallback(std::move(send_cb), dispatcher());
  fake_chan()->Receive(cmd);

  RunUntilIdle();
  EXPECT_EQ(3, cb_times_called);
}

}  // namespace
}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
