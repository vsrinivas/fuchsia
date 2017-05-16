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

// Helper for computing the size of a string literal at compile time. sizeof() would have worked
// too but that counts the null character.
template <std::size_t N>
constexpr size_t StringSize(char const (&str)[N]) {
  return N - 1;
}

TEST(AdvertisingDataTest, WriteField) {
  constexpr char kValue0[] = "value zero";
  constexpr char kValue1[] = "value one";
  constexpr char kValue2[] = "value two";
  constexpr char kValue3[] = "value three";

  // Have just enough space for the first three values (+ 6 for 2 extra octets for each TLV field).
  constexpr char kBufferSize = StringSize(kValue0) + StringSize(kValue1) + StringSize(kValue2) + 6;
  common::StaticByteBuffer<kBufferSize> buffer;

  AdvertisingDataWriter writer(&buffer);
  EXPECT_EQ(0u, writer.bytes_written());

  // We write malformed values here for testing purposes.
  EXPECT_TRUE(writer.WriteField(DataType::kFlags, common::BufferView(kValue0)));
  EXPECT_EQ(StringSize(kValue0) + 2, writer.bytes_written());

  EXPECT_TRUE(writer.WriteField(DataType::kShortenedLocalName, common::BufferView(kValue1)));
  EXPECT_EQ(StringSize(kValue0) + 2 + StringSize(kValue1) + 2, writer.bytes_written());

  // Trying to write kValue3 should fail because there isn't enough room left in the buffer.
  EXPECT_FALSE(writer.WriteField(DataType::kCompleteLocalName, common::BufferView(kValue3)));

  // Writing kValue2 should fill up the buffer.
  EXPECT_TRUE(writer.WriteField(DataType::kCompleteLocalName, common::BufferView(kValue2)));
  EXPECT_FALSE(writer.WriteField(DataType::kCompleteLocalName, common::BufferView(kValue3)));
  EXPECT_EQ(buffer.GetSize(), writer.bytes_written());

  // Verify the contents.
  DataType type;
  common::BufferView value;
  AdvertisingDataReader reader(buffer);
  EXPECT_TRUE(reader.is_valid());

  EXPECT_TRUE(reader.GetNextField(&type, &value));
  EXPECT_EQ(DataType::kFlags, type);
  EXPECT_EQ(kValue0, value.AsString());

  EXPECT_TRUE(reader.GetNextField(&type, &value));
  EXPECT_EQ(DataType::kShortenedLocalName, type);
  EXPECT_EQ(kValue1, value.AsString());

  EXPECT_TRUE(reader.GetNextField(&type, &value));
  EXPECT_EQ(DataType::kCompleteLocalName, type);
  EXPECT_EQ(kValue2, value.AsString());

  EXPECT_FALSE(reader.GetNextField(&type, &value));
}

}  // namespace
}  // namespace gap
}  // namespace bluetooth
