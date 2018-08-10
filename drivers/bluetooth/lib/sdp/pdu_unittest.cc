
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
      0x35, 0x03,        // Sequence uint8 3 bytes
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
      0x35, 0x06,        // Sequence uint8 6 bytes
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
      0x35, 0x00,  // Sequence uint8 0 bytes
      0xFF, 0xFF,  // MaximumServiceRecordCount: (none)
      0x00         // Contunuation State: none
  );

  ServiceSearchRequest req2(kInvalidNoItems);
  EXPECT_FALSE(req2.valid());

  const auto kInvalidTooManyItems = common::CreateStaticByteBuffer(
      // ServiceSearchPattern
      0x35, 0x27,        // Sequence uint8 27 bytes
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

TEST_F(SDP_PDUTest, ServiceAttributeRequestValidity) {
  ServiceAttributeRequest req;

  // No attributes requested, so it begins invalid
  EXPECT_FALSE(req.valid());

  // Adding an attribute makes it valid, and adds a range.
  req.AddAttribute(kServiceClassIdList);

  EXPECT_TRUE(req.valid());
  EXPECT_EQ(1u, req.attribute_ranges().size());
  EXPECT_EQ(kServiceClassIdList, req.attribute_ranges().front().start);

  // Adding an attribute adjacent just adds to the range (on either end)
  req.AddAttribute(kServiceClassIdList - 1);  // kServiceRecordHandle
  req.AddAttribute(kServiceClassIdList + 1);
  req.AddAttribute(kServiceClassIdList + 2);

  EXPECT_TRUE(req.valid());
  EXPECT_EQ(1u, req.attribute_ranges().size());
  EXPECT_EQ(kServiceClassIdList - 1, req.attribute_ranges().front().start);
  EXPECT_EQ(kServiceClassIdList + 2, req.attribute_ranges().front().end);

  // Adding a disjoint range makes it two ranges, and they're in the right
  // order.
  req.AddAttribute(kServiceClassIdList + 4);

  EXPECT_TRUE(req.valid());
  EXPECT_EQ(2u, req.attribute_ranges().size());
  EXPECT_EQ(kServiceClassIdList - 1, req.attribute_ranges().front().start);
  EXPECT_EQ(kServiceClassIdList + 2, req.attribute_ranges().front().end);
  EXPECT_EQ(kServiceClassIdList + 4, req.attribute_ranges().back().start);
  EXPECT_EQ(kServiceClassIdList + 4, req.attribute_ranges().back().end);

  // Adding one that makes it contiguous collapses them to a single range.
  req.AddAttribute(kServiceClassIdList + 3);

  EXPECT_EQ(1u, req.attribute_ranges().size());
  EXPECT_EQ(kServiceClassIdList - 1, req.attribute_ranges().front().start);
  EXPECT_EQ(kServiceClassIdList + 4, req.attribute_ranges().front().end);

  // Attribute byte count has a minimum size.
  req.set_max_attribute_byte_count(6);

  EXPECT_FALSE(req.valid());
}

TEST_F(SDP_PDUTest, ServiceAttriuteRequestAddRange) {
  ServiceAttributeRequest req;

  req.AddAttributeRange(0x0010, 0xFFF0);

  EXPECT_EQ(1u, req.attribute_ranges().size());
  EXPECT_EQ(0x0010, req.attribute_ranges().front().start);
  EXPECT_EQ(0xFFF0, req.attribute_ranges().front().end);

  req.AddAttributeRange(0x0100, 0xFF00);

  EXPECT_EQ(1u, req.attribute_ranges().size());
  EXPECT_EQ(0x0010, req.attribute_ranges().front().start);
  EXPECT_EQ(0xFFF0, req.attribute_ranges().front().end);

  req.AddAttributeRange(0x0000, 0x0002);

  EXPECT_EQ(2u, req.attribute_ranges().size());
  EXPECT_EQ(0x0000, req.attribute_ranges().front().start);
  EXPECT_EQ(0x0002, req.attribute_ranges().front().end);

  req.AddAttributeRange(0x0003, 0x000F);

  EXPECT_EQ(1u, req.attribute_ranges().size());
  EXPECT_EQ(0x0000, req.attribute_ranges().front().start);
  EXPECT_EQ(0xFFF0, req.attribute_ranges().front().end);

  req.AddAttributeRange(0xFFF2, 0xFFF3);
  req.AddAttributeRange(0xFFF5, 0xFFF6);
  req.AddAttributeRange(0xFFF8, 0xFFF9);
  req.AddAttributeRange(0xFFFB, 0xFFFC);

  EXPECT_EQ(5u, req.attribute_ranges().size());

  // merges 0x0000-0xFFF0 with 0xFFF2-0xFFF3 and new range leaving
  // 0x0000-0xFFF3, 0xFFF5-0xFFF6, 0xFFF8-0xFFF9 and 0xFFFB-0xFFFC
  req.AddAttributeRange(0xFFF1, 0xFFF1);

  EXPECT_EQ(4u, req.attribute_ranges().size());

  // merges everything except 0xFFFB-0xFFFC
  req.AddAttributeRange(0xFFF1, 0xFFF9);

  EXPECT_EQ(2u, req.attribute_ranges().size());

  req.AddAttributeRange(0xFFFA, 0xFFFF);

  EXPECT_EQ(1u, req.attribute_ranges().size());
  EXPECT_EQ(0x0000, req.attribute_ranges().front().start);
  EXPECT_EQ(0xFFFF, req.attribute_ranges().front().end);
}

TEST_F(SDP_PDUTest, ServiceAttributeRequestParse) {
  const auto kSDPAllAttributes = common::CreateStaticByteBuffer(
      0x00, 0x00, 0x00, 0x00,  // ServiceRecordHandle: 0
      0xF0, 0x0F,              // Maximum attribute byte count: (max = 61455)
      // Attribute ID List
      0x35, 0x05,              // Sequence uint8 5 bytes
      0x0A,                    // uint32_t
      0x00, 0x00, 0xFF, 0xFF,  // Attribute range: 0x000 - 0xFFFF (All of them)
      0x00                     // No continuation state
  );

  ServiceAttributeRequest req(kSDPAllAttributes);

  EXPECT_TRUE(req.valid());
  EXPECT_EQ(1u, req.attribute_ranges().size());
  EXPECT_EQ(0x0000, req.attribute_ranges().front().start);
  EXPECT_EQ(0xFFFF, req.attribute_ranges().front().end);
  EXPECT_EQ(61455, req.max_attribute_byte_count());

  const auto kContinued = common::CreateStaticByteBuffer(
      0x00, 0x00, 0x00, 0x00,  // ServiceRecordHandle: 0
      0x00, 0x0F,              // Maximum attribute byte count: (max = 15)
      // Attribute ID List
      0x35, 0x06,             // Sequence uint8 3 bytes
      0x09,                   // uint16_t
      0x00, 0x01,             // Attribute ID: 1
      0x09,                   // uint16_t
      0x00, 0x02,             // Attribute ID: 2
      0x03, 0x12, 0x34, 0x56  // Continuation state
  );

  ServiceAttributeRequest req_cont_state(kContinued);

  EXPECT_TRUE(req_cont_state.valid());
  EXPECT_EQ(2u, req_cont_state.attribute_ranges().size());
  EXPECT_EQ(0x0001, req_cont_state.attribute_ranges().front().start);
  EXPECT_EQ(0x0001, req_cont_state.attribute_ranges().front().end);
  EXPECT_EQ(0x0002, req_cont_state.attribute_ranges().back().start);
  EXPECT_EQ(0x0002, req_cont_state.attribute_ranges().back().end);
  EXPECT_EQ(15, req_cont_state.max_attribute_byte_count());

  // Too short request.
  ServiceAttributeRequest req_short((common::BufferView()));

  EXPECT_FALSE(req_short.valid());

  const auto kInvalidMaxBytes = common::CreateStaticByteBuffer(
      0x00, 0x00, 0x00, 0x00,  // ServiceRecordHandle: 0
      0x00, 0x04,              // Maximum attribute byte count (4)
      // Attribute ID List
      0x35, 0x03,        // Sequence uint8 3 bytes
      0x09, 0x00, 0x02,  // uint16_t (2)
      0x00               // No continuation state
  );

  ServiceAttributeRequest req_minmax(kInvalidMaxBytes);

  EXPECT_FALSE(req_minmax.valid());

  // Invalid order of the attributes.
  const auto kInvalidAttributeListOrder = common::CreateStaticByteBuffer(
      0x00, 0x00, 0x00, 0x00,  // ServiceRecordHandle: 0
      0xFF, 0xFF,              // Maximum attribute byte count: (max = 65535)
      // Attribute ID List
      0x35, 0x06,        // Sequence uint8 5 bytes
      0x09, 0x00, 0x02,  // uint16_t (2)
      0x09, 0x00, 0x01,  // uint16_t (1)
      0x00               // No continuation state
  );

  ServiceAttributeRequest req_order(kInvalidAttributeListOrder);

  EXPECT_FALSE(req_short.valid());

  // AttributeID List has invalid items
  const auto kInvalidAttributeList = common::CreateStaticByteBuffer(
      0x00, 0x00, 0x00, 0x00,  // ServiceRecordHandle: 0
      0xFF, 0xFF,              // Maximum attribute byte count: (max = 65535)
      // Attribute ID List
      0x35, 0x06,        // Sequence uint8 5 bytes
      0x09, 0x00, 0x02,  // uint16_t (2)
      0x19, 0x00, 0x01,  // UUID (0x0001)
      0x00               // No continuation state
  );

  ServiceAttributeRequest req_baditems(kInvalidAttributeList);

  EXPECT_FALSE(req_baditems.valid());
}

TEST_F(SDP_PDUTest, ServiceAttributeResponseParse) {
  const auto kValidResponseEmpty = common::CreateStaticByteBuffer(
      0x00, 0x02,  // AttributeListByteCount (2 bytes)
      // Attribute List
      0x35, 0x00,  // Sequence uint8 0 bytes
      0x00         // No continuation state
  );

  ServiceAttributeResponse resp;
  auto status = resp.Parse(kValidResponseEmpty);

  EXPECT_TRUE(status);

  const auto kValidResponseItems = common::CreateStaticByteBuffer(
      0x00, 0x12,  // AttributeListByteCount (18 bytes)
      // Attribute List
      0x35, 0x10,        // Sequence uint8 16 bytes
      0x09, 0x00, 0x00,  // Handle: uint16_t (0 = kServiceRecordHandle)
      0x0A, 0xFE, 0xED, 0xBE, 0xEF,  // Element: uint32_t (0xFEEDBEEF)
      0x09, 0x00, 0x01,  // Handle: uint16_t (1 = kServiceClassIdList)
      0x35, 0x03, 0x19, 0x11, 0x01,  // Element: Sequence (3) { UUID(0x1101) }
      0x00                           // No continuation state
  );

  status = resp.Parse(kValidResponseItems);

  EXPECT_TRUE(status);
  EXPECT_EQ(2u, resp.attributes().size());
  auto it = resp.attributes().find(0x00);
  EXPECT_NE(resp.attributes().end(), it);
  EXPECT_EQ(DataElement::Type::kUnsignedInt, it->second.type());

  it = resp.attributes().find(0x01);
  EXPECT_NE(resp.attributes().end(), it);
  EXPECT_EQ(DataElement::Type::kSequence, it->second.type());

  const auto kInvalidItemsWrongOrder = common::CreateStaticByteBuffer(
      0x00, 0x12,  // AttributeListByteCount (18 bytes)
      // Attribute List
      0x35, 0x10,        // Sequence uint8 16 bytes
      0x09, 0x00, 0x01,  // Handle: uint16_t (1 = kServiceClassIdList)
      0x35, 0x03, 0x19, 0x11, 0x01,  // Element: Sequence (3) { UUID(0x1101) }
      0x09, 0x00, 0x00,  // Handle: uint16_t (0 = kServiceRecordHandle)
      0x0A, 0xFE, 0xED, 0xBE, 0xEF,  // Element: uint32_t (0xFEEDBEEF)
      0x00                           // No continuation state
  );

  status = resp.Parse(kInvalidItemsWrongOrder);

  EXPECT_FALSE(status);

  const auto kInvalidByteCount = common::CreateStaticByteBuffer(
      0x00, 0x12,  // AttributeListByteCount (18 bytes)
      // Attribute List
      0x35, 0x0F,                    // Sequence uint8 15 bytes
      0x09, 0x00, 0x01,              // Handle: uint16_t (1)
      0x35, 0x03, 0x19, 0x11, 0x01,  // Element: Sequence (3) { UUID(0x1101) }
      0x09, 0x00, 0x00,              // Handle: uint16_t (0)
      0x25, 0x02, 'h', 'i',          // Element: String ('hi')
      0x00                           // No continuation state
  );

  status = resp.Parse(kInvalidByteCount);

  EXPECT_FALSE(status);
}

}  // namespace
}  // namespace sdp
}  // namespace btlib
