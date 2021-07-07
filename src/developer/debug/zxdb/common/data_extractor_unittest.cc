// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/data_extractor.h"

#include <limits>

#include <gtest/gtest.h>

namespace zxdb {

TEST(DataExtractor, Empty) {
  DataExtractor ext;
  EXPECT_TRUE(ext.done());
  EXPECT_EQ(0u, ext.cur());

  ext.Advance(1);
  EXPECT_EQ(0u, ext.cur());

  ext.Seek(1);
  EXPECT_EQ(0u, ext.cur());

  EXPECT_FALSE(ext.Read<uint8_t>());
}

TEST(DataExtractor, SeekAdvance) {
  std::vector<uint8_t> buffer{0x01, 0x02, 0x03, 0x04};
  DataExtractor ext(buffer);
  EXPECT_EQ(0u, ext.cur());

  ext.Advance(2);
  EXPECT_EQ(2u, ext.cur());

  ext.Advance(2);
  EXPECT_EQ(4u, ext.cur());

  // Should stop at the end.
  ext.Advance(2);
  EXPECT_EQ(4u, ext.cur());

  // Seek back tot he beginning.
  ext.Seek(0);
  EXPECT_EQ(0u, ext.cur());

  // Advance and Seek clamps to the end.
  ext.Advance(2000);
  EXPECT_EQ(4u, ext.cur());
  ext.Seek(2000);
  EXPECT_EQ(4u, ext.cur());
}

TEST(DataExtractor, Numbers) {
  // clang-format off
  std::vector<uint8_t> buffer{
      0x10,
      0x20, 0x21,
      0x30, 0x31, 0x32, 0x33,
      0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47 };
  // clang-format on

  // Signed integers (little-endian).
  DataExtractor ext(buffer);
  EXPECT_FALSE(ext.done());
  EXPECT_EQ(std::optional<int8_t>(0x10), ext.Read<int8_t>());
  EXPECT_EQ(std::optional<int16_t>(0x2120), ext.Read<int16_t>());
  EXPECT_EQ(std::optional<int32_t>(0x33323130), ext.Read<int32_t>());
  EXPECT_EQ(std::optional<int64_t>(0x4746454443424140), ext.Read<int64_t>());
  EXPECT_EQ(std::optional<int8_t>(), ext.Read<int8_t>());  // Read past end fails.
  EXPECT_TRUE(ext.done());

  // Unsigned integers (little-endian).
  ext = DataExtractor(buffer);
  EXPECT_EQ(std::optional<uint8_t>(0x10), ext.Read<uint8_t>());
  EXPECT_EQ(std::optional<uint16_t>(0x2120), ext.Read<uint16_t>());
  EXPECT_EQ(std::optional<uint32_t>(0x33323130), ext.Read<uint32_t>());
  EXPECT_EQ(std::optional<uint64_t>(0x4746454443424140), ext.Read<uint64_t>());

  // Reading past the end should leave the position unchanged (buffer is 15 bytes long).
  ext = DataExtractor(buffer);
  ext.Read<uint64_t>();
  EXPECT_EQ(8u, ext.cur());
  EXPECT_FALSE(ext.Read<uint64_t>());  // Read fails.
  EXPECT_EQ(8u, ext.cur());            // Position is unchanged.
}

TEST(DataExtractor, Manual) {
  std::vector<uint8_t> buffer{0x01, 0x02, 0x03, 0x04};

  DataExtractor ext(buffer);
  EXPECT_EQ(0u, ext.cur());
  EXPECT_TRUE(ext.CanRead(1));
  EXPECT_TRUE(ext.CanRead(4));
  EXPECT_FALSE(ext.CanRead(5));

  EXPECT_EQ(0u, ext.cur());
  ext.Advance(2);
  EXPECT_EQ(2u, ext.cur());
  ext.Advance(0);
  EXPECT_EQ(2u, ext.cur());
  EXPECT_TRUE(ext.CanRead(2));
  EXPECT_FALSE(ext.CanRead(3));

  // Advances one-past-the end. Should stop at the end.
  ext.Advance(3);
  EXPECT_EQ(4u, ext.cur());
  EXPECT_TRUE(ext.CanRead(0));
  EXPECT_FALSE(ext.CanRead(1));

  // Test overflow of size variables. In this case size + read_size overflows.
  size_t big_read = std::numeric_limits<size_t>::max() - 1;
  ext = DataExtractor(buffer);
  EXPECT_FALSE(ext.CanRead(big_read));

  // Here cur + read_size overflows.
  ext.Advance(2);
  EXPECT_FALSE(ext.CanRead(big_read));
}

// Test a long encoded number with the high bit not set.
TEST(DataExtractor, ReadLeb128_NoHighBit) {
  //                          |--- First ----|  |- Following
  std::vector<uint8_t> buffer{0xe5, 0x8e, 0x26, 0x04};

  // Read unsigned value.
  DataExtractor ext(buffer);
  std::optional<uint64_t> result_u = ext.ReadUleb128();
  ASSERT_TRUE(result_u);
  EXPECT_EQ(624485ul, *result_u);

  // Read following value.
  result_u = ext.ReadUleb128();
  ASSERT_TRUE(result_u);
  EXPECT_EQ(4ul, *result_u);

  // Read signed value. The high bit on this is not set so the result should be the same.
  ext = DataExtractor(buffer);
  std::optional<int64_t> result_s = ext.ReadSleb128();
  ASSERT_TRUE(result_s);
  EXPECT_EQ(624485l, *result_s);

  // Read following value.
  result_s = ext.ReadSleb128();
  ASSERT_TRUE(result_s);
  EXPECT_EQ(4l, *result_s);

  // Reached the end of the data, can't read any more.
  result_u = ext.ReadUleb128();
  EXPECT_FALSE(result_u);
  result_s = ext.ReadSleb128();
  EXPECT_FALSE(result_s);
}

// The high bit is set on this one so that the signed one should be sign-extended.
TEST(DataExtractor, ReadLeb128_HighBit) {
  //                          |--- First ----|  |- Following
  std::vector<uint8_t> buffer{0xc0, 0xbb, 0x78, 0x7f};

  // Read unsigned value.
  DataExtractor ext(buffer);
  std::optional<uint64_t> result_u = ext.ReadUleb128();
  ASSERT_TRUE(result_u);
  EXPECT_EQ(1973696ul, *result_u);

  // Read following value.
  result_u = ext.ReadUleb128();
  ASSERT_TRUE(result_u);
  EXPECT_EQ(0x7ful, *result_u);

  // Read signed value, should be sign-extended to negative.
  ext = DataExtractor(buffer);
  std::optional<int64_t> result_s = ext.ReadSleb128();
  ASSERT_TRUE(result_s);
  EXPECT_EQ(-123456l, *result_s);

  // Read following value.
  result_s = ext.ReadSleb128();
  ASSERT_TRUE(result_s);
  EXPECT_EQ(-1l, *result_s);
}

TEST(DataExtractor, ReadLeb128_Error) {
  // Empty buffer.
  std::vector<uint8_t> empty;
  DataExtractor ext(empty);
  EXPECT_FALSE(ext.ReadSleb128());
  EXPECT_FALSE(ext.ReadUleb128());

  // This buffer has no value without the high bit set so the number won't terminate.
  std::vector<uint8_t> buffer{0xc0, 0xbb};
  ext = DataExtractor(buffer);
  EXPECT_FALSE(ext.ReadSleb128());
  EXPECT_FALSE(ext.ReadUleb128());
}

}  // namespace zxdb
