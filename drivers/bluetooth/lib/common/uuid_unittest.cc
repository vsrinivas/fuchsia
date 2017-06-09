// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/lib/common/uuid.h"

#include "gtest/gtest.h"

#include "apps/bluetooth/lib/common/byte_buffer.h"

namespace bluetooth {
namespace common {
namespace {

// Variants of 16-bit ID 180d
constexpr uint16_t kId1As16 = 0x180d;
constexpr uint32_t kId1As32 = 0x0000180d;
constexpr UInt128 kId1As128 = {{0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00,
                                0x00, 0x0d, 0x18, 0x00, 0x00}};
constexpr char kId1AsString[] = "0000180d-0000-1000-8000-00805f9b34fb";

// 16-bit ID for comparison
constexpr uint16_t kOther16BitId = 0x1800;

// Variants of 32-bit ID 0x12341234
constexpr uint32_t kId2As32 = 0x12341234;
constexpr UInt128 kId2As128 = {{0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00,
                                0x00, 0x34, 0x12, 0x034, 0x12}};
constexpr char kId2AsString[] = "12341234-0000-1000-8000-00805f9b34fb";

constexpr UInt128 kId3As128 = {{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
                                0x0B,

                                // Make this part be the same as kId1* for the sake of testing.
                                0x0d, 0x18, 0x00, 0x00}};
constexpr char kId3AsString[] = "0000180d-0b0a-0908-0706-050403020100";

TEST(UUIDTest, 16Bit) {
  UUID uuid(kId1As16);

  // We perform each comparison twice, swapping the lhs and rhs, to test the top-level equality
  // operators.

  // Direct comparison with uint16_t.
  EXPECT_EQ(uuid, kId1As16);
  EXPECT_NE(uuid, kOther16BitId);
  EXPECT_EQ(kId1As16, uuid);
  EXPECT_NE(kOther16BitId, uuid);

  // Direct comparison with uint32_t.
  EXPECT_EQ(uuid, kId1As32);
  EXPECT_NE(uuid, kId2As32);
  EXPECT_EQ(kId1As32, uuid);
  EXPECT_NE(kId2As32, uuid);

  // Direct comparison with UInt128.
  EXPECT_EQ(kId1As128, uuid);
  EXPECT_NE(kId2As128, uuid);

  // Direct comparison with UUID.
  EXPECT_EQ(UUID(kId1As16), uuid);
  EXPECT_EQ(UUID(kId1As32), uuid);
  EXPECT_EQ(UUID(static_cast<uint16_t>(kId1As32)), uuid);
  EXPECT_EQ(UUID(kId1As128), uuid);
  EXPECT_NE(UUID(kOther16BitId), uuid);
  EXPECT_NE(UUID(kId2As32), uuid);
  EXPECT_NE(UUID(kId2As128), uuid);
}

TEST(UUIDTest, 32Bit) {
  UUID uuid(kId2As32);

  // Direct comparison with uint32_t.
  EXPECT_EQ(uuid, kId2As32);
  EXPECT_EQ(kId2As32, uuid);
  EXPECT_NE(uuid, kId1As32);
  EXPECT_NE(kId1As32, uuid);

  // Direct comparison with UInt128.
  EXPECT_EQ(kId2As128, uuid);
  EXPECT_NE(kId1As128, uuid);

  // Direct comparison with UUID.
  EXPECT_EQ(UUID(kId2As32), uuid);
  EXPECT_EQ(UUID(kId2As128), uuid);
  EXPECT_NE(UUID(kId1As16), uuid);
  EXPECT_NE(UUID(kId1As32), uuid);
  EXPECT_NE(UUID(kId1As128), uuid);
}

TEST(UUIDTest, 128Bit) {
  UUID uuid(kId3As128);

  EXPECT_EQ(kId3As128, uuid);

  // 16-bit and 32-bit comparison should fail as the base-UUID portions do not match.
  EXPECT_NE(kId1As16, uuid);
  EXPECT_NE(kId1As32, uuid);

  EXPECT_EQ(UUID(kId3As128), uuid);
  EXPECT_NE(UUID(kId1As128), uuid);
}

TEST(UUIDTest, CompareBytes) {
  auto kUUID16Bytes = common::CreateStaticByteBuffer(0x0d, 0x18);
  auto kUUID32Bytes = common::CreateStaticByteBuffer(0x0d, 0x18, 0x00, 0x00);
  auto kUUID128Bytes =
      common::CreateStaticByteBuffer(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10,
                                     0x00, 0x00, 0x0d, 0x18, 0x00, 0x00);

  UUID uuid(kId1As16);
  EXPECT_TRUE(uuid.CompareBytes(kUUID16Bytes));
  EXPECT_TRUE(uuid.CompareBytes(kUUID32Bytes));
  EXPECT_TRUE(uuid.CompareBytes(kUUID128Bytes));

  common::BufferView empty;
  EXPECT_FALSE(uuid.CompareBytes(empty));
}

TEST(UUIDTest, ToString) {
  EXPECT_EQ(kId1AsString, UUID(kId1As16).ToString());
  EXPECT_EQ(kId1AsString, UUID(kId1As32).ToString());
  EXPECT_EQ(kId1AsString, UUID(kId1As128).ToString());

  EXPECT_EQ(kId2AsString, UUID(kId2As32).ToString());
  EXPECT_EQ(kId2AsString, UUID(kId2As128).ToString());

  EXPECT_EQ(kId3AsString, UUID(kId3As128).ToString());
}

TEST(UUIDTest, IsStringValidUUID) {
  EXPECT_FALSE(IsStringValidUUID("0000180d00001000800000805f9b34fb"));
  EXPECT_FALSE(IsStringValidUUID("0000180d-0000-1000-8000000805f9b34fb"));
  EXPECT_FALSE(IsStringValidUUID("0000180d-0000-100008000-00805f9b34fb"));
  EXPECT_FALSE(IsStringValidUUID("0000180d-000001000-8000-00805f9b34fb"));
  EXPECT_FALSE(IsStringValidUUID("0000180d00000-1000-8000-00805f9b34fb"));
  EXPECT_FALSE(IsStringValidUUID("0000180d-0000-1000-8000-00805g9b34fb"));
  EXPECT_FALSE(IsStringValidUUID("000-180d-0000-1000-8000-00805f9b34fb"));

  // Combinations of lower and upper case characters should work.
  EXPECT_TRUE(IsStringValidUUID("0000180d-0000-1000-8000-00805f9b34fb"));
  EXPECT_TRUE(IsStringValidUUID("0000180D-0000-1000-8000-00805F9B34FB"));
  EXPECT_TRUE(IsStringValidUUID("0000180d-0000-1000-8000-00805F9b34fB"));
  EXPECT_TRUE(IsStringValidUUID(kId2AsString));
  EXPECT_TRUE(IsStringValidUUID(kId3AsString));
}

TEST(UUIDTest, StringToUUID) {
  UUID uuid;

  EXPECT_FALSE(StringToUUID("0000180d00001000800000805f9b34fb", &uuid));
  EXPECT_FALSE(StringToUUID("0000180d-0000-1000-8000000805f9b34fb", &uuid));
  EXPECT_FALSE(StringToUUID("0000180d-0000-100008000-00805f9b34fb", &uuid));
  EXPECT_FALSE(StringToUUID("0000180d-000001000-8000-00805f9b34fb", &uuid));
  EXPECT_FALSE(StringToUUID("0000180d00000-1000-8000-00805f9b34fb", &uuid));
  EXPECT_FALSE(StringToUUID("0000180d-0000-1000-8000-00805g9b34fb", &uuid));
  EXPECT_FALSE(StringToUUID("000-180d-0000-1000-8000-00805f9b34fb", &uuid));

  // Combinations of lower and upper case characters should work.
  EXPECT_TRUE(StringToUUID("0000180d-0000-1000-8000-00805f9b34fb", &uuid));
  EXPECT_EQ(kId1As16, uuid);
  EXPECT_TRUE(StringToUUID("0000180D-0000-1000-8000-00805F9B34FB", &uuid));
  EXPECT_EQ(kId1As16, uuid);
  EXPECT_TRUE(StringToUUID("0000180d-0000-1000-8000-00805F9b34fB", &uuid));
  EXPECT_EQ(kId1As16, uuid);

  EXPECT_TRUE(StringToUUID(kId2AsString, &uuid));
  EXPECT_EQ(kId2As32, uuid);

  EXPECT_TRUE(StringToUUID(kId3AsString, &uuid));
  EXPECT_EQ(kId3As128, uuid.value());
}

TEST(UUIDTest, FromBytes) {
  auto kUUID16Bytes = common::CreateStaticByteBuffer(0x0d, 0x18);
  auto kUUID32Bytes = common::CreateStaticByteBuffer(0x0d, 0x18, 0x00, 0x00);
  auto kUUID128Bytes =
      common::CreateStaticByteBuffer(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10,
                                     0x00, 0x00, 0x0d, 0x18, 0x00, 0x00);

  auto kInvalid0 = common::CreateStaticByteBuffer(0x0d);
  auto kInvalid1 = common::CreateStaticByteBuffer(0x0d, 0x18, 0x00);
  common::BufferView kInvalid2;

  UUID uuid;

  EXPECT_FALSE(UUID::FromBytes(kInvalid0, &uuid));
  EXPECT_FALSE(UUID::FromBytes(kInvalid1, &uuid));
  EXPECT_FALSE(UUID::FromBytes(kInvalid2, &uuid));

  EXPECT_TRUE(UUID::FromBytes(kUUID16Bytes, &uuid));
  EXPECT_EQ(kId1As16, uuid);
  EXPECT_TRUE(UUID::FromBytes(kUUID32Bytes, &uuid));
  EXPECT_EQ(kId1As16, uuid);
  EXPECT_TRUE(UUID::FromBytes(kUUID128Bytes, &uuid));
  EXPECT_EQ(kId1As16, uuid);
}

}  // namespace
}  // namespace common
}  // namespace bluetooth
