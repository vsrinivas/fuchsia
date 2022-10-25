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

// The uint8_t is an edge case. No actual swappage occurs.
TEST(BigEndianTest, Be8) {
  BigEndian<uint8_t> value;
  uint8_t *bytes = reinterpret_cast<uint8_t *>(&value);

  EXPECT_EQ(sizeof(BigEndian<uint8_t>), 1);
  // Check that reads are (not) swapped correctly.
  bytes[0] = 0x01U;
  EXPECT_EQ(0x01U, value);
  EXPECT_EQ(0x01U, value.GetBE());

  // Check that writes are (not) swapped correctly.
  value = 0xf1U;
  EXPECT_EQ(0xf1U, bytes[0]);

  // Check that SetBE also does nothing.
  EXPECT_EQ(0xa1U, value.SetBE(0xa1U));
  EXPECT_EQ(0xa1U, bytes[0]);
}

TEST(BigEndianTest, Le16) {
  uint16_t value;
  uint8_t i, *bytes = reinterpret_cast<uint8_t *>(&value);

  // Check that reads are swapped correctly.
  for (i = 0; i < sizeof(uint16_t); i++) {
    bytes[i] = i + 0x01U;
  }
  EXPECT_EQ(0x0201U, value);

  // Check that writes are swapped correctly.
  value = 0xf2f1;
  for (i = 0; i < sizeof(uint16_t); i++) {
    EXPECT_EQ(i + 0xf1U, bytes[i]);
  }
}

TEST(BigEndianTest, Be16) {
  BigEndian<uint16_t> value;
  uint8_t i, *bytes = reinterpret_cast<uint8_t *>(&value);

  // Check that reads are swapped correctly.
  for (i = 0; i < sizeof(BigEndian<uint16_t>); i++) {
    bytes[i] = i + 0x01U;
  }
  EXPECT_EQ(0x0102U, value);
  EXPECT_EQ(0x0201U, value.GetBE());

  // Check that writes are swapped correctly.
  value = 0xf1f2U;
  for (i = 0; i < sizeof(BigEndian<uint16_t>); i++) {
    EXPECT_EQ(i + 0xf1U, bytes[i]);
  }

  // Check that SetBE is not swapped when stored, but returns swapped.
  EXPECT_EQ(value.SetBE(0xa2a1U), 0xa1a2U);
  for (i = 0; i < sizeof(BigEndian<uint16_t>); i++) {
    EXPECT_EQ(i + 0xa1U, bytes[i]);
  }
}

TEST(BigEndianTest, Le32) {
  uint32_t value;
  uint8_t i, *bytes = reinterpret_cast<uint8_t *>(&value);

  // Check that reads are swapped correctly.
  for (i = 0; i < sizeof(uint32_t); i++) {
    bytes[i] = i + 0x01U;
  }
  EXPECT_EQ(0x04030201U, value);

  // Check that writes are swapped correctly.
  value = 0xf4f3f2f1U;
  for (i = 0; i < sizeof(uint32_t); i++) {
    EXPECT_EQ(i + 0xf1U, bytes[i]);
  }
}

TEST(BigEndianTest, Be32) {
  BigEndian<uint32_t> value;
  uint8_t i, *bytes = reinterpret_cast<uint8_t *>(&value);

  // Check that reads are swapped correctly.
  for (i = 0; i < sizeof(BigEndian<uint32_t>); i++) {
    bytes[i] = i + 0x01U;
  }
  EXPECT_EQ(0x01020304U, value);
  EXPECT_EQ(0x04030201U, value.GetBE());

  // Check that writes are swapped correctly.
  value = 0xf1f2f3f4U;
  for (i = 0; i < sizeof(BigEndian<uint32_t>); i++) {
    EXPECT_EQ(i + 0xf1U, bytes[i]);
  }

  // Check that SetBE is not swapped when stored, but returns swapped.
  EXPECT_EQ(value.SetBE(0xa4a3a2a1U), 0xa1a2a3a4U);
  for (i = 0; i < sizeof(BigEndian<uint16_t>); i++) {
    EXPECT_EQ(i + 0xa1U, bytes[i]);
  }
}

TEST(BigEndianTest, Le64) {
  uint64_t value;
  uint8_t i, *bytes = reinterpret_cast<uint8_t *>(&value);

  // Check that reads are swapped correctly.
  for (i = 0; i < sizeof(uint64_t); i++) {
    bytes[i] = i + 0x01U;
  }
  EXPECT_EQ(0x0807060504030201U, value);

  // Check that writes are swapped correctly.
  value = 0xf8f7f6f5f4f3f2f1U;
  for (i = 0; i < sizeof(uint64_t); i++) {
    EXPECT_EQ(i + 0xf1U, bytes[i]);
  }
}

TEST(BigEndianTest, Be64) {
  BigEndian<uint64_t> value;
  uint8_t i, *bytes = reinterpret_cast<uint8_t *>(&value);

  // Check that reads are swapped correctly.
  for (i = 0; i < sizeof(BigEndian<uint64_t>); i++) {
    bytes[i] = i + 0x01U;
  }
  EXPECT_EQ(0x0102030405060708U, value);
  EXPECT_EQ(0x0807060504030201U, value.GetBE());

  // Check that writes are swapped correctly.
  value = 0xf1f2f3f4f5f6f7f8U;
  for (i = 0; i < sizeof(BigEndian<uint64_t>); i++) {
    EXPECT_EQ(i + 0xf1U, bytes[i]);
  }

  // Check that SetBE is not swapped when stored, but returns swapped.
  EXPECT_EQ(value.SetBE(0xa8a7a6a5a4a3a2a1U), 0xa1a2a3a4a5a6a7a8U);
  for (i = 0; i < sizeof(BigEndian<uint16_t>); i++) {
    EXPECT_EQ(i + 0xa1U, bytes[i]);
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

  hilo.half.hi = 0xaabbccddU;
  hilo.half.lo = 0x11223344U;
  EXPECT_EQ(0xaabbccdd11223344U, hilo.value);
  EXPECT_EQ(0x44332211ddccbbaaU, hilo.value.GetBE());

  hilo.value = 0xfeedfacecafef00dU;
  EXPECT_EQ(0xfeedfaceU, hilo.half.hi);
  EXPECT_EQ(0xcafef00dU, hilo.half.lo);
  EXPECT_EQ(0xcefaedfeU, hilo.half.hi.GetBE());
  EXPECT_EQ(0x0df0fecaU, hilo.half.lo.GetBE());

  hilo.value.SetBE(0x1baddeadbeefd00dU);
  EXPECT_EQ(0x0dd0efbeU, hilo.half.hi);
  EXPECT_EQ(0xaddead1bU, hilo.half.lo);
  EXPECT_EQ(0xbeefd00dU, hilo.half.hi.GetBE());
  EXPECT_EQ(0x1baddeadU, hilo.half.lo.GetBE());
}

}  // namespace gvnic
