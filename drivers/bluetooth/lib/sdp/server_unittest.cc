// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/sdp/server.h"

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel_test.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap_defs.h"
#include "garnet/drivers/bluetooth/lib/sdp/pdu.h"
#include "garnet/drivers/bluetooth/lib/sdp/status.h"

namespace btlib {
namespace sdp {
namespace {

using common::LowerBits;
using common::UpperBits;

class SDP_ServerTest : public l2cap::testing::FakeChannelTest {
 public:
  SDP_ServerTest() = default;
  ~SDP_ServerTest() = default;

 protected:
  void SetUp() override { server_ = std::make_unique<Server>(); }

  void TearDown() override { server_ = nullptr; }

  Server* server() const { return server_.get(); }

  ServiceHandle AddSPP() {
    ServiceHandle handle;
    bool s = server()->RegisterService([&handle](auto* record) {
      handle = record->handle();
      record->SetServiceClassUUIDs({profile::kSerialPort});
      record->AddProtocolDescriptor(ServiceRecord::kPrimaryProtocolList,
                                    protocol::kL2CAP, DataElement());
      record->AddProtocolDescriptor(ServiceRecord::kPrimaryProtocolList,
                                    protocol::kRFCOMM, DataElement(uint8_t(0)));
      record->AddProfile(profile::kSerialPort, 1, 2);
      record->AddInfo("en", "FAKE", "", "");
    });
    EXPECT_TRUE(s);
    return handle;
  }

  ServiceHandle AddA2DPSink() {
    ServiceHandle handle;
    bool s = server()->RegisterService([&handle](auto* record) {
      handle = record->handle();
      record->SetServiceClassUUIDs({profile::kAudioSink});
      record->AddProtocolDescriptor(ServiceRecord::kPrimaryProtocolList,
                                    protocol::kL2CAP,
                                    DataElement(l2cap::kAVDTP));
      record->AddProtocolDescriptor(ServiceRecord::kPrimaryProtocolList,
                                    protocol::kAVDTP,
                                    DataElement(uint16_t(0x0103)));  // Version
      record->AddProfile(profile::kAdvancedAudioDistribution, 1, 3);
      record->SetAttribute(kA2DP_SupportedFeatures,
                           DataElement(uint16_t(0x0001)));  // Headphones
    });
    EXPECT_TRUE(s);
    return handle;
  }

 private:
  std::unique_ptr<Server> server_;
};

constexpr l2cap::ChannelId kSdpChannel = 0x0041;

#define SDP_ERROR_RSP(t_id, code)                                              \
  common::CreateStaticByteBuffer(0x01, UpperBits(t_id), LowerBits(t_id), 0x00, \
                                 0x02, UpperBits(uint16_t(code)),              \
                                 LowerBits(uint16_t(code)));

// Test:
//  - Accepts channels and holds channel open correctly.
//  - Packets that are the wrong length are responded to with kInvalidSize
//  - Answers with the same TransactionID as sent
TEST_F(SDP_ServerTest, BasicError) {
  {
    auto fake_chan = CreateFakeChannel(ChannelOptions(kSdpChannel));
    EXPECT_TRUE(server()->AddConnection(std::string("one"), fake_chan));
  }

  EXPECT_TRUE(fake_chan()->activated());

  const auto kRspErrSize = SDP_ERROR_RSP(0x1001, ErrorCode::kInvalidSize);

  const auto kTooSmall =
      common::CreateStaticByteBuffer(0x01,        // SDP_ServiceSearchRequest
                                     0x10, 0x01,  // Transaction ID (0x1001)
                                     0x00, 0x09   // Parameter length (9 bytes)
      );

  const auto kRspTooSmall = SDP_ERROR_RSP(0x1001, ErrorCode::kInvalidSize);

  const auto kTooBig =
      common::CreateStaticByteBuffer(0x01,        // SDP_ServiceSearchRequest
                                     0x20, 0x10,  // Transaction ID (0x2010)
                                     0x00, 0x02,  // Parameter length (2 bytes)
                                     0x01, 0x02, 0x03  // 3 bytes of parameters
      );

  const auto kRspTooBig = SDP_ERROR_RSP(0x2010, ErrorCode::kInvalidSize);

  EXPECT_TRUE(ReceiveAndExpect(kTooSmall, kRspTooSmall));
  EXPECT_TRUE(ReceiveAndExpect(kTooBig, kRspTooBig));

  const auto kRspInvalidSyntax =
      SDP_ERROR_RSP(0x2010, ErrorCode::kInvalidRequestSyntax);

  // Responses aren't valid requests
  EXPECT_TRUE(ReceiveAndExpect(kRspTooBig, kRspInvalidSyntax));
}

// Test:
//  - Passes an initialized ServiceRecord that has a matching SericeHandle
//  - Doesn't add a service that doesn't contain a ServiceClassIDList
//  - Adds a service that is valid.
//  - Services can be Unregistered.
TEST_F(SDP_ServerTest, RegisterService) {
  EXPECT_FALSE(server()->RegisterService([](auto*) {}));
  EXPECT_FALSE(server()->RegisterService([](auto* record) {
    record->SetAttribute(kServiceClassIdList, DataElement(uint16_t(42)));
  }));

  EXPECT_FALSE(server()->RegisterService([](auto* record) {
    // kSDPHandle is invalid anyway, but we can't change it.
    record->SetAttribute(kServiceRecordHandle, DataElement(0));
  }));

  EXPECT_FALSE(server()->RegisterService(
      [](auto* record) { record->RemoveAttribute(kServiceRecordHandle); }));

  ServiceHandle handle;

  bool added = server()->RegisterService([&handle](ServiceRecord* record) {
    EXPECT_TRUE(record);
    EXPECT_TRUE(record->HasAttribute(kServiceRecordHandle));
    handle = record->handle();
    record->SetServiceClassUUIDs({profile::kAVRemoteControl});
  });

  EXPECT_TRUE(added);

  EXPECT_TRUE(server()->UnregisterService(handle));
  EXPECT_FALSE(server()->UnregisterService(handle));
}

#define UINT32_AS_BE_BYTES(x)                                    \
  UpperBits(x >> 16), LowerBits(x >> 16), UpperBits(x & 0xFFFF), \
      LowerBits(x & 0xFFFF)

// Test ServiceSearchRequest:
//  - returns services with the UUID included
//  - doesn't return services that don't have the UUID
//  - fails when there are no items or too many items in the search
//  - doesn't return more than the max requested
TEST_F(SDP_ServerTest, ServiceSearchRequest) {
  {
    auto fake_chan = CreateFakeChannel(ChannelOptions(kSdpChannel));
    EXPECT_TRUE(server()->AddConnection(std::string("one"), fake_chan));
  }

  ServiceHandle spp_handle = AddSPP();
  ServiceHandle a2dp_handle = AddA2DPSink();
  const auto kL2capSearch = common::CreateStaticByteBuffer(
      0x02,        // SDP_ServiceSearchRequest
      0x10, 0x01,  // Transaction ID (0x1001)
      0x00, 0x08,  // Parameter length (8 bytes)
      // ServiceSearchPattern
      0x35, 0x03,        // Data Element Sequence w/1 byte length (3 bytes)
      0x19, 0x01, 0x00,  // UUID: Protocol: L2CAP
      0xFF, 0xFF,        // MaximumServiceRecordCount: (none)
      0x00               // Contunuation State: none
  );

  ServiceSearchRequest search_req;
  EXPECT_FALSE(search_req.valid());
  EXPECT_EQ(nullptr, search_req.GetPDU(0x1001));

  search_req.set_search_pattern({protocol::kL2CAP});

  auto pdu = search_req.GetPDU(0x1001);
  EXPECT_NE(nullptr, pdu);

  EXPECT_TRUE(ContainersEqual(kL2capSearch, *pdu));

  const auto kL2capSearchResponse = common::CreateStaticByteBuffer(
      0x03,                            // SDP_ServicesearchResponse
      0x10, 0x01,                      // Transaction ID (0x1001)
      0x00, 0x0D,                      // Parameter length (13 bytes)
      0x00, 0x02,                      // Total service record count: 2
      0x00, 0x02,                      // Current service record count: 2
      UINT32_AS_BE_BYTES(spp_handle),  // This list isn't specifically ordered
      UINT32_AS_BE_BYTES(a2dp_handle),
      0x00  // No continuation state
  );

  bool recv = false;
  std::vector<ServiceHandle> handles;
  TransactionId tid;
  auto cb = [&recv, &handles, &tid](auto cb_packet) {
    EXPECT_LE(sizeof(Header), cb_packet->size());
    common::PacketView<Header> packet(cb_packet.get());
    EXPECT_EQ(kServiceSearchResponse, packet.header().pdu_id);
    tid = betoh16(packet.header().tid);
    uint16_t len = betoh16(packet.header().param_length);
    bt_log(SPEW, "unittest", "resize packet to %d", len);
    packet.Resize(len);
    ServiceSearchResponse resp;
    auto status = resp.Parse(packet.payload_data());
    EXPECT_TRUE(status);
    handles = resp.service_record_handle_list();
    recv = true;
  };

  fake_chan()->SetSendCallback(cb, dispatcher());
  fake_chan()->Receive(kL2capSearch);
  RunLoopUntilIdle();

  EXPECT_TRUE(recv);
  EXPECT_EQ(0x1001, tid);
  EXPECT_EQ(2u, handles.size());
  EXPECT_NE(handles.end(),
            std::find(handles.begin(), handles.end(), spp_handle));
  EXPECT_NE(handles.end(),
            std::find(handles.begin(), handles.end(), a2dp_handle));

  const auto kInvalidNoItems = common::CreateStaticByteBuffer(
      0x02,        // SDP_ServiceSearchRequest
      0x10, 0xA1,  // Transaction ID (0x10A1)
      0x00, 0x05,  // Parameter length (5 bytes)
      // ServiceSearchPattern
      0x35, 0x00,  // Data Element Sequence w/1 byte length (no bytes)
      0xFF, 0xFF,  // MaximumServiceRecordCount: (none)
      0x00         // Contunuation State: none
  );

  const auto kRspErrSyntax =
      SDP_ERROR_RSP(0x10A1, ErrorCode::kInvalidRequestSyntax);

  EXPECT_TRUE(ReceiveAndExpect(kInvalidNoItems, kRspErrSyntax));

  const auto kInvalidTooManyItems = common::CreateStaticByteBuffer(
      0x02,        // SDP_ServiceSearchRequest
      0x10, 0xA1,  // Transaction ID (0x10B1)
      0x00, 0x2C,  // Parameter length (44 bytes)
      // ServiceSearchPattern
      0x35, 0x27,        // Data Element Sequence w/1 byte length (27 bytes)
      0x19, 0x30, 0x01,  // 13 UUIDs in the search
      0x19, 0x30, 0x02, 0x19, 0x30, 0x03, 0x19, 0x30, 0x04, 0x19, 0x30, 0x05,
      0x19, 0x30, 0x06, 0x19, 0x30, 0x07, 0x19, 0x30, 0x08, 0x19, 0x30, 0x09,
      0x19, 0x30, 0x10, 0x19, 0x30, 0x11, 0x19, 0x30, 0x12, 0x19, 0x30, 0x13,
      0xFF, 0xFF,  // MaximumServiceRecordCount: (none)
      0x00         // Contunuation State: none
  );

  EXPECT_TRUE(ReceiveAndExpect(kInvalidTooManyItems, kRspErrSyntax));
}

// Test ServiceSearchRequest:
//  - doesn't return more than the max requested
TEST_F(SDP_ServerTest, ServiceSearchRequestOneOfMany) {
  {
    auto fake_chan = CreateFakeChannel(ChannelOptions(kSdpChannel));
    EXPECT_TRUE(server()->AddConnection(std::string("one"), fake_chan));
  }

  ServiceHandle spp_handle = AddSPP();
  ServiceHandle a2dp_handle = AddA2DPSink();

  bool recv = false;
  std::vector<ServiceHandle> handles;
  TransactionId tid;
  auto cb = [&recv, &handles, &tid](auto cb_packet) {
    EXPECT_LE(sizeof(Header), cb_packet->size());
    common::PacketView<Header> packet(cb_packet.get());
    EXPECT_EQ(kServiceSearchResponse, packet.header().pdu_id);
    tid = betoh16(packet.header().tid);
    uint16_t len = betoh16(packet.header().param_length);
    bt_log(SPEW, "unittests", "resizing packet to %d", len);
    packet.Resize(len);
    ServiceSearchResponse resp;
    auto status = resp.Parse(packet.payload_data());
    EXPECT_TRUE(status);
    handles = resp.service_record_handle_list();
    recv = true;
  };

  const auto kL2capSearchOne = common::CreateStaticByteBuffer(
      0x02,        // SDP_ServiceSearchRequest
      0x10, 0xC1,  // Transaction ID (0x10C1)
      0x00, 0x08,  // Parameter length (8 bytes)
      // ServiceSearchPattern
      0x35, 0x03,        // Data Element Sequence w/1 byte length (3 bytes)
      0x19, 0x01, 0x00,  // UUID: Protocol: L2CAP
      0x00, 0x01,        // MaximumServiceRecordCount: 1
      0x00               // Contunuation State: none
  );

  handles.clear();
  recv = false;

  fake_chan()->SetSendCallback(cb, dispatcher());
  fake_chan()->Receive(kL2capSearchOne);
  RunLoopUntilIdle();

  EXPECT_TRUE(recv);
  EXPECT_EQ(0x10C1, tid);
  EXPECT_EQ(1u, handles.size());
  bool found_spp =
      std::find(handles.begin(), handles.end(), spp_handle) != handles.end();
  bool found_a2dp =
      std::find(handles.begin(), handles.end(), a2dp_handle) != handles.end();
  EXPECT_TRUE(found_spp || found_a2dp);
}

#undef SDP_ERROR_RSP
#undef UINT32_AS_LE_BYTES

}  // namespace
}  // namespace sdp
}  // namespace btlib
