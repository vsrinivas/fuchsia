// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <cstdint>

#include <fbl/bits.h>
#include <zxtest/zxtest.h>

namespace {

TEST(ExtractBitsTest, BasicCases) {
  EXPECT_EQ(0b1001u, (fbl::ExtractBits<3, 0, uint8_t>(0b10101001u)));
  EXPECT_EQ(0b1u, (fbl::ExtractBit<0, uint8_t>(0b10101001u)));
  EXPECT_EQ(0b0u, (fbl::ExtractBit<2, uint8_t>(0b10101001u)));
  EXPECT_EQ(0b1001u, (fbl::ExtractBits<5, 2, uint8_t>(0b1010100100u)));
  EXPECT_EQ(0b1001u, (fbl::ExtractBits<63, 60, uint64_t>(0x9000000000000000u)));
  EXPECT_EQ(0xe7c07357u, (fbl::ExtractBits<63, 32, uint32_t>(0xe7c0735700000000)));
}

const uint64_t test_val_3b = 0b101;
const uint64_t test_val_4b = 0b1001;
const uint64_t test_val_5b = 0b10001;
const uint64_t test_val_8b = 0b10000001;
const uint64_t test_val_13b = 0b1000000000001;

FBL_BITFIELD_DEF_START(TestBFuint64, uint64_t)
FBL_BITFIELD_MEMBER(m0_3bits, 0, 3);
FBL_BITFIELD_MEMBER(m1_4bits, 3, 4);
FBL_BITFIELD_MEMBER(m2_8bits, 7, 8);
FBL_BITFIELD_MEMBER(m3_13bits, 15, 13);
FBL_BITFIELD_MEMBER(m4_5bits, 28, 5);
FBL_BITFIELD_MEMBER(unused, 33, 31);
FBL_BITFIELD_DEF_END();

TEST(BitfieldTest, ReadWriteUint64) {
  TestBFuint64 bf;
  ASSERT_EQ(bf.value, 0u);

  ASSERT_EQ(bf.m0_3bits.maximum(), 7);
  ASSERT_EQ(bf.m1_4bits.maximum(), 15);
  ASSERT_EQ(bf.m2_8bits.maximum(), 255);
  ASSERT_EQ(bf.m3_13bits.maximum(), 8191);
  ASSERT_EQ(bf.m4_5bits.maximum(), 31);

  uint64_t test_val = 0;
  test_val |= test_val_3b << 0;
  test_val |= test_val_4b << 3;
  test_val |= test_val_8b << 7;
  test_val |= test_val_13b << 15;
  test_val |= test_val_5b << 28;

  bf.value = test_val;
  ASSERT_EQ(bf.m0_3bits, test_val_3b);
  ASSERT_EQ(bf.m1_4bits, test_val_4b);
  ASSERT_EQ(bf.m2_8bits, test_val_8b);
  ASSERT_EQ(bf.m3_13bits, test_val_13b);
  ASSERT_EQ(bf.m4_5bits, test_val_5b);
  ASSERT_EQ(bf.unused, 0u);

  bf.m0_3bits = 0u;
  ASSERT_EQ(bf.m0_3bits, 0u);
  ASSERT_EQ(bf.m1_4bits, test_val_4b);
  ASSERT_EQ(bf.m2_8bits, test_val_8b);
  ASSERT_EQ(bf.m3_13bits, test_val_13b);
  ASSERT_EQ(bf.m4_5bits, test_val_5b);
  ASSERT_EQ(bf.unused, 0u);

  bf.value = test_val;
  bf.m1_4bits = 0u;
  ASSERT_EQ(bf.m0_3bits, test_val_3b);
  ASSERT_EQ(bf.m1_4bits, 0u);
  ASSERT_EQ(bf.m2_8bits, test_val_8b);
  ASSERT_EQ(bf.m3_13bits, test_val_13b);
  ASSERT_EQ(bf.m4_5bits, test_val_5b);
  ASSERT_EQ(bf.unused, 0u);

  bf.value = test_val;
  bf.m2_8bits = 0u;
  ASSERT_EQ(bf.m0_3bits, test_val_3b);
  ASSERT_EQ(bf.m1_4bits, test_val_4b);
  ASSERT_EQ(bf.m2_8bits, 0u);
  ASSERT_EQ(bf.m3_13bits, test_val_13b);
  ASSERT_EQ(bf.m4_5bits, test_val_5b);
  ASSERT_EQ(bf.unused, 0u);

  bf.value = test_val;
  bf.m3_13bits = 0u;
  ASSERT_EQ(bf.m0_3bits, test_val_3b);
  ASSERT_EQ(bf.m1_4bits, test_val_4b);
  ASSERT_EQ(bf.m2_8bits, test_val_8b);
  ASSERT_EQ(bf.m3_13bits, 0u);
  ASSERT_EQ(bf.m4_5bits, test_val_5b);
  ASSERT_EQ(bf.unused, 0u);

  bf.value = test_val;
  bf.m4_5bits = 0u;
  ASSERT_EQ(bf.m0_3bits, test_val_3b);
  ASSERT_EQ(bf.m1_4bits, test_val_4b);
  ASSERT_EQ(bf.m2_8bits, test_val_8b);
  ASSERT_EQ(bf.m3_13bits, test_val_13b);
  ASSERT_EQ(bf.m4_5bits, 0u);
  ASSERT_EQ(bf.unused, 0u);
}

TEST(BitfieldTest, AssignFromBitField) {
  TestBFuint64 bf1, bf2;
  ASSERT_EQ(bf1.value, 0u);
  ASSERT_EQ(bf2.value, 0u);

  bf1.m1_4bits = test_val_4b;
  bf2.m2_8bits = test_val_8b;

  bf1.m2_8bits = bf2.m2_8bits;
  // |bf1.m1_4bits| should not be overwritten.
  ASSERT_EQ(bf1.m1_4bits, test_val_4b);
  ASSERT_EQ(bf1.m2_8bits, test_val_8b);
}

constexpr TestBFuint64 cex_bf_uint64;
static_assert(sizeof(cex_bf_uint64.m0_3bits) == sizeof(uint64_t));

union Rights {
  uint32_t raw_value = 0u;
  fbl::BitFieldMember<uint32_t, 0, 1> read;
  fbl::BitFieldMember<uint32_t, 1, 1> write;
  fbl::BitFieldMember<uint32_t, 2, 1> admin;
  fbl::BitFieldMember<uint32_t, 3, 1> execute;

  constexpr Rights() {}

  constexpr static Rights ReadExec() {
    Rights rights;
    rights.read = true;
    rights.execute = true;
    return rights;
  }
};

TEST(BitfieldTest, AssignMultipleValuesThenRead) {
  auto rights = Rights::ReadExec();
  // (read | execute) should be (1 | 8) == 9.  gcc 8.3 produced the value 8 for this
  // scenario when not using std::memcpy() to assign values: https://godbolt.org/z/YBBCKz
  ASSERT_EQ(rights.raw_value, 9);
}

union ByteBitfield {
  uint8_t value = 0;
  fbl::BitFieldMember<uint8_t, 0, 4> low_nibble;
  fbl::BitFieldMember<uint8_t, 7, 1> high_bit;
};

TEST(BitfieldTest, ReadWriteUint8) {
  ByteBitfield byte;
  ASSERT_EQ(byte.value, 0);

  byte.value = 0xFC;
  ASSERT_EQ(byte.low_nibble, 0x0C);
  ASSERT_EQ(byte.high_bit, 1);

  byte.high_bit = 0;
  byte.low_nibble = 0x05;
  ASSERT_EQ(byte.value, 0x75);
}

}  // namespace
