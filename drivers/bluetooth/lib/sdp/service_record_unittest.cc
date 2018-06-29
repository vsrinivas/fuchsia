// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/sdp/service_record.h"
#include "garnet/drivers/bluetooth/lib/sdp/data_element.h"

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"

namespace btlib {
namespace sdp {
namespace {

using SDP_ServiceRecordTest = ::testing::Test;

// Test: making a new record generates a UUID.
// Test: GetAttribute
// Test: HasAttribute
//   - returns true if attribute is there
//   - returns false if attribute is not there
// Test: RemoveAttribute
// Test: SetServiceClassUUIDs
//  - Sets the right attribute with the right format.
TEST_F(SDP_ServiceRecordTest, BasicFunctionality) {
  ServiceRecord record(kSDPHandle);

  EXPECT_EQ(kSDPHandle, record.handle());

  EXPECT_TRUE(record.HasAttribute(kServiceId));

  EXPECT_FALSE(record.HasAttribute(kServiceClassIdList));

  // This isn't a valid service class ID list:
  //  - ServiceDiscoveryServerServiceClassID
  //  - BrowseGroupDesciptorServiceClassID
  common::UUID sdp_id(uint16_t(0x1000));
  common::UUID group_id(uint16_t(0x1001));
  std::vector<common::UUID> service_class;
  service_class.push_back(sdp_id);
  service_class.emplace_back(group_id);

  record.SetServiceClassUUIDs(service_class);

  EXPECT_TRUE(record.HasAttribute(kServiceClassIdList));

  DataElement elem = record.GetAttribute(kServiceClassIdList);

  EXPECT_EQ(DataElement::Type::kSequence, elem.type());

  common::Optional<std::vector<DataElement>> vec =
      elem.Get<std::vector<DataElement>>();

  EXPECT_TRUE(vec);
  EXPECT_EQ(2u, vec->size());
  EXPECT_EQ(sdp_id, *(vec->at(0).Get<common::UUID>()));
  EXPECT_EQ(group_id, *(vec->at(1).Get<common::UUID>()));

  record.RemoveAttribute(kServiceId);

  EXPECT_FALSE(record.HasAttribute(kServiceId));
}

// Test: GetAttributes
//  - Returns the correct format
//  - Returns any attributes that are present in the correct order.
TEST_F(SDP_ServiceRecordTest, GetAttributes) {
  ServiceRecord record(kSDPHandle);

  record.SetAttribute(0xf00d, DataElement());
  record.SetAttribute(0x0001, DataElement());
  record.SetAttribute(0xfeed, DataElement());

  std::unordered_set<AttributeId> ids;
  ids.insert(0xfeed);
  ids.insert(kServiceRecordHandle);
  ids.insert(0xb000);
  ids.insert(0xf00d);

  DataElement attr = record.GetAttributes(ids);

  EXPECT_EQ(DataElement::Type::kSequence, attr.type());

  // clang-format off
  auto expected = common::CreateStaticByteBuffer(
      0x35, 0x10, // Data Element Sequence with 1 byte length (16 bytes)
      0x09, // uint16_t type (Attribute ID)
      0x00, 0x00, // kServiceRecordHandle
      0x0A, // uint32_t type (Service Record)
      0x00, 0x00, 0x00, 0x00, // kSDPHandle
      0x09, // uint16_t type (Attribute ID)
      0xf0, 0x0d, // 0xf00d
      0x00, // nil type (no data bytes)
      0x09, // uint16_t type (Attribute ID)
      0xfe, 0xed, // 0xfeed
      0x00 // nil type (no data bytes)
  );
  // clang-format on

  common::DynamicByteBuffer block(18);

  size_t written = attr.Write(&block);

  EXPECT_EQ(expected.size(), written);
  EXPECT_EQ(written, attr.WriteSize());
  EXPECT_TRUE(ContainersEqual(expected, block));
}

// Test: FindUUID
//  - Only returns true if all uuids are present
TEST_F(SDP_ServiceRecordTest, FindUUID) {
  ServiceRecord record(kSDPHandle);

  DataElement elem;
  elem.Set(common::UUID(uint16_t(0xfeaa)));
  record.SetAttribute(0xb001, elem);
  elem.Set(common::UUID(uint16_t(0xfeed)));
  record.SetAttribute(0xb002, elem);
  elem.Set(common::UUID(uint16_t(0xfeec)));
  record.SetAttribute(0xb003, elem);

  std::unordered_set<common::UUID> search_pattern;
  search_pattern.insert(common::UUID(uint16_t(0xfeaa)));

  EXPECT_TRUE(record.FindUUID(search_pattern));

  search_pattern.insert(common::UUID(uint16_t(0xfeec)));

  EXPECT_TRUE(record.FindUUID(search_pattern));

  search_pattern.insert(common::UUID(uint16_t(0xfeeb)));

  EXPECT_FALSE(record.FindUUID(search_pattern));
}

// Test: AddProtocolDescriptor
TEST_F(SDP_ServiceRecordTest, AddProtocolDescriptor) {
  ServiceRecord record(kSDPHandle);

  EXPECT_FALSE(record.HasAttribute(kProtocolDescriptorList));

  DataElement psm;
  psm.Set(uint16_t(0x0001));  // SDP PSM

  record.AddProtocolDescriptor(ServiceRecord::kPrimaryProtocolList,
                               protocol::kL2CAP, psm);

  // clang-format off
  auto expected = common::CreateStaticByteBuffer(
      0x35, 0x08, // Data Element Sequence (8 bytes)
      0x35, 0x06, // Data Element Sequence (6 bytes)
      0x19, // UUID (16 bits)
      0x01, 0x00, // L2CAP protocol UUID
      0x09, // uint16_t
      0x00, 0x01  // PSM=SDP
  );
  // clang-format on

  EXPECT_TRUE(record.HasAttribute(kProtocolDescriptorList));

  DataElement val = record.GetAttribute(kProtocolDescriptorList);
  common::DynamicByteBuffer block(val.WriteSize());
  val.Write(&block);

  EXPECT_EQ(expected.size(), block.size());
  EXPECT_TRUE(ContainersEqual(expected, block));

  record.AddProtocolDescriptor(ServiceRecord::kPrimaryProtocolList,
                               protocol::kSDP, DataElement());

  EXPECT_TRUE(record.HasAttribute(kProtocolDescriptorList));

  // clang-format off
  auto expected_sdp = common::CreateStaticByteBuffer(
      0x35, 0x0D, // Data Element Sequence (13 bytes)
      0x35, 0x06, // Data Element Sequence (6 bytes)
      0x19, // UUID (16 bits)
      0x01, 0x00, // L2CAP protocol UUID
      0x09, // uint16_t
      0x00, 0x01,  // PSM=SDP
      0x35, 0x03, // Data Element Sequence (3 bytes)
      0x19, // UUID (16 bits)
      0x00, 0x01 // kSDP protocol
  );
  // clang-format on

  val = record.GetAttribute(kProtocolDescriptorList);
  common::DynamicByteBuffer block_sdp(val.WriteSize());
  val.Write(&block_sdp);

  EXPECT_EQ(expected_sdp.size(), block_sdp.size());
  EXPECT_TRUE(ContainersEqual(expected_sdp, block_sdp));

  record.AddProtocolDescriptor(1, protocol::kRFCOMM, DataElement());

  EXPECT_TRUE(record.HasAttribute(kAdditionalProtocolDescriptorList));

  // clang-format off
  auto expected_addl = common::CreateStaticByteBuffer(
      0x35, 0x07, // Data Element Sequence (AdditionalProtocolDescriptorLists)
      0x35, 0x05, // Data Element Sequence (ProtocolDescriptorList 1)
      0x35, 0x03, // Data Element Sequence Protocol List 1 Descriptor 0
      0x19, // UUID (16 bits)
      0x00, 0x03 // kRFCOMM protocol
  );
  // clang-format on

  val = record.GetAttribute(kAdditionalProtocolDescriptorList);
  common::DynamicByteBuffer block_addl(val.WriteSize());
  val.Write(&block_addl);

  EXPECT_EQ(expected_addl.size(), block_addl.size());
  EXPECT_TRUE(ContainersEqual(expected_addl, block_addl));
}

// Test: AddProfile
//  - Adds an attribute if it doesn't exist
//  - Appends to the attribute if it does exist
TEST_F(SDP_ServiceRecordTest, AddProfile) {
  ServiceRecord record(kSDPHandle);

  EXPECT_FALSE(record.HasAttribute(kBluetoothProfileDescriptorList));

  record.AddProfile(profile::kSerialPort, 2, 3);

  EXPECT_TRUE(record.HasAttribute(kBluetoothProfileDescriptorList));

  // clang-format off
  auto expected = common::CreateStaticByteBuffer(
      0x35, 0x08, // Data Element Sequence (8 bytes)
      0x35, 0x06, // Data Element Sequence (6 bytes)
      0x19, // UUID (16 bits)
      0x11, 0x01, // SerialPort protocol UUID
      0x09, // uint16_t
      0x02, 0x03  // 16 bit profile version number (major=2, minor=3)
  );
  // clang-format on

  DataElement val = record.GetAttribute(kBluetoothProfileDescriptorList);
  common::DynamicByteBuffer block(val.WriteSize());
  val.Write(&block);

  EXPECT_EQ(expected.size(), block.size());
  EXPECT_TRUE(ContainersEqual(expected, block));

  record.AddProfile(profile::kDialupNetworking, 4, 5);

  // clang-format off
  auto expected_dun = common::CreateStaticByteBuffer(
      0x35, 0x10, // Data Element Sequence (16 bytes)
      0x35, 0x06, // Data Element Sequence (6 bytes)
      0x19, // UUID (16 bits)
      0x11, 0x01, // SerialPort protocol UUID
      0x09, // uint16_t
      0x02, 0x03, // 16 bit profile version number (major=2, minor=3)
      0x35, 0x06, // Data Element Sequence (6 bytes)
      0x19, // UUID (16 bits)
      0x11, 0x03, // DUN UUID
      0x09, // uint16_t
      0x04, 0x05 // 16 bit profile version number (major=4, minor=5)
  );
  // clang-format on

  DataElement val_dun = record.GetAttribute(kBluetoothProfileDescriptorList);
  common::DynamicByteBuffer block_dun(val_dun.WriteSize());
  val_dun.Write(&block_dun);

  EXPECT_EQ(expected_dun.size(), block_dun.size());
  EXPECT_TRUE(ContainersEqual(expected_dun, block_dun));
}

// Test: AddInfo
//  - Requires at least one is set.
//  - Adds the right attributes to a set.
TEST_F(SDP_ServiceRecordTest, AddInfo) {
  ServiceRecord record(kSDPHandle);

  EXPECT_FALSE(record.HasAttribute(kLanguageBaseAttributeIdList));

  // Can't add with nothing specified.
  EXPECT_FALSE(record.AddInfo("en", "", "", ""));
  EXPECT_FALSE(record.HasAttribute(kLanguageBaseAttributeIdList));

  EXPECT_TRUE(record.AddInfo("en", "SDP", "💖", ""));

  EXPECT_TRUE(record.HasAttribute(kLanguageBaseAttributeIdList));
  DataElement val = record.GetAttribute(kLanguageBaseAttributeIdList);

  auto triplets = val.Get<std::vector<DataElement>>();
  EXPECT_TRUE(triplets);
  // They have to be triplets in this.
  EXPECT_TRUE(triplets->size() % 3 == 0);
  EXPECT_EQ(DataElement::Type::kUnsignedInt, triplets->at(0).type());
  EXPECT_EQ(DataElement::Type::kUnsignedInt, triplets->at(1).type());
  EXPECT_EQ(DataElement::Type::kUnsignedInt, triplets->at(2).type());
  auto lang = triplets->at(0).Get<uint16_t>();
  EXPECT_TRUE(lang);
  EXPECT_EQ(0x6e65, *lang);  // should be 'en' in ascii (but little-endian)

  auto encoding = triplets->at(1).Get<uint16_t>();
  EXPECT_TRUE(encoding);
  EXPECT_EQ(106, *encoding);  // should always be UTF-8

  auto base_attrid = triplets->at(2).Get<uint16_t>();
  EXPECT_TRUE(base_attrid);
  EXPECT_EQ(0x0100, *base_attrid);  // The primary language must be at 0x0100.

  EXPECT_TRUE(record.HasAttribute(*base_attrid + kServiceNameOffset));
  DataElement name_elem =
      record.GetAttribute(*base_attrid + kServiceNameOffset);
  auto name = name_elem.Get<std::string>();
  EXPECT_TRUE(name);
  EXPECT_EQ("SDP", *name);

  EXPECT_TRUE(record.HasAttribute(*base_attrid + kServiceDescriptionOffset));
  DataElement desc_elem =
      record.GetAttribute(*base_attrid + kServiceDescriptionOffset);
  auto desc = desc_elem.Get<std::string>();
  EXPECT_TRUE(desc);
  EXPECT_EQ("💖", *desc);

  EXPECT_FALSE(record.HasAttribute(*base_attrid + kProviderNameOffset));
};

}  // namespace
}  // namespace sdp
}  // namespace btlib
