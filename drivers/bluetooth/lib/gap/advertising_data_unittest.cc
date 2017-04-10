// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/lib/gap/advertising_data.h"

#include "gtest/gtest.h"

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "apps/bluetooth/lib/common/test_helpers.h"

namespace bluetooth {
namespace gap {
namespace {

TEST(AdvertisingDataTest, ReaderMalformedData) {
  // TLV length exceeds the size of the payload
  auto bytes0 = common::CreateStaticByteBuffer(0x01);
  AdvertisingDataReader reader(bytes0);
  EXPECT_FALSE(reader.is_valid());
  EXPECT_FALSE(reader.HasMoreData());

  auto bytes = common::CreateStaticByteBuffer(0x05, 0x00, 0x00, 0x00, 0x00);
  reader = AdvertisingDataReader(bytes);
  EXPECT_FALSE(reader.is_valid());
  EXPECT_FALSE(reader.HasMoreData());

  // TLV length is 0. This is not considered malformed. Data should be valid but
  // should not return more data.
  bytes = common::CreateStaticByteBuffer(0x00, 0x00, 0x00, 0x00, 0x00);
  reader = AdvertisingDataReader(bytes);
  EXPECT_TRUE(reader.is_valid());
  EXPECT_FALSE(reader.HasMoreData());

  // First field is valid, second field is not.
  DataType type;
  common::BufferView data;
  bytes = common::CreateStaticByteBuffer(0x02, 0x00, 0x00, 0x02, 0x00);
  reader = AdvertisingDataReader(bytes);
  EXPECT_FALSE(reader.is_valid());
  EXPECT_FALSE(reader.HasMoreData());
  EXPECT_FALSE(reader.GetNextField(&type, &data));

  // First field is valid, second field has length 0.
  bytes = common::CreateStaticByteBuffer(0x02, 0x00, 0x00, 0x00, 0x00);
  reader = AdvertisingDataReader(bytes);
  EXPECT_TRUE(reader.is_valid());
  EXPECT_TRUE(reader.HasMoreData());
  EXPECT_TRUE(reader.GetNextField(&type, &data));
  EXPECT_FALSE(reader.HasMoreData());
  EXPECT_FALSE(reader.GetNextField(&type, &data));
}

TEST(AdvertisingDataTest, ReaderParseFields) {
  auto bytes = common::CreateStaticByteBuffer(
      0x02, 0x01, 0x00,
      0x05, 0x09, 'T', 'e', 's', 't');
  AdvertisingDataReader reader(bytes);
  EXPECT_TRUE(reader.is_valid());
  EXPECT_TRUE(reader.HasMoreData());

  DataType type;
  common::BufferView data;
  EXPECT_TRUE(reader.GetNextField(&type, &data));
  EXPECT_EQ(DataType::kFlags, type);
  EXPECT_EQ(1u, data.GetSize());
  EXPECT_TRUE(common::ContainersEqual(common::CreateStaticByteBuffer(0x00), data));

  EXPECT_TRUE(reader.HasMoreData());
  EXPECT_TRUE(reader.GetNextField(&type, &data));
  EXPECT_EQ(DataType::kCompleteLocalName, type);
  EXPECT_EQ(4u, data.GetSize());
  EXPECT_TRUE(common::ContainersEqual(std::string("Test"), data));

  EXPECT_FALSE(reader.HasMoreData());
  EXPECT_FALSE(reader.GetNextField(&type, &data));
}

}  // namespace
}  // namespace gap
}  // namespace bluetooth
