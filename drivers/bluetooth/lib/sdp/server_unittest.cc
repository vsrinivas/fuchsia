// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/sdp/server.h"

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel_test.h"

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
    record->SetAttribute(kServiceRecordHandle, 0);
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

}  // namespace
}  // namespace sdp
}  // namespace btlib
