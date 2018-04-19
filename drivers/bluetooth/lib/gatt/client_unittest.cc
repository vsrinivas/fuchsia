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
void NopChrcCallback(const gatt::CharacteristicData&) {}

class GATT_ClientTest : public l2cap::testing::FakeChannelTest {
 public:
  GATT_ClientTest() = default;
  ~GATT_ClientTest() override = default;

 protected:
  void SetUp() override {
    ChannelOptions options(l2cap::kATTChannelId);
    fake_chan_ = CreateFakeChannel(options);
    att_ = att::Bearer::Create(fake_chan_);
    client_ = Client::Create(att_);
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
  async::PostTask(dispatcher(),
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
  async::PostTask(dispatcher(),
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
  async::PostTask(dispatcher(),
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
  async::PostTask(dispatcher(),
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
  async::PostTask(dispatcher(),
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
  async::PostTask(dispatcher(),
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
  auto res_cb = [this, &status](att::Status val) { status = val; };

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
  auto res_cb = [this, &status](att::Status val) { status = val; };

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
  auto res_cb = [this, &status](att::Status val) { status = val; };

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
  auto res_cb = [this, &status](att::Status val) { status = val; };

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
  auto res_cb = [this, &status](att::Status val) { status = val; };

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
  auto res_cb = [this, &status](att::Status val) { status = val; };

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
  auto res_cb = [this, &status](att::Status val) { status = val; };

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
  auto res_cb = [this, &status](att::Status val) { status = val; };

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
  auto res_cb = [this, &status](att::Status val) { status = val; };

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
  auto res_cb = [this, &status](att::Status val) { status = val; };

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

TEST_F(GATT_ClientTest, DiscoverCharacHandlesEqual) {
  constexpr att::Handle kStart = 0x0001;
  constexpr att::Handle kEnd = 0x0001;

  att::Status status(HostError::kFailed);  // Initialize as error
  auto res_cb = [this, &status](att::Status val) { status = val; };

  // Should succeed immediately.
  client()->DiscoverCharacteristics(kStart, kEnd, NopChrcCallback, res_cb);
  EXPECT_TRUE(status);
}

TEST_F(GATT_ClientTest, DiscoverCharacResponseTooShort) {
  constexpr att::Handle kStart = 0x0001;
  constexpr att::Handle kEnd = 0xFFFF;

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x01, 0x00,  // start handle: 0x0001
      0xFF, 0xFF,  // end handle: 0xFFFF
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );

  att::Status status;
  auto res_cb = [this, &status](att::Status val) { status = val; };

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverCharacteristics(kStart, kEnd, NopChrcCallback, res_cb);
  });

  ASSERT_TRUE(Expect(kExpectedRequest));

  // Respond back with a malformed payload.
  fake_chan()->Receive(common::CreateStaticByteBuffer(0x09));

  RunUntilIdle();

  EXPECT_EQ(HostError::kPacketMalformed, status.error());
}

TEST_F(GATT_ClientTest, DiscoverCharacMalformedDataLength) {
  constexpr att::Handle kStart = 0x0001;
  constexpr att::Handle kEnd = 0xFFFF;

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x01, 0x00,  // start handle: 0x0001
      0xFF, 0xFF,  // end handle: 0xFFFF
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );

  att::Status status;
  auto res_cb = [this, &status](att::Status val) { status = val; };

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverCharacteristics(kStart, kEnd, NopChrcCallback, res_cb);
  });

  ASSERT_TRUE(Expect(kExpectedRequest));

  // Respond back with an unexpected data length. This is 7 for characteristics
  // with a 16-bit UUID (handle (2) + props (1) + value handle (2) + uuid (2))
  // and 21 for 128-bit (handle (2) + props (1) + value handle (2) + uuid (16)).
  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x09,                   // opcode: read by type response
      8,                      // data length: 8 (not 7 or 21)
      0, 1, 2, 3, 4, 5, 6, 7  // one entry of length 8, which will be ignored
      ));

  RunUntilIdle();

  EXPECT_EQ(HostError::kPacketMalformed, status.error());
}

TEST_F(GATT_ClientTest, DiscoverCharacMalformedAttrDataList) {
  constexpr att::Handle kStart = 0x0001;
  constexpr att::Handle kEnd = 0xFFFF;

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x01, 0x00,  // start handle: 0x0001
      0xFF, 0xFF,  // end handle: 0xFFFF
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );

  att::Status status;
  auto res_cb = [this, &status](att::Status val) { status = val; };

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverCharacteristics(kStart, kEnd, NopChrcCallback, res_cb);
  });

  ASSERT_TRUE(Expect(kExpectedRequest));

  // Respond back with an unexpected data length. This is 7 for characteristics
  // with a 16-bit UUID (handle (2) + props (1) + value handle (2) + uuid (2))
  // and 21 for 128-bit (handle (2) + props (1) + value handle (2) + uuid (16)).
  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x09,                 // opcode: read by type response
      7,                    // data length: 7 (16-bit UUIDs)
      0, 1, 2, 3, 4, 5, 6,  // entry 1: correct size
      0, 1, 2, 3, 4, 5      // entry 2: incorrect size
      ));

  RunUntilIdle();

  EXPECT_EQ(HostError::kPacketMalformed, status.error());
}

TEST_F(GATT_ClientTest, DiscoverCharacEmptyDataList) {
  constexpr att::Handle kStart = 0x0001;
  constexpr att::Handle kEnd = 0xFFFF;

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x01, 0x00,  // start handle: 0x0001
      0xFF, 0xFF,  // end handle: 0xFFFF
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );

  att::Status status;
  auto res_cb = [this, &status](att::Status val) { status = val; };

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverCharacteristics(kStart, kEnd, NopChrcCallback, res_cb);
  });

  ASSERT_TRUE(Expect(kExpectedRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x09,  // opcode: read by type response
      7      // data length: 7 (16-bit UUIDs)
             // data list empty
      ));

  RunUntilIdle();

  EXPECT_TRUE(status);
}

TEST_F(GATT_ClientTest, DiscoverCharacAttributeNotFound) {
  constexpr att::Handle kStart = 0x0001;
  constexpr att::Handle kEnd = 0xFFFF;

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x01, 0x00,  // start handle: 0x0001
      0xFF, 0xFF,  // end handle: 0xFFFF
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );

  att::Status status;
  auto res_cb = [this, &status](att::Status val) { status = val; };

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverCharacteristics(kStart, kEnd, NopChrcCallback, res_cb);
  });

  ASSERT_TRUE(Expect(kExpectedRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x01, 0x00,  // handle: 0x0001
      0x0A         // error: Attribute Not Found
      ));

  RunUntilIdle();

  // Attribute Not Found error means the procedure is over.
  EXPECT_TRUE(status);
}

TEST_F(GATT_ClientTest, DiscoverCharacError) {
  constexpr att::Handle kStart = 0x0001;
  constexpr att::Handle kEnd = 0xFFFF;

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x01, 0x00,  // start handle: 0x0001
      0xFF, 0xFF,  // end handle: 0xFFFF
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );

  att::Status status;
  auto res_cb = [this, &status](att::Status val) { status = val; };

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverCharacteristics(kStart, kEnd, NopChrcCallback, res_cb);
  });

  ASSERT_TRUE(Expect(kExpectedRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x01, 0x00,  // handle: 0x0001
      0x06         // error: Request Not Supported
      ));

  RunUntilIdle();

  EXPECT_TRUE(status.is_protocol_error());
  EXPECT_EQ(att::ErrorCode::kRequestNotSupported, status.protocol_error());
}

TEST_F(GATT_ClientTest, DiscoverCharac16BitResultsSingleRequest) {
  constexpr att::Handle kStart = 0x0001;
  constexpr att::Handle kEnd = 0x0005;

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x01, 0x00,  // start handle: 0x0001
      0x05, 0x00,  // end handle: 0x0005
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );

  att::Status status;
  auto res_cb = [this, &status](att::Status val) { status = val; };

  std::vector<CharacteristicData> chrcs;
  auto chrc_cb = [&chrcs](const CharacteristicData& chrc) {
    chrcs.push_back(chrc);
  };

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverCharacteristics(kStart, kEnd, chrc_cb, res_cb);
  });

  ASSERT_TRUE(Expect(kExpectedRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x09,        // opcode: read by type response
      0x07,        // data length: 7 (16-bit UUIDs)
      0x03, 0x00,  // chrc 1 handle
      0x00,        // chrc 1 properties
      0x04, 0x00,  // chrc 1 value handle
      0xAD, 0xDE,  // chrc 1 uuid: 0xDEAD
      0x05, 0x00,  // chrc 2 handle (0x0005 is the end of the requested range)
      0x01,        // chrc 2 properties
      0x06, 0x00,  // chrc 2 value handle
      0xEF, 0xBE   // chrc 2 uuid: 0xBEEF
      ));

  RunUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_EQ(2u, chrcs.size());
  EXPECT_EQ(0x0003, chrcs[0].handle);
  EXPECT_EQ(0, chrcs[0].properties);
  EXPECT_EQ(0x0004, chrcs[0].value_handle);
  EXPECT_EQ(kTestUuid1, chrcs[0].type);
  EXPECT_EQ(0x0005, chrcs[1].handle);
  EXPECT_EQ(1, chrcs[1].properties);
  EXPECT_EQ(0x0006, chrcs[1].value_handle);
  EXPECT_EQ(kTestUuid2, chrcs[1].type);
}

TEST_F(GATT_ClientTest, DiscoverCharac128BitResultsSingleRequest) {
  constexpr att::Handle kStart = 0x0001;
  constexpr att::Handle kEnd = 0x0005;

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x01, 0x00,  // start handle: 0x0001
      0x05, 0x00,  // end handle: 0x0005
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );

  att::Status status;
  auto res_cb = [this, &status](att::Status val) { status = val; };

  std::vector<CharacteristicData> chrcs;
  auto chrc_cb = [&chrcs](const CharacteristicData& chrc) {
    chrcs.push_back(chrc);
  };

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverCharacteristics(kStart, kEnd, chrc_cb, res_cb);
  });

  ASSERT_TRUE(Expect(kExpectedRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x09,        // opcode: read by type response
      0x15,        // data length: 21 (128-bit UUIDs)
      0x05, 0x00,  // chrc handle
      0x00,        // chrc properties
      0x06, 0x00,  // chrc value handle

      // UUID matches |kTestUuid3| declared above.
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));

  RunUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_EQ(1u, chrcs.size());
  EXPECT_EQ(0x0005, chrcs[0].handle);
  EXPECT_EQ(0, chrcs[0].properties);
  EXPECT_EQ(0x0006, chrcs[0].value_handle);
  EXPECT_EQ(kTestUuid3, chrcs[0].type);
}

TEST_F(GATT_ClientTest, DiscoverCharacMultipleRequests) {
  constexpr att::Handle kStart = 0x0001;
  constexpr att::Handle kEnd = 0xFFFF;

  const auto kExpectedRequest1 = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x01, 0x00,  // start handle: 0x0001
      0xFF, 0xFF,  // end handle: 0xFFFF
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );
  const auto kExpectedRequest2 = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x06, 0x00,  // start handle: 0x0006
      0xFF, 0xFF,  // end handle: 0xFFFF
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );
  const auto kExpectedRequest3 = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x08, 0x00,  // start handle: 0x0008
      0xFF, 0xFF,  // end handle: 0xFFFF
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );

  att::Status status;
  auto res_cb = [this, &status](att::Status val) { status = val; };

  std::vector<CharacteristicData> chrcs;
  auto chrc_cb = [&chrcs](const CharacteristicData& chrc) {
    chrcs.push_back(chrc);
  };

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverCharacteristics(kStart, kEnd, chrc_cb, res_cb);
  });

  ASSERT_TRUE(Expect(kExpectedRequest1));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x09,        // opcode: read by type response
      0x07,        // data length: 7 (16-bit UUIDs)
      0x03, 0x00,  // chrc 1 handle
      0x00,        // chrc 1 properties
      0x04, 0x00,  // chrc 1 value handle
      0xAD, 0xDE,  // chrc 1 uuid: 0xDEAD
      0x05, 0x00,  // chrc 2 handle
      0x01,        // chrc 2 properties
      0x06, 0x00,  // chrc 2 value handle
      0xEF, 0xBE   // chrc 2 uuid: 0xBEEF
      ));

  // The client should follow up with a second request following the last
  // characteristic declaration handle.
  ASSERT_TRUE(Expect(kExpectedRequest2));

  // Respond with one characteristic with a 128-bit UUID

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x09,        // opcode: read by type response
      0x15,        // data length: 21 (128-bit UUIDs)
      0x07, 0x00,  // chrc handle
      0x00,        // chrc properties
      0x08, 0x00,  // chrc value handle

      // UUID matches |kTestUuid3| declared above.
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));

  // The client should follow up with a third request following the last
  // characteristic declaration handle.
  ASSERT_TRUE(Expect(kExpectedRequest3));

  // Terminate the procedure with an error response.
  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x0A, 0x00,  // handle: 0x000A
      0x0A         // error: Attribute Not Found
      ));

  RunUntilIdle();

  EXPECT_TRUE(status);
  EXPECT_EQ(3u, chrcs.size());

  EXPECT_EQ(0x0003, chrcs[0].handle);
  EXPECT_EQ(0, chrcs[0].properties);
  EXPECT_EQ(0x0004, chrcs[0].value_handle);
  EXPECT_EQ(kTestUuid1, chrcs[0].type);

  EXPECT_EQ(0x0005, chrcs[1].handle);
  EXPECT_EQ(1, chrcs[1].properties);
  EXPECT_EQ(0x0006, chrcs[1].value_handle);
  EXPECT_EQ(kTestUuid2, chrcs[1].type);

  EXPECT_EQ(0x0007, chrcs[2].handle);
  EXPECT_EQ(0, chrcs[2].properties);
  EXPECT_EQ(0x0008, chrcs[2].value_handle);
  EXPECT_EQ(kTestUuid3, chrcs[2].type);
}

// Expects the discovery procedure to end with an error if a batch contains
// results that are from before requested range.
TEST_F(GATT_ClientTest, CharacteristicDiscoveryResultsBeforeRange) {
  constexpr att::Handle kStart = 0x0002;
  constexpr att::Handle kEnd = 0x0005;

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x02, 0x00,  // start handle: 0x0002
      0x05, 0x00,  // end handle: 0x0005
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );

  att::Status status;
  auto res_cb = [this, &status](att::Status val) { status = val; };

  std::vector<CharacteristicData> chrcs;
  auto chrc_cb = [&chrcs](const CharacteristicData& chrc) {
    chrcs.push_back(chrc);
  };

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverCharacteristics(kStart, kEnd, chrc_cb, res_cb);
  });

  ASSERT_TRUE(Expect(kExpectedRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x09,        // opcode: read by type response
      0x07,        // data length: 7 (16-bit UUIDs)
      0x01, 0x00,  // chrc 1 handle (handle is before the range)
      0x00,        // chrc 1 properties
      0x02, 0x00,  // chrc 1 value handle
      0xAD, 0xDE   // chrc 1 uuid: 0xDEAD
      ));

  RunUntilIdle();

  EXPECT_EQ(HostError::kPacketMalformed, status.error());
  EXPECT_TRUE(chrcs.empty());
}

// Expects the discovery procedure to end with an error if a batch contains
// results that are from beyond the requested range.
TEST_F(GATT_ClientTest, CharacteristicDiscoveryResultsBeyondRange) {
  constexpr att::Handle kStart = 0x0002;
  constexpr att::Handle kEnd = 0x0005;

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x02, 0x00,  // start handle: 0x0002
      0x05, 0x00,  // end handle: 0x0005
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );

  att::Status status;
  auto res_cb = [this, &status](att::Status val) { status = val; };

  std::vector<CharacteristicData> chrcs;
  auto chrc_cb = [&chrcs](const CharacteristicData& chrc) {
    chrcs.push_back(chrc);
  };

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverCharacteristics(kStart, kEnd, chrc_cb, res_cb);
  });

  ASSERT_TRUE(Expect(kExpectedRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x09,        // opcode: read by type response
      0x07,        // data length: 7 (16-bit UUIDs)
      0x06, 0x00,  // chrc 1 handle (handle is beyond the range)
      0x00,        // chrc 1 properties
      0x07, 0x00,  // chrc 1 value handle
      0xAD, 0xDE   // chrc 1 uuid: 0xDEAD
      ));

  RunUntilIdle();

  EXPECT_EQ(HostError::kPacketMalformed, status.error());
  EXPECT_TRUE(chrcs.empty());
}

// Expects the the characteristic value handle to immediately follow the
// declaration as specified in Vol 3, Part G, 3.3.
TEST_F(GATT_ClientTest, CharacteristicDiscoveryValueNotContiguous) {
  constexpr att::Handle kStart = 0x0002;
  constexpr att::Handle kEnd = 0x0005;

  const auto kExpectedRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type request
      0x02, 0x00,  // start handle: 0x0002
      0x05, 0x00,  // end handle: 0x0005
      0x03, 0x28   // type: characteristic decl. (0x2803)
  );

  att::Status status;
  auto res_cb = [this, &status](att::Status val) { status = val; };

  std::vector<CharacteristicData> chrcs;
  auto chrc_cb = [&chrcs](const CharacteristicData& chrc) {
    chrcs.push_back(chrc);
  };

  // Initiate the request on the message loop since Expect() below blocks.
  async::PostTask(dispatcher(), [&, this] {
    client()->DiscoverCharacteristics(kStart, kEnd, chrc_cb, res_cb);
  });

  ASSERT_TRUE(Expect(kExpectedRequest));

  fake_chan()->Receive(common::CreateStaticByteBuffer(
      0x09,        // opcode: read by type response
      0x07,        // data length: 7 (16-bit UUIDs)
      0x02, 0x00,  // chrc 1 handle
      0x00,        // chrc 1 properties
      0x04, 0x00,  // chrc 1 value handle (not immediate)
      0xAD, 0xDE   // chrc 1 uuid: 0xDEAD
      ));

  RunUntilIdle();

  EXPECT_EQ(HostError::kPacketMalformed, status.error());
  EXPECT_TRUE(chrcs.empty());
}

}  // namespace
}  // namespace gatt
}  // namespace btlib
