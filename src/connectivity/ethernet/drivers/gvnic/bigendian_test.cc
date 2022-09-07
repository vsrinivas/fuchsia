// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/ethernet/drivers/gvnic/bigendian.h"

#include <stdint.h>
#include <string.h>

#include <zxtest/zxtest.h>

namespace gvnic {

// "Le" tests verify that this machine is little endian, and does not use the BigEndian class.
// "Be" tests check the actual BigEndian class.

// Just to distinguish them, and to prevent tests passing out of coincidence, the bytes of the
// read value are numbered from 0x01, while the bytes of the write value are numbered from 0xf1.

TEST(BigEndianTest, Le16) {
  uint16_t value;
  uint8_t i, *bytes = reinterpret_cast<uint8_t *>(&value);

  // Check that reads are swapped correctly.
  for (i = 0; i < sizeof(uint16_t); i++) {
    bytes[i] = i + 0x01;
  }
  EXPECT_EQ(0x0201U, value);

  // Check that writes are swapped correctly.
  value = 0xf2f1;
  for (i = 0; i < sizeof(uint16_t); i++) {
    EXPECT_EQ(i + 0xf1, bytes[i]);
  }
}

TEST(BigEndianTest, Be16) {
  BigEndian<uint16_t> value;
  uint8_t i, *bytes = reinterpret_cast<uint8_t *>(&value);

  // Check that reads are swapped correctly.
  for (i = 0; i < sizeof(BigEndian<uint16_t>); i++) {
    bytes[i] = i + 0x01;
  }
  EXPECT_EQ(0x0102U, value);

  // Check that writes are swapped correctly.
  value = 0xf1f2;
  for (i = 0; i < sizeof(BigEndian<uint16_t>); i++) {
    EXPECT_EQ(i + 0xf1, bytes[i]);
  }
}

TEST(BigEndianTest, Le32) {
  uint32_t value;
  uint8_t i, *bytes = reinterpret_cast<uint8_t *>(&value);

  // Check that reads are swapped correctly.
  for (i = 0; i < sizeof(uint32_t); i++) {
    bytes[i] = i + 0x01;
  }
  EXPECT_EQ(0x04030201U, value);

  // Check that writes are swapped correctly.
  value = 0xf4f3f2f1;
  for (i = 0; i < sizeof(uint32_t); i++) {
    EXPECT_EQ(i + 0xf1, bytes[i]);
  }
}

TEST(BigEndianTest, Be32) {
  BigEndian<uint32_t> value;
  uint8_t i, *bytes = reinterpret_cast<uint8_t *>(&value);

  // Check that reads are swapped correctly.
  for (i = 0; i < sizeof(BigEndian<uint32_t>); i++) {
    bytes[i] = i + 0x01;
  }
  EXPECT_EQ(0x01020304U, value);

  // Check that writes are swapped correctly.
  value = 0xf1f2f3f4;
  for (i = 0; i < sizeof(BigEndian<uint32_t>); i++) {
    EXPECT_EQ(i + 0xf1, bytes[i]);
  }
}

TEST(BigEndianTest, Le64) {
  uint64_t value;
  uint8_t i, *bytes = reinterpret_cast<uint8_t *>(&value);

  // Check that reads are swapped correctly.
  for (i = 0; i < sizeof(uint64_t); i++) {
    bytes[i] = i + 0x01;
  }
  EXPECT_EQ(0x0807060504030201U, value);

  // Check that writes are swapped correctly.
  value = 0xf8f7f6f5f4f3f2f1;
  for (i = 0; i < sizeof(uint64_t); i++) {
    EXPECT_EQ(i + 0xf1, bytes[i]);
  }
}

TEST(BigEndianTest, Be64) {
  BigEndian<uint64_t> value;
  uint8_t i, *bytes = reinterpret_cast<uint8_t *>(&value);

  // Check that reads are swapped correctly.
  for (i = 0; i < sizeof(BigEndian<uint64_t>); i++) {
    bytes[i] = i + 0x01;
  }
  EXPECT_EQ(0x0102030405060708U, value);

  // Check that writes are swapped correctly.
  value = 0xf1f2f3f4f5f6f7f8;
  for (i = 0; i < sizeof(BigEndian<uint64_t>); i++) {
    EXPECT_EQ(i + 0xf1, bytes[i]);
  }
}

// The Gvnic spec breaks 64bit values into two 32bit halves, first hi then lo.
// This checks that it is equivalent to a 64bit value, which is more convenient to use.
TEST(BigEndianTest, HiLoBe32IsBe64) {
  union {
    struct {
      BigEndian<uint32_t> hi;
      BigEndian<uint32_t> lo;
    } half;
    BigEndian<uint64_t> value;
  } hilo;

  hilo.half.hi = 0xaabbccdd;
  hilo.half.lo = 0x11223344;
  EXPECT_EQ(0xaabbccdd11223344, hilo.value);

  hilo.value = 0xfeedfacecafef00d;
  EXPECT_EQ(0xfeedface, hilo.half.hi);
  EXPECT_EQ(0xcafef00d, hilo.half.lo);
}

}  // namespace gvnic
