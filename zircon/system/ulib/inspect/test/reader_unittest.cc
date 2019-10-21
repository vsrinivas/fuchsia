// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>

#include <type_traits>

#include <zxtest/zxtest.h>

using inspect::Inspector;
using inspect::Node;
using inspect::internal::Block;
using inspect::internal::BlockType;
using inspect::internal::ExtentBlockFields;
using inspect::internal::HeaderBlockFields;
using inspect::internal::kMagicNumber;
using inspect::internal::kMinOrderSize;
using inspect::internal::NameBlockFields;
using inspect::internal::PropertyBlockPayload;
using inspect::internal::ValueBlockFields;

namespace {

TEST(Reader, GetByPath) {
  Inspector inspector;
  ASSERT_TRUE(bool(inspector));
  auto child = inspector.GetRoot().CreateChild("test");
  auto child2 = child.CreateChild("test2");

  auto result = inspect::ReadFromVmo(inspector.DuplicateVmo());
  ASSERT_TRUE(result.is_ok());
  auto hierarchy = result.take_value();

  EXPECT_NOT_NULL(hierarchy.GetByPath({"test"}));
  EXPECT_NOT_NULL(hierarchy.GetByPath({"test", "test2"}));
  EXPECT_NULL(hierarchy.GetByPath({"test", "test2", "test3"}));
}

TEST(Reader, BucketComparison) {
  inspect::UintArrayValue::HistogramBucket a(0, 2, 6);
  inspect::UintArrayValue::HistogramBucket b(0, 2, 6);
  inspect::UintArrayValue::HistogramBucket c(1, 2, 6);
  inspect::UintArrayValue::HistogramBucket d(0, 3, 6);
  inspect::UintArrayValue::HistogramBucket e(0, 2, 7);

  EXPECT_TRUE(a == b);
  EXPECT_TRUE(a != c);
  EXPECT_TRUE(b != c);
  EXPECT_TRUE(a != d);
  EXPECT_TRUE(a != e);
}

TEST(Reader, InvalidNameParsing) {
  std::vector<uint8_t> buf;
  buf.resize(4096);

  Block* header = reinterpret_cast<Block*>(buf.data());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], kMagicNumber, 4);
  header->payload.u64 = 0;

  // Manually create a value with an invalid name field.
  Block* value = reinterpret_cast<Block*>(buf.data() + kMinOrderSize);
  value->header = ValueBlockFields::Order::Make(0) |
                  ValueBlockFields::Type::Make(BlockType::kNodeValue) |
                  ValueBlockFields::NameIndex::Make(2000);

  auto result = inspect::ReadFromBuffer(std::move(buf));
  EXPECT_TRUE(result.is_ok());
}

TEST(Reader, LargeExtentsWithCycle) {
  std::vector<uint8_t> buf;
  buf.resize(4096);

  Block* header = reinterpret_cast<Block*>(buf.data());
  header->header = HeaderBlockFields::Order::Make(0) |
                   HeaderBlockFields::Type::Make(BlockType::kHeader) |
                   HeaderBlockFields::Version::Make(0);
  memcpy(&header->header_data[4], kMagicNumber, 4);
  header->payload.u64 = 0;

  // Manually create a property.
  Block* value = reinterpret_cast<Block*>(buf.data() + kMinOrderSize);
  value->header = ValueBlockFields::Order::Make(0) |
                  ValueBlockFields::Type::Make(BlockType::kPropertyValue) |
                  ValueBlockFields::NameIndex::Make(2);
  value->payload.u64 = PropertyBlockPayload::TotalLength::Make(0xFFFFFFFF) |
                       PropertyBlockPayload::ExtentIndex::Make(3);

  Block* name = reinterpret_cast<Block*>(buf.data() + kMinOrderSize * 2);
  name->header = NameBlockFields::Order::Make(0) | NameBlockFields::Type::Make(BlockType::kName) |
                 NameBlockFields::Length::Make(1);
  memcpy(name->payload.data, "a", 2);

  Block* extent = reinterpret_cast<Block*>(buf.data() + kMinOrderSize * 3);
  extent->header = ExtentBlockFields::Order::Make(0) |
                   ExtentBlockFields::Type::Make(BlockType::kExtent) |
                   ExtentBlockFields::NextExtentIndex::Make(3);

  auto result = inspect::ReadFromBuffer(std::move(buf));
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(1u, result.value().node().properties().size());
}

}  // namespace
