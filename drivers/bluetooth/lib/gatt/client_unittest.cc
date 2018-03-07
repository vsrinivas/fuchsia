// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client.h"

#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel_test.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace gatt {
namespace {

using common::HostError;

constexpr common::UUID kTestUuid1((uint16_t)0xDEAD);
constexpr common::UUID kTestUuid2((uint16_t)0xBEEF);
constexpr common::UUID kTestUuid3({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
                                   14, 15});

// clang-format off
const auto kDiscoverAllPrimaryRequest = common::CreateStaticByteBuffer(
    0x10,        // opcode: read by group type request
    0x01, 0x00,  // start handle: 0x0001
    0xFF, 0xFF,  // end handle: 0xFFFF
    0x00, 0x28   // type: primary service (0x2800)
);
// clang-format on

void NopSvcCallback(const gatt::ServiceData&) {}

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
  att::Status status;
  auto mtu_cb = [&, this](att::Status cb_status, uint16_t val) {
    final_mtu = val;
    status = cb_status;
  };

  att()->set_preferred_mtu(kPreferredMTU);

  // Initiate the request in a loop task, as Expect() below blocks
  async::PostTask(message_loop()->async(),
                  [this, mtu_cb] { client()->ExchangeMTU(mtu_cb); });

  ASSERT_TRUE(Expect(kExpectedRequest));
  ASSERT_FALSE(fake_chan()->link_error());

  // Respond back with a malformed PDU. This should cause a link error and the
  // MTU request should fail.
  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x03,  // opcode: exchange MTU response
      30     // server rx mtu is one octet too short
  ));

  RunUntilIdle();

  EXPECT_EQ(HostError::kPacketMalformed, status.error());
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
  att::Status status;
  auto mtu_cb = [&, this](att::Status cb_status, uint16_t val) {
    final_mtu = val;
    status = cb_status;
  };

  // Set the initial MTU to something other than the default LE MTU since we
  // want to confirm that the MTU changes to the default.
  att()->set_mtu(kInitialMTU);
  att()->set_preferred_mtu(kPreferredMTU);

  // Initiate the request on the loop since Expect() below blocks.
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

  EXPECT_FALSE(status);
  EXPECT_EQ(att::ErrorCode::kRequestNotSupported, status.protocol_error());
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
  att::Status status;
  auto mtu_cb = [&, this](att::Status cb_status, uint16_t val) {
    final_mtu = val;
    status = cb_status;
  };

  att()->set_preferred_mtu(kPreferredMTU);
  EXPECT_EQ(att::kLEMinMTU, att()->mtu());

  // Initiate the request on the loop since Expect() below blocks.
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

  EXPECT_EQ(att::ErrorCode::kUnlikelyError, status.protocol_error());
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
  att::Status status;
  auto mtu_cb = [&, this](att::Status cb_status, uint16_t val) {
    final_mtu = val;
    status = cb_status;
  };

  att()->set_preferred_mtu(kPreferredMTU);

  // Initiate the request on the loop since Expect() below blocks.
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

  EXPECT_TRUE(status);
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
  att::Status status;
  auto mtu_cb = [&, this](att::Status cb_status, uint16_t val) {
    final_mtu = val;
    status = cb_status;
  };

  att()->set_preferred_mtu(kPreferredMTU);

  // Initiate the request on the loop since Expect() below blocks.
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

  EXPECT_TRUE(status);
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
  att::Status status;
  auto mtu_cb = [&, this](att::Status cb_status, uint16_t val) {
    final_mtu = val;
    status = cb_status;
  };

  att()->set_preferred_mtu(kPreferredMTU);

  // Initiate the request on the loop since Expect() below blocks.
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

  EXPECT_TRUE(status);
  EXPECT_EQ(att::kLEMinMTU, final_mtu);
  EXPECT_EQ(att::kLEMinMTU, att()->mtu());
}

TEST_F(GATT_ClientTest, DiscoverAllPrimaryResponseTooShort) {
  att::Status status;
  auto res_cb = [this, &status](att::Status cb_res) { status = cb_res; };

  // Initiate the request on the loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverPrimaryServices(NopSvcCallback, res_cb);
  });

  ASSERT_TRUE(Expect(kDiscoverAllPrimaryRequest));

  // Respond back with a malformed payload.
  fake_chan()->Receive(common::CreateStaticByteBuffer(0x11));

  RunUntilIdle();

  EXPECT_EQ(HostError::kPacketMalformed, status.error());
}

TEST_F(GATT_ClientTest, DiscoverAllPrimaryMalformedDataLength) {
  att::Status status;
  auto res_cb = [this, &status](att::Status cb_res) { status = cb_res; };

  // Initiate the request on the loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverPrimaryServices(NopSvcCallback, res_cb);
  });

  ASSERT_TRUE(Expect(kDiscoverAllPrimaryRequest));

  // Respond back with an unexpected data length. This is 6 for services with a
  // 16-bit UUID (start (2) + end (2) + uuid (2)) and 20 for 128-bit
  // (start (2) + end (2) + uuid (16)).
  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x11,                // opcode: read by group type response
      7,                   // data length: 7 (not 6 or 20)
      0, 1, 2, 3, 4, 5, 6  // one entry of length 7, which will be ignored
      ));

  RunUntilIdle();

  EXPECT_EQ(HostError::kPacketMalformed, status.error());
}

TEST_F(GATT_ClientTest, DiscoverAllPrimaryMalformedAttrDataList) {
  att::Status status;
  auto res_cb = [this, &status](att::Status cb_res) { status = cb_res; };

  // Initiate the request on the loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverPrimaryServices(NopSvcCallback, res_cb);
  });

  ASSERT_TRUE(Expect(kDiscoverAllPrimaryRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x11,              // opcode: read by group type response
      6,                 // data length: 6 (16-bit UUIDs)
      0, 1, 2, 3, 4, 5,  // entry 1: correct size
      0, 1, 2, 3, 4      // entry 2: incorrect size
      ));

  RunUntilIdle();

  EXPECT_EQ(HostError::kPacketMalformed, status.error());
}

// Tests that we handle an empty attribute data list properly. In practice, the
// server would send an "Attribute Not Found" error instead but our stack treats
// an empty data list as not an error.
TEST_F(GATT_ClientTest, DiscoverAllPrimaryEmptyDataList) {
  att::Status status(HostError::kFailed);
  auto res_cb = [this, &status](att::Status cb_res) { status = cb_res; };

  // Initiate the request on the loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverPrimaryServices(NopSvcCallback, res_cb);
  });

  ASSERT_TRUE(Expect(kDiscoverAllPrimaryRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x11,  // opcode: read by group type response
      6      // data length: 6 (16-bit UUIDs)
             // data list is empty
      ));

  RunUntilIdle();
  EXPECT_TRUE(status);
}

// The first request results in "Attribute Not Found".
TEST_F(GATT_ClientTest, DiscoverAllPrimaryAttributeNotFound) {
  att::Status status(HostError::kFailed);
  auto res_cb = [this, &status](att::Status cb_res) { status = cb_res; };

  // Initiate the request on the loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverPrimaryServices(NopSvcCallback, res_cb);
  });

  ASSERT_TRUE(Expect(kDiscoverAllPrimaryRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x10,        // request: read by group type
      0x01, 0x00,  // handle: 0x0001
      0x0A         // error: Attribute Not Found
      ));

  RunUntilIdle();

  // The procedure succeeds with no services.
  EXPECT_TRUE(status);
}

// The first request results in an error.
TEST_F(GATT_ClientTest, DiscoverAllPrimaryError) {
  att::Status status(HostError::kFailed);
  auto res_cb = [this, &status](att::Status cb_res) { status = cb_res; };

  // Initiate the request on the loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverPrimaryServices(NopSvcCallback, res_cb);
  });

  ASSERT_TRUE(Expect(kDiscoverAllPrimaryRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x10,        // request: read by group type
      0x01, 0x00,  // handle: 0x0001
      0x06         // error: Request Not Supported
      ));

  RunUntilIdle();

  EXPECT_TRUE(status.is_protocol_error());
  EXPECT_EQ(att::ErrorCode::kRequestNotSupported, status.protocol_error());
}

TEST_F(GATT_ClientTest, DiscoverAllPrimaryMalformedServiceRange) {
  att::Status status(HostError::kFailed);
  auto res_cb = [this, &status](att::Status cb_res) { status = cb_res; };

  // Initiate the request on the loop since Expect() below blocks.
  async::PostTask(dispatcher(), [this, res_cb] {
    client()->DiscoverPrimaryServices(NopSvcCallback, res_cb);
  });

  ASSERT_TRUE(Expect(kDiscoverAllPrimaryRequest));

  // Return a service where start > end.
  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x11,        // opcode: read by group type response
      0x06,        // data length: 6 (16-bit UUIDs)
      0x02, 0x00,  // svc 1 start: 0x0002
      0x01, 0x00   // svc 1 end: 0x0001
      ));

  RunUntilIdle();

  // The procedure should be over since the last service in the payload has
  // end handle 0xFFFF.
  EXPECT_FALSE(status);
  EXPECT_EQ(HostError::kPacketMalformed, status.error());
}

TEST_F(GATT_ClientTest, DiscoverAllPrimary16BitResultsSingleRequest) {
  att::Status status(HostError::kFailed);
  auto res_cb = [this, &status](att::Status cb_res) { status = cb_res; };

  std::vector<ServiceData> services;
  auto svc_cb = [&services](const ServiceData& svc) {
    services.push_back(svc);
  };

  // Initiate the request on the loop since Expect() below blocks.
  async::PostTask(dispatcher(), [this, svc_cb, res_cb] {
    client()->DiscoverPrimaryServices(svc_cb, res_cb);
  });

  ASSERT_TRUE(Expect(kDiscoverAllPrimaryRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x11,        // opcode: read by group type response
      0x06,        // data length: 6 (16-bit UUIDs)
      0x01, 0x00,  // svc 1 start: 0x0001
      0x05, 0x00,  // svc 1 end: 0x0005
      0xAD, 0xDE,  // svc 1 uuid: 0xDEAD
      0x06, 0x00,  // svc 2 start: 0x0006
      0xFF, 0xFF,  // svc 2 end: 0xFFFF
      0xEF, 0xBE   // svc 2 uuid: 0xBEEF
      ));

  RunUntilIdle();

  // The procedure should be over since the last service in the payload has
  // end handle 0xFFFF.
  EXPECT_TRUE(status);
  EXPECT_EQ(2u, services.size());
  EXPECT_EQ(0x0001, services[0].range_start);
  EXPECT_EQ(0x0005, services[0].range_end);
  EXPECT_EQ(kTestUuid1, services[0].type);
  EXPECT_EQ(0x0006, services[1].range_start);
  EXPECT_EQ(0xFFFF, services[1].range_end);
  EXPECT_EQ(kTestUuid2, services[1].type);
}

TEST_F(GATT_ClientTest, DiscoverAllPrimary128BitResultSingleRequest) {
  att::Status status(HostError::kFailed);
  auto res_cb = [this, &status](att::Status cb_res) { status = cb_res; };

  std::vector<ServiceData> services;
  auto svc_cb = [&services](const ServiceData& svc) {
    services.push_back(svc);
  };

  // Initiate the request on the loop since Expect() below blocks.
  async::PostTask(dispatcher(), [this, svc_cb, res_cb] {
    client()->DiscoverPrimaryServices(svc_cb, res_cb);
  });

  ASSERT_TRUE(Expect(kDiscoverAllPrimaryRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x11,        // opcode: read by group type response
      0x14,        // data length: 20 (128-bit UUIDs)
      0x01, 0x00,  // svc 1 start: 0x0008
      0xFF, 0xFF,  // svc 1 end: 0xFFFF

      // UUID matches |kTestUuid3| declared above.
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));

  RunUntilIdle();

  // The procedure should be over since the last service in the payload has
  // end handle 0xFFFF.
  EXPECT_TRUE(status);
  EXPECT_EQ(1u, services.size());
  EXPECT_EQ(0x0001, services[0].range_start);
  EXPECT_EQ(0xFFFF, services[0].range_end);
  EXPECT_EQ(kTestUuid3, services[0].type);
}

TEST_F(GATT_ClientTest, DiscoverAllPrimaryMultipleRequests) {
  const auto kExpectedRequest1 = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type request
      0x01, 0x00,  // start handle: 0x0001
      0xFF, 0xFF,  // end handle: 0xFFFF
      0x00, 0x28   // type: primary service (0x2800)
  );
  const auto kExpectedRequest2 = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type request
      0x08, 0x00,  // start handle: 0x0008
      0xFF, 0xFF,  // end handle: 0xFFFF
      0x00, 0x28   // type: primary service (0x2800)
  );
  const auto kExpectedRequest3 = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type request
      0x0A, 0x00,  // start handle: 0x000A
      0xFF, 0xFF,  // end handle: 0xFFFF
      0x00, 0x28   // type: primary service (0x2800)
  );

  att::Status status(HostError::kFailed);
  auto res_cb = [this, &status](att::Status cb_res) { status = cb_res; };

  std::vector<ServiceData> services;
  auto svc_cb = [&services](const ServiceData& svc) {
    services.push_back(svc);
  };

  // Initiate the request on the loop since Expect() below blocks.
  async::PostTask(dispatcher(), [this, svc_cb, res_cb] {
    client()->DiscoverPrimaryServices(svc_cb, res_cb);
  });

  ASSERT_TRUE(Expect(kExpectedRequest1));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x11,        // opcode: read by group type response
      0x06,        // data length: 6 (16-bit UUIDs)
      0x01, 0x00,  // svc 1 start: 0x0001
      0x05, 0x00,  // svc 1 end: 0x0005
      0xAD, 0xDE,  // svc 1 uuid: 0xDEAD
      0x06, 0x00,  // svc 2 start: 0x0006
      0x07, 0x00,  // svc 2 end: 0x0007
      0xEF, 0xBE   // svc 2 uuid: 0xBEEF
      ));

  // The client should follow up with a second request following the last end
  // handle.
  ASSERT_TRUE(Expect(kExpectedRequest2));

  // Respond with one 128-bit service UUID.

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x11,        // opcode: read by group type response
      0x14,        // data length: 20 (128-bit UUIDs)
      0x08, 0x00,  // svc 1 start: 0x0008
      0x09, 0x00,  // svc 1 end: 0x0009

      // UUID matches |kTestUuid3| declared above.
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));

  // The client should follow up with a third request following the last end
  // handle.
  ASSERT_TRUE(Expect(kExpectedRequest3));

  // Terminate the procedure with an error response.
  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x10,        // request: read by group type
      0x0A, 0x00,  // handle: 0x000A
      0x0A         // error: Attribute Not Found
      ));

  RunUntilIdle();

  // The procedure should be over since the last service in the payload has
  // end handle 0xFFFF.
  EXPECT_TRUE(status);
  EXPECT_EQ(3u, services.size());

  EXPECT_EQ(0x0001, services[0].range_start);
  EXPECT_EQ(0x0005, services[0].range_end);
  EXPECT_EQ(kTestUuid1, services[0].type);

  EXPECT_EQ(0x0006, services[1].range_start);
  EXPECT_EQ(0x0007, services[1].range_end);
  EXPECT_EQ(kTestUuid2, services[1].type);

  EXPECT_EQ(0x0008, services[2].range_start);
  EXPECT_EQ(0x0009, services[2].range_end);
  EXPECT_EQ(kTestUuid3, services[2].type);
}

}  // namespace
}  // namespace gatt
}  // namespace btlib
