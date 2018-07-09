
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/sdp/pdu.h"
#include "garnet/drivers/bluetooth/lib/sdp/sdp.h"
#include "garnet/drivers/bluetooth/lib/sdp/status.h"

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"

namespace btlib {
namespace sdp {
namespace {

using common::LowerBits;
using common::UpperBits;

using SDP_PDUTest = ::testing::Test;

TEST_F(SDP_PDUTest, ErrorResponse) {
  ErrorResponse response;
  EXPECT_FALSE(response.complete());

  auto kInvalidContState = common::CreateStaticByteBuffer(
      0x01,        // opcode: kErrorResponse
      0xDE, 0xAD,  // transaction ID: 0xDEAD
      0x00, 0x02,  // parameter length: 2 bytes
      0x00, 0x05,  // ErrorCode: Invalid Continuation State
      0xFF, 0x00   // dummy extra bytes to cause an error
  );

  Status status = response.Parse(kInvalidContState.view(sizeof(Header)));
  EXPECT_FALSE(status);
  EXPECT_EQ(common::HostError::kPacketMalformed, status.error());

  status = response.Parse(kInvalidContState.view(sizeof(Header), 2));
  EXPECT_TRUE(status);
  EXPECT_TRUE(response.complete());
  EXPECT_EQ(ErrorCode::kInvalidContinuationState, response.error_code());

  response.set_error_code(ErrorCode::kInvalidContinuationState);
  auto ptr =
      response.GetPDU(0xF00F /* ignored */, 0xDEAD, common::BufferView());

  EXPECT_TRUE(ContainersEqual(kInvalidContState.view(0, 7), *ptr));
}

TEST_F(SDP_PDUTest, ServiceSearchRequestParse) {
  const auto kL2capSearch = common::CreateStaticByteBuffer(
      // ServiceSearchPattern
      0x35, 0x03,        // Data Element Sequence w/1 byte length (3 bytes)
      0x19, 0x01, 0x00,  // UUID: Protocol: L2CAP
      0x00, 0x10,        // MaximumServiceRecordCount: 16
      0x00               // Contunuation State: none
  );

  ServiceSearchRequest req(kL2capSearch);
  EXPECT_TRUE(req.valid());
  EXPECT_EQ(1u, req.service_search_pattern().size());
  EXPECT_TRUE(req.service_search_pattern().count(protocol::kL2CAP));
  EXPECT_EQ(16, req.max_service_record_count());

  const auto kL2capSearchOne = common::CreateStaticByteBuffer(
      // ServiceSearchPattern
      0x35, 0x06,        // Data Element Sequence w/1 byte length (6 bytes)
      0x19, 0x01, 0x00,  // UUID: Protocol: L2CAP
      0x19, 0xED, 0xFE,  // UUID: 0xEDFE (unknown, doesn't need to be found)
      0x00, 0x01,        // MaximumServiceRecordCount: 1
      0x00               // Contunuation State: none
  );

  ServiceSearchRequest req_one(kL2capSearchOne);
  EXPECT_TRUE(req_one.valid());
  EXPECT_EQ(2u, req_one.service_search_pattern().size());
  EXPECT_EQ(1, req_one.max_service_record_count());

  const auto kInvalidNoItems = common::CreateStaticByteBuffer(
      // ServiceSearchPattern
      0x35, 0x00,  // Data Element Sequence w/1 byte length (no bytes)
      0xFF, 0xFF,  // MaximumServiceRecordCount: (none)
      0x00         // Contunuation State: none
  );

  ServiceSearchRequest req2(kInvalidNoItems);
  EXPECT_FALSE(req2.valid());

  const auto kInvalidTooManyItems = common::CreateStaticByteBuffer(
      // ServiceSearchPattern
      0x35, 0x27,        // Data Element Sequence w/1 byte length (27 bytes)
      0x19, 0x30, 0x01,  // 13 UUIDs in the search
      0x19, 0x30, 0x02, 0x19, 0x30, 0x03, 0x19, 0x30, 0x04, 0x19, 0x30, 0x05,
      0x19, 0x30, 0x06, 0x19, 0x30, 0x07, 0x19, 0x30, 0x08, 0x19, 0x30, 0x09,
      0x19, 0x30, 0x10, 0x19, 0x30, 0x11, 0x19, 0x30, 0x12, 0x19, 0x30, 0x13,
      0xFF, 0xFF,  // MaximumServiceRecordCount: (none)
      0x00         // Contunuation State: none
  );

  ServiceSearchRequest req3(kInvalidTooManyItems);
  EXPECT_FALSE(req3.valid());
};

TEST_F(SDP_PDUTest, ServiceSearchResponseParse) {
  const auto kValidResponse = common::CreateStaticByteBuffer(
      0x00, 0x02,              // Total service record count: 2
      0x00, 0x02,              // Current service record count: 2
      0x00, 0x00, 0x00, 0x01,  // Service Handle 1
      0x00, 0x00, 0x00, 0x02,  // Service Handle 2
      0x00                     // No continuation state
  );

  ServiceSearchResponse resp;
  auto status = resp.Parse(kValidResponse);
  EXPECT_TRUE(status);

  // Can't parse into an already complete record.
  status = resp.Parse(kValidResponse);
  EXPECT_FALSE(status);

  const auto kNotEnoughRecords = common::CreateStaticByteBuffer(
      0x00, 0x02,              // Total service record count: 2
      0x00, 0x02,              // Current service record count: 2
      0x00, 0x00, 0x00, 0x01,  // Service Handle 1
      0x00                     // No continuation state
  );
  // Doesn't contain the right # of records.
  ServiceSearchResponse resp2;
  status = resp2.Parse(kNotEnoughRecords);
  EXPECT_FALSE(status);
};

TEST_F(SDP_PDUTest, ServiceSearchResponsePDU) {
  std::vector<ServiceHandle> results{1, 2};
  ServiceSearchResponse resp;
  resp.set_service_record_handle_list(results);

  const auto kExpected = common::CreateStaticByteBuffer(
      0x03,                    // ServiceSearch Response PDU ID
      0x01, 0x10,              // Transaction ID (0x0110)
      0x00, 0x0d,              // Parameter length: 13 bytes
      0x00, 0x02,              // Total service record count: 2
      0x00, 0x02,              // Current service record count: 2
      0x00, 0x00, 0x00, 0x01,  // Service record 1
      0x00, 0x00, 0x00, 0x02,  // Service record 2
      0x00                     // No continuation state
  );

  auto pdu = resp.GetPDU(0xFFFF, 0x0110, common::BufferView());
  EXPECT_TRUE(ContainersEqual(kExpected, *pdu));

  const auto kExpectedLimited = common::CreateStaticByteBuffer(
      0x03,                    // ServiceSearchResponse PDU ID
      0x01, 0x10,              // Transaction ID (0x0110)
      0x00, 0x09,              // Parameter length: 9
      0x00, 0x01,              // Total service record count: 1
      0x00, 0x01,              // Current service record count: 1
      0x00, 0x00, 0x00, 0x01,  // Service record 1
      0x00                     // No continuation state
  );

  pdu = resp.GetPDU(1, 0x0110, common::BufferView());
  EXPECT_TRUE(ContainersEqual(kExpectedLimited, *pdu));
};

}  // namespace
}  // namespace sdp
}  // namespace btlib
