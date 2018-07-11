// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/sdp/data_element.h"
#include "garnet/drivers/bluetooth/lib/sdp/sdp.h"

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"

namespace btlib {
namespace sdp {
namespace {

using common::LowerBits;
using common::UpperBits;

using SDP_DataElementTest = ::testing::Test;

TEST_F(SDP_DataElementTest, CreateIsNull) {
  DataElement elem;
  EXPECT_EQ(DataElement::Type::kNull, elem.type());
  EXPECT_TRUE(elem.Get<std::nullptr_t>());
  EXPECT_EQ(nullptr, *elem.Get<std::nullptr_t>());

  auto expected = common::CreateStaticByteBuffer(0x00);
  common::DynamicByteBuffer buf(1);
  EXPECT_EQ(1u, elem.Write(&buf));
  EXPECT_TRUE(ContainersEqual(expected, buf));
}

TEST_F(SDP_DataElementTest, SetAndGet) {
  DataElement elem;

  elem.Set(uint8_t(5));

  EXPECT_TRUE(elem.Get<uint8_t>());
  EXPECT_EQ(5u, *elem.Get<uint8_t>());
  EXPECT_FALSE(elem.Get<int16_t>());

  elem.Set(std::string("Fuchsia💖"));

  EXPECT_FALSE(elem.Get<uint8_t>());
  EXPECT_TRUE(elem.Get<std::string>());
  EXPECT_EQ(std::string("Fuchsia💖"), *elem.Get<std::string>());
}

TEST_F(SDP_DataElementTest, Read) {
  auto buf = common::CreateStaticByteBuffer(
      0x25,  // Type (4: String) & Size (5: in an additional byte) = 0b00100 101
      0x0B,  // Bytes
      'F', 'u', 'c', 'h', 's', 'i', 'a', 0xF0, 0x9F, 0x92, 0x96,  // String
      0xDE, 0xAD, 0xBE, 0xEF  // Extra data (shouldn't be parsed)
  );

  DataElement elem;
  EXPECT_EQ(13u, DataElement::Read(&elem, buf));

  EXPECT_EQ(DataElement::Type::kString, elem.type());
  EXPECT_EQ(std::string("Fuchsia💖"), *elem.Get<std::string>());

  // Invalid - 0xDE: 0x11011 110 = 37 (invalid) + 6 (2 following byte size)
  EXPECT_EQ(0u, DataElement::Read(&elem, buf.view(13)));

  // elem shouldn't have been touched
  EXPECT_EQ(DataElement::Type::kString, elem.type());
  EXPECT_EQ(std::string("Fuchsia💖"), *elem.Get<std::string>());
}

TEST_F(SDP_DataElementTest, ReadUUID) {
  auto buf = common::CreateStaticByteBuffer(
      0x19,       // Type (3: UUID) & Size (1: two bytes) = 0b00011 001
      0x01, 0x00  // L2CAP
  );

  DataElement elem;
  EXPECT_EQ(3u, DataElement::Read(&elem, buf));
  EXPECT_EQ(DataElement::Type::kUuid, elem.type());
  EXPECT_EQ(common::UUID(uint16_t(0x0100)), *elem.Get<common::UUID>());

  auto buf2 = common::CreateStaticByteBuffer(
      0x1A,  // Type (3: UUID) & Size (2: four bytes) = 0b00011 010
      0x01, 0x02, 0x03, 0x04);

  EXPECT_EQ(5u, DataElement::Read(&elem, buf2));
  EXPECT_EQ(DataElement::Type::kUuid, elem.type());
  EXPECT_EQ(common::UUID(uint32_t(0x01020304)), *elem.Get<common::UUID>());

  auto buf3 = common::CreateStaticByteBuffer(
      0x1B,  // Type (3: UUID) & Size (3: eight bytes) = 0b00011 011
      0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04,
      0x01, 0x02, 0x03, 0x04);

  EXPECT_EQ(0u, DataElement::Read(&elem, buf3));

  auto buf4 = common::CreateStaticByteBuffer(
      0x1C,  // Type (3: UUID) & Size (3: eight bytes) = 0b00011 100
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
      0x0D, 0x0E, 0x0F, 0x10);

  EXPECT_EQ(17u, DataElement::Read(&elem, buf4));
  EXPECT_EQ(DataElement::Type::kUuid, elem.type());
  // UInt128 in UUID is little-endian
  EXPECT_EQ(common::UUID({0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08,
                          0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}),
            *elem.Get<common::UUID>());
}

TEST_F(SDP_DataElementTest, Write) {
  // This represents a plausible attribute_lists parameter of a
  // SDP_ServiceSearchAtttributeResponse PDU for an SPP service.
  std::vector<DataElement> attribute_list;

  DataElement service_class_id;
  service_class_id.Set(kServiceClassIdList);
  DataElement service_class_value;
  DataElement service_class_uuid_spp;
  // SerialPort from Assigned Numbers
  service_class_uuid_spp.Set(common::UUID(uint16_t(0x1101)));
  std::vector<DataElement> service_class_list;
  service_class_list.push_back(service_class_uuid_spp);
  service_class_value.Set(service_class_list);
  attribute_list.push_back(service_class_id);
  attribute_list.push_back(service_class_value);

  // Protocol Descriptor List
  DataElement protocol_list_id;
  protocol_list_id.Set(kProtocolDescriptorList);
  std::vector<DataElement> protocol_list_value;

  // ( L2CAP, PSM=RFCOMM )
  std::vector<DataElement> protocol_l2cap;
  DataElement protocol;
  protocol.Set(protocol::kL2CAP);
  DataElement psm;
  psm.Set(uint16_t(0x0003));  // RFCOMM
  protocol_l2cap.push_back(protocol);
  protocol_l2cap.push_back(psm);

  DataElement protocol_l2cap_elem;
  protocol_l2cap_elem.Set(protocol_l2cap);
  protocol_list_value.push_back(protocol_l2cap_elem);

  // ( RFCOMM, CHANNEL=1 )
  std::vector<DataElement> protocol_rfcomm;
  protocol.Set(protocol::kRFCOMM);
  DataElement chan;
  chan.Set(uint8_t(1));  // Server Channel = 1
  protocol_rfcomm.push_back(protocol);
  protocol_rfcomm.push_back(chan);

  DataElement protocol_rfcomm_elem;
  protocol_rfcomm_elem.Set(protocol_rfcomm);
  protocol_list_value.push_back(protocol_rfcomm_elem);

  attribute_list.push_back(protocol_list_id);
  DataElement protocol_list_val;
  protocol_list_val.Set(protocol_list_value);
  attribute_list.push_back(protocol_list_val);

  // Bluetooth Profile Descriptor List
  DataElement profile_descriptor_list_id;
  profile_descriptor_list_id.Set(kBluetoothProfileDescriptorList);

  std::vector<DataElement> profile_sequence_list;
  std::vector<DataElement> spp_sequence;
  DataElement profile_uuid;
  profile_uuid.Set(common::UUID(uint16_t(0x1101)));
  DataElement profile_version;
  profile_version.Set(uint16_t(0x0102));
  spp_sequence.push_back(profile_uuid);
  spp_sequence.push_back(profile_version);
  DataElement spp_sequence_elem;
  spp_sequence_elem.Set(spp_sequence);

  profile_sequence_list.push_back(spp_sequence_elem);

  DataElement profile_descriptor_list_value;
  profile_descriptor_list_value.Set(profile_sequence_list);

  attribute_list.push_back(profile_descriptor_list_id);
  attribute_list.push_back(profile_descriptor_list_value);

  DataElement attribute_lists_elem;
  attribute_lists_elem.Set(attribute_list);

  // clang-format off
  auto expected = common::CreateStaticByteBuffer(
      0x35, 0x29,  // Data Element Sequence with 1 byte length (41 bytes)
      0x09,        // uint16_t type
      UpperBits(kServiceClassIdList), LowerBits(kServiceClassIdList),
      0x35, 0x03,  // Data Element Sequence with 1 byte length (3 bytes)
      0x19,        // UUID (16 bits)
      0x11, 0x01,  // Serial Port from assigned numbers
      0x09,        // uint16_t type
      UpperBits(kProtocolDescriptorList), LowerBits(kProtocolDescriptorList),
      0x35, 0x0F,  // Data Element Sequence with 1 byte length (15 bytes)
      0x35, 0x06,  // Data Element Sequence with 1 byte length (6 bytes)
      0x19,        // Type: UUID (16 bits)
      0x01, 0x00,  // L2CAP UUID
      0x09,        // Type: uint16_t
      0x00, 0x03,  // RFCOMM PSM
      0x35, 0x05,  // Data Element Sequence with 1 byte length (5 bytes)
      0x19,        // Type: UUID (16 bits)
      0x00, 0x03,  // RFCOMM UUID
      0x08,        // Type: uint8_t
      0x01,        // RFCOMM Channel 1
      0x09,        // uint16_t type
      UpperBits(kBluetoothProfileDescriptorList),
      LowerBits(kBluetoothProfileDescriptorList),
      0x35, 0x08,  // Data Element Sequence with 1 byte length (8 bytes)
      0x35, 0x06,  // Data Element Sequence with 1 byte ength (6 bytes)
      0x19,        // Type: UUID (16 bits)
      0x11, 0x01,  // 0x1101 (SPP)
      0x09,        // Type: uint16_t
      0x01, 0x02   // v1.2
  );
  // clang-format on

  common::DynamicByteBuffer block(43);

  size_t written = attribute_lists_elem.Write(&block);

  EXPECT_EQ(expected.size(), written);
  EXPECT_EQ(written, attribute_lists_elem.WriteSize());
  EXPECT_TRUE(ContainersEqual(expected, block));
}

}  // namespace
}  // namespace sdp
}  // namespace btlib
