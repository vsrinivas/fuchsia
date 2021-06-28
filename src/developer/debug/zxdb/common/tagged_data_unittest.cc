// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/tagged_data.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/tagged_data_builder.h"

namespace zxdb {

TEST(TaggedData, AllValid) {
  // Empty range.
  TaggedData empty;
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(0u, empty.size());
  EXPECT_TRUE(empty.all_valid());
  EXPECT_EQ("", empty.ToString());

  EXPECT_TRUE(empty.RangeIsEntirely(0, 0, TaggedData::kValid));
  EXPECT_TRUE(empty.RangeContains(0, 0, TaggedData::kValid));
  EXPECT_FALSE(empty.RangeIsEntirely(0, 0, TaggedData::kUnknown));
  EXPECT_FALSE(empty.RangeContains(0, 0, TaggedData::kUnknown));

  // Nonempty range.
  std::vector<uint8_t> d{0, 1, 2, 3};
  TaggedData full(d);
  EXPECT_FALSE(full.empty());
  EXPECT_EQ(4u, full.size());
  EXPECT_EQ(d, full.bytes());
  EXPECT_TRUE(full.all_valid());
  EXPECT_EQ("00 01 02 03\n", full.ToString());

  EXPECT_TRUE(full.RangeIsEntirely(0, 4, TaggedData::kValid));
  EXPECT_TRUE(full.RangeContains(0, 4, TaggedData::kValid));
  EXPECT_FALSE(full.RangeIsEntirely(0, 4, TaggedData::kUnknown));
  EXPECT_FALSE(full.RangeContains(0, 4, TaggedData::kUnknown));

  // Test some valid extractions.
  std::optional<TaggedData> extracted = empty.Extract(0, 0);
  ASSERT_TRUE(extracted);
  EXPECT_TRUE(extracted->empty());
  EXPECT_TRUE(extracted->all_valid());

  extracted = full.Extract(1, 2);
  ASSERT_TRUE(extracted);
  EXPECT_EQ(2u, extracted->size());
  EXPECT_TRUE(extracted->all_valid());
  EXPECT_EQ(1, extracted->bytes()[0]);
  EXPECT_EQ(2, extracted->bytes()[1]);

  extracted = full.Extract(3, 1);
  ASSERT_TRUE(extracted);
  EXPECT_EQ(1u, extracted->size());
  EXPECT_EQ(3, extracted->bytes()[0]);

  // Out-of-bound extraction.
  EXPECT_FALSE(empty.Extract(1, 0));
  EXPECT_FALSE(empty.Extract(1, 1));
  EXPECT_FALSE(full.Extract(0, 5));
  EXPECT_FALSE(full.Extract(4, 1));
}

TEST(TaggedData, AllValidBuilder) {
  TaggedDataBuilder builder;
  EXPECT_TRUE(builder.TakeData().empty());

  std::vector<uint8_t> extra{10, 11, 12, 13};

  builder.Append({0, 1, 2, 3});
  builder.Append({});
  builder.Append(extra);

  TaggedData data = builder.TakeData();
  ASSERT_EQ(8u, data.size());
  std::vector<uint8_t> expected{0, 1, 2, 3, 10, 11, 12, 13};
  EXPECT_EQ(expected, data.bytes());
}

TEST(TaggedData, SomeInvalid) {
  TaggedDataBuilder builder;

  // Entirely invalid.
  builder.AppendUnknown(4);
  TaggedData data = builder.TakeData();
  EXPECT_EQ(4u, data.size());
  EXPECT_FALSE(data.all_valid());

  // Partially valid.
  builder.AppendUnknown(2);
  builder.Append({1, 2});
  builder.Append({});
  builder.AppendUnknown(0);
  builder.AppendUnknown(2);
  data = builder.TakeData();
  EXPECT_EQ(6u, data.size());
  EXPECT_FALSE(data.all_valid());

  // Unknown bytes are 0's in the data buffer.
  std::vector<uint8_t> expected_full{0, 0, 1, 2, 0, 0};
  EXPECT_EQ(expected_full, data.bytes());
  EXPECT_EQ("?? ?? 01 02 ?? ??\n", data.ToString());

  EXPECT_TRUE(data.RangeContains(0, 6, TaggedData::kValid));
  EXPECT_TRUE(data.RangeContains(0, 6, TaggedData::kUnknown));
  EXPECT_FALSE(data.RangeIsEntirely(0, 6, TaggedData::kValid));
  EXPECT_FALSE(data.RangeIsEntirely(0, 6, TaggedData::kUnknown));

  EXPECT_FALSE(data.RangeContains(2, 2, TaggedData::kUnknown));
  EXPECT_FALSE(data.RangeIsEntirely(2, 2, TaggedData::kUnknown));
  EXPECT_TRUE(data.RangeContains(2, 2, TaggedData::kValid));
  EXPECT_TRUE(data.RangeIsEntirely(2, 2, TaggedData::kValid));

  // Extracted regions.
  std::optional<TaggedData> extracted = data.Extract(1, 3);
  ASSERT_TRUE(extracted);
  EXPECT_FALSE(extracted->all_valid());

  // This sub-region is entirely valid.
  extracted = data.Extract(2, 2);
  ASSERT_TRUE(extracted);
  EXPECT_TRUE(extracted->all_valid());
}

// ToString was tested in a few places above, this tests the multiline case.
TEST(TaggedData, ToString) {
  TaggedDataBuilder builder;
  builder.Append({0x10, 0x11, 0x12, 0x13, 0x14, 0x15});
  builder.AppendUnknown(16);
  builder.Append({0xf0, 0xf1, 0xf2});

  EXPECT_EQ(builder.TakeData().ToString(),
            "10 11 12 13 14 15 ?? ??   ?? ?? ?? ?? ?? ?? ?? ??\n"
            "?? ?? ?? ?? ?? ?? f0 f1   f2\n");
}

}  // namespace zxdb
