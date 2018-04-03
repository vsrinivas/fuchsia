// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client.h"

#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel_test.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace gatt {
namespace {

class GATT_ClientTest : public l2cap::testing::FakeChannelTest {
 public:
  GATT_ClientTest() = default;
  ~GATT_ClientTest() override = default;

 protected:
  void SetUp() override {
    ChannelOptions options(l2cap::kATTChannelId);
    fake_chan_ = CreateFakeChannel(options);
    att_ = att::Bearer::Create(fake_chan_);
    client_ = std::make_unique<Client>(att_);
  }

  void TearDown() override {
    client_ = nullptr;
    att_ = nullptr;
  }

  att::Bearer* att() const { return att_.get(); }
  Client* client() const { return client_.get(); }
  l2cap::testing::FakeChannel* fake_chan() const { return fake_chan_.get(); }

 private:
  fbl::RefPtr<l2cap::testing::FakeChannel> fake_chan_;
  fxl::RefPtr<att::Bearer> att_;
  std::unique_ptr<Client> client_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GATT_ClientTest);
};

TEST_F(GATT_ClientTest, ExchangeMTUMalformedResponse) {
  constexpr uint16_t kPreferredMTU = 100;
  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x02,                // opcode: exchange MTU
      kPreferredMTU, 0x00  // client rx mtu: kPreferredMTU
  );

  // Initialize to a non-zero value.
  uint16_t final_mtu = kPreferredMTU;
  att::ErrorCode ecode;
  auto mtu_cb = [&, this](att::ErrorCode cb_code, uint16_t val) {
    final_mtu = val;
    ecode = cb_code;
  };

  att()->set_preferred_mtu(kPreferredMTU);

  // Initiate the request in a message loop task, as Expect() below blocks
  async::PostTask(message_loop()->async(),
                  [this, mtu_cb] { client()->ExchangeMTU(mtu_cb); });

  ASSERT_TRUE(Expect(kExpectedRequest));
  ASSERT_FALSE(fake_chan()->link_error());

  // Respond back with a malformed PDU. This should result in a link error and
  // the MTU request should fail.
  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x03,  // opcode: exchange MTU response
      30     // server rx mtu is one octet too short
  ));

  RunUntilIdle();

  EXPECT_EQ(att::ErrorCode::kInvalidPDU, ecode);
  EXPECT_EQ(0, final_mtu);
  EXPECT_TRUE(fake_chan()->link_error());
}

// Tests that the ATT "Request Not Supported" error results in the default MTU.
TEST_F(GATT_ClientTest, ExchangeMTUErrorNotSupported) {
  constexpr uint16_t kPreferredMTU = 100;
  constexpr uint16_t kInitialMTU = 50;
  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x02,                // opcode: exchange MTU
      kPreferredMTU, 0x00  // client rx mtu: kPreferredMTU
  );

  uint16_t final_mtu = 0;
  att::ErrorCode ecode;
  auto mtu_cb = [&, this](att::ErrorCode cb_code, uint16_t val) {
    final_mtu = val;
    ecode = cb_code;
  };

  // Set the initial MTU to something other than the default LE MTU since we
  // want to confirm that the MTU changes to the default.
  att()->set_mtu(kInitialMTU);
  att()->set_preferred_mtu(kPreferredMTU);

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(message_loop()->async(),
                  [this, mtu_cb] { client()->ExchangeMTU(mtu_cb); });

  ASSERT_TRUE(Expect(kExpectedRequest));

  // Respond with "Request Not Supported". This will cause us to switch to the
  // default MTU.
  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x02,        // request: exchange MTU
      0x00, 0x00,  // handle: 0
      0x06         // error: Request Not Supported
  ));

  RunUntilIdle();

  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
  EXPECT_EQ(att::kLEMinMTU, final_mtu);
  EXPECT_EQ(att::kLEMinMTU, att()->mtu());
}

TEST_F(GATT_ClientTest, ExchangeMTUErrorOther) {
  constexpr uint16_t kPreferredMTU = 100;
  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x02,                // opcode: exchange MTU
      kPreferredMTU, 0x00  // client rx mtu: kPreferredMTU
  );

  uint16_t final_mtu = kPreferredMTU;
  att::ErrorCode ecode;
  auto mtu_cb = [&, this](att::ErrorCode cb_code, uint16_t val) {
    final_mtu = val;
    ecode = cb_code;
  };

  att()->set_preferred_mtu(kPreferredMTU);
  EXPECT_EQ(att::kLEMinMTU, att()->mtu());

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(message_loop()->async(),
                  [this, mtu_cb] { client()->ExchangeMTU(mtu_cb); });

  ASSERT_TRUE(Expect(kExpectedRequest));

  // Respond with an error. The MTU should remain unchanged.
  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x02,        // request: exchange MTU
      0x00, 0x00,  // handle: 0
      0x0E         // error: Unlikely Error
  ));

  RunUntilIdle();

  EXPECT_EQ(att::ErrorCode::kUnlikelyError, ecode);
  EXPECT_EQ(0, final_mtu);
  EXPECT_EQ(att::kLEMinMTU, att()->mtu());
}

// Tests that the client rx MTU is selected when smaller.
TEST_F(GATT_ClientTest, ExchangeMTUSelectLocal) {
  constexpr uint16_t kPreferredMTU = 100;
  constexpr uint16_t kServerRxMTU = kPreferredMTU + 1;

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x02,                // opcode: exchange MTU
      kPreferredMTU, 0x00  // client rx mtu: kPreferredMTU
  );

  uint16_t final_mtu = 0;
  att::ErrorCode ecode;
  auto mtu_cb = [&, this](att::ErrorCode cb_code, uint16_t val) {
    final_mtu = val;
    ecode = cb_code;
  };

  att()->set_preferred_mtu(kPreferredMTU);

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(message_loop()->async(),
                  [this, mtu_cb] { client()->ExchangeMTU(mtu_cb); });

  ASSERT_TRUE(Expect(kExpectedRequest));
  ASSERT_EQ(att::kLEMinMTU, att()->mtu());

  // Respond with an error. The MTU should remain unchanged.
  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x03,                    // opcode: exchange MTU response
      kServerRxMTU, 0x00       // server rx mtu
  ));

  RunUntilIdle();

  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
  EXPECT_EQ(kPreferredMTU, final_mtu);
  EXPECT_EQ(kPreferredMTU, att()->mtu());
}

// Tests that the server rx MTU is selected when smaller.
TEST_F(GATT_ClientTest, ExchangeMTUSelectRemote) {
  constexpr uint16_t kPreferredMTU = 100;
  constexpr uint16_t kServerRxMTU = kPreferredMTU - 1;

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x02,                // opcode: exchange MTU
      kPreferredMTU, 0x00  // client rx mtu: kPreferredMTU
  );

  uint16_t final_mtu = 0;
  att::ErrorCode ecode;
  auto mtu_cb = [&, this](att::ErrorCode cb_code, uint16_t val) {
    final_mtu = val;
    ecode = cb_code;
  };

  att()->set_preferred_mtu(kPreferredMTU);

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(message_loop()->async(),
                  [this, mtu_cb] { client()->ExchangeMTU(mtu_cb); });

  ASSERT_TRUE(Expect(kExpectedRequest));
  ASSERT_EQ(att::kLEMinMTU, att()->mtu());

  // Respond with an error. The MTU should remain unchanged.
  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x03,                    // opcode: exchange MTU response
      kServerRxMTU, 0x00       // server rx mtu
  ));

  RunUntilIdle();

  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
  EXPECT_EQ(kServerRxMTU, final_mtu);
  EXPECT_EQ(kServerRxMTU, att()->mtu());
}

// Tests that the default MTU is selected when one of the MTUs is too small.
TEST_F(GATT_ClientTest, ExchangeMTUSelectDefault) {
  constexpr uint16_t kPreferredMTU = 100;
  constexpr uint16_t kServerRxMTU = 5;  // Smaller than the LE default MTU

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x02,                // opcode: exchange MTU
      kPreferredMTU, 0x00  // client rx mtu: kPreferredMTU
  );

  uint16_t final_mtu = 0;
  att::ErrorCode ecode;
  auto mtu_cb = [&, this](att::ErrorCode cb_code, uint16_t val) {
    final_mtu = val;
    ecode = cb_code;
  };

  att()->set_preferred_mtu(kPreferredMTU);

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(message_loop()->async(),
                  [this, mtu_cb] { client()->ExchangeMTU(mtu_cb); });

  ASSERT_TRUE(Expect(kExpectedRequest));
  ASSERT_EQ(att::kLEMinMTU, att()->mtu());

  // Respond with an error. The MTU should remain unchanged.
  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x03,                    // opcode: exchange MTU response
      kServerRxMTU, 0x00       // server rx mtu
  ));

  RunUntilIdle();

  EXPECT_EQ(att::ErrorCode::kNoError, ecode);
  EXPECT_EQ(att::kLEMinMTU, final_mtu);
  EXPECT_EQ(att::kLEMinMTU, att()->mtu());
}

}  // namespace
}  // namespace gatt
}  // namespace btlib
