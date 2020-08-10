// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.
//
// Unittest code for the functions in fuchsia_porting.h.

#include <lib/mock-function/mock-function.h>
#include <stdio.h>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fuchsia_porting.h"
}

namespace {

class FuchsiaPortingTest : public zxtest::Test {
 public:
  FuchsiaPortingTest() {}
  ~FuchsiaPortingTest() {}
};

TEST_F(FuchsiaPortingTest, __set_bit) {
  unsigned long masks[] = {0, 0};

  // Test LSB at first addr.
  __set_bit(0, masks);
  EXPECT_EQ(masks[0], 0x0000000000000001);
  EXPECT_EQ(masks[1], 0x0000000000000000);

  // Test MSB at first addr.
  __set_bit(63, masks);
  EXPECT_EQ(masks[0], 0x8000000000000001);
  EXPECT_EQ(masks[1], 0x0000000000000000);

  // Test LSB at 2nd addr.
  __set_bit(64, masks);
  EXPECT_EQ(masks[0], 0x8000000000000001);
  EXPECT_EQ(masks[1], 0x0000000000000001);
}

TEST_F(FuchsiaPortingTest, set_bit) {
  unsigned long masks[] = {0, 0};

  // Test LSB at first addr.
  set_bit(0, masks);
  EXPECT_EQ(masks[0], 0x0000000000000001);
  EXPECT_EQ(masks[1], 0x0000000000000000);

  // Test MSB at first addr.
  set_bit(63, masks);
  EXPECT_EQ(masks[0], 0x8000000000000001);
  EXPECT_EQ(masks[1], 0x0000000000000000);

  // Test LSB at 2nd addr.
  set_bit(64, masks);
  EXPECT_EQ(masks[0], 0x8000000000000001);
  EXPECT_EQ(masks[1], 0x0000000000000001);
}

TEST_F(FuchsiaPortingTest, clear_bit) {
  unsigned long masks[] = {0xffffffffffffffff, 0xffffffffffffffff};

  // Test LSB at first addr.
  clear_bit(0, masks);
  EXPECT_EQ(masks[0], 0xfffffffffffffffe);
  EXPECT_EQ(masks[1], 0xffffffffffffffff);

  // Test MSB at first addr.
  clear_bit(63, masks);
  EXPECT_EQ(masks[0], 0x7ffffffffffffffe);
  EXPECT_EQ(masks[1], 0xffffffffffffffff);

  // Test LSB at 2nd addr.
  clear_bit(64, masks);
  EXPECT_EQ(masks[0], 0x7ffffffffffffffe);
  EXPECT_EQ(masks[1], 0xfffffffffffffffe);
}

TEST_F(FuchsiaPortingTest, bits_to_ints) {
  EXPECT_EQ(0, BITS_TO_INTS(0));
  EXPECT_EQ(1, BITS_TO_INTS(1));
  EXPECT_EQ(1, BITS_TO_INTS(BITS_PER_INT - 1));
  EXPECT_EQ(1, BITS_TO_INTS(BITS_PER_INT));
  EXPECT_EQ(2, BITS_TO_INTS(BITS_PER_INT + 1));
}

TEST_F(FuchsiaPortingTest, bits_to_longs) {
  EXPECT_EQ(0, BITS_TO_LONGS(0));
  EXPECT_EQ(1, BITS_TO_LONGS(1));
  EXPECT_EQ(1, BITS_TO_LONGS(BITS_PER_LONG - 1));
  EXPECT_EQ(1, BITS_TO_LONGS(BITS_PER_LONG));
  EXPECT_EQ(2, BITS_TO_LONGS(BITS_PER_LONG + 1));
}

TEST_F(FuchsiaPortingTest, eth_broadcast_addr) {
  uint8_t mac[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde};
  eth_broadcast_addr(mac);

  uint8_t expected[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xde};
  EXPECT_BYTES_EQ(expected, mac, sizeof(expected));
}

TEST_F(FuchsiaPortingTest, find_first_bit) {
  unsigned test0[] = {
      0x00000000,
      0x00000000,
  };
  EXPECT_EQ(1, find_first_bit(test0, 1));    // Not found
  EXPECT_EQ(31, find_first_bit(test0, 31));  // Not found
  EXPECT_EQ(32, find_first_bit(test0, 32));  // Not found
  EXPECT_EQ(64, find_first_bit(test0, 64));  // Not found

  unsigned test1[] = {
      0x80000000,
      0xdead0001,
  };
  ASSERT_EQ(sizeof(*test1), 4);
  EXPECT_EQ(1, find_first_bit(test1, 1));    // Not found
  EXPECT_EQ(31, find_first_bit(test1, 31));  // Not found
  EXPECT_EQ(31, find_first_bit(test1, 32));
  EXPECT_EQ(31, find_first_bit(test1, 33));
  EXPECT_EQ(31, find_first_bit(test1, 64));

  unsigned test2[] = {
      0x00000000,
      0xdead0001,
  };
  EXPECT_EQ(31, find_first_bit(test2, 31));  // Not found
  EXPECT_EQ(32, find_first_bit(test2, 32));  // Not found
  EXPECT_EQ(32, find_first_bit(test2, 33));
  EXPECT_EQ(32, find_first_bit(test2, 64));
}

TEST_F(FuchsiaPortingTest, HexDumpErrorHandling) {
  char buf[HEX_DUMP_BUF_SIZE - 1];
  uint8_t data[] = {};

  // buffer underrun case
  ASSERT_EQ(nullptr, hex_dump_str(buf, sizeof(buf), data, sizeof(data)));

  // attempt to dump too many bytes
  ASSERT_EQ(nullptr, hex_dump_str(buf, sizeof(buf), data, 17));
}

TEST_F(FuchsiaPortingTest, HexDumpExactly16Bytes) {
  char buf[HEX_DUMP_BUF_SIZE];
  uint8_t data[] = {
      0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
      0xde, 0xad, 0xbe, 0xef, 0x55, 0x66, 0x01, 0x83,
  };

  ASSERT_EQ(buf, hex_dump_str(buf, sizeof(buf), data, sizeof(data)));

  // Hex value part
  EXPECT_EQ('0', buf[0]);  // the first byte: 0x01
  EXPECT_EQ('1', buf[1]);
  EXPECT_EQ(':', buf[2]);
  EXPECT_EQ('8', buf[15 * 3 + 0]);  // the last byte: 0x83
  EXPECT_EQ('3', buf[15 * 3 + 1]);
  EXPECT_EQ(':', buf[15 * 3 + 2]);

  // ASCII part
  EXPECT_EQ(kNP, buf[50]);                      // non-printable
  EXPECT_EQ('E', buf[52]);                      // printable
  EXPECT_EQ(kNP, buf[54]);                      // non-printable
  EXPECT_EQ(kNP, buf[55]);                      // the last byte: non-printable
  EXPECT_EQ('\0', buf[HEX_DUMP_BUF_SIZE - 1]);  // null-terminator
}

TEST_F(FuchsiaPortingTest, HexDumpLessThan16Bytes) {
  char buf[HEX_DUMP_BUF_SIZE];
  uint8_t data[] = {
      0x61,
  };

  ASSERT_EQ(buf, hex_dump_str(buf, sizeof(buf), data, sizeof(data)));

  // Hex value part
  EXPECT_EQ('6', buf[0]);  // the first byte: 0x61
  EXPECT_EQ('1', buf[1]);
  EXPECT_EQ(':', buf[2]);
  EXPECT_EQ(' ', buf[3]);  // the second byte: not dumped.
  EXPECT_EQ(' ', buf[4]);
  EXPECT_EQ(' ', buf[5]);

  // ASCII part
  EXPECT_EQ('a', buf[50]);                      // printable
  EXPECT_EQ(' ', buf[52]);                      // the second byte: not dumped.
  EXPECT_EQ('\0', buf[HEX_DUMP_BUF_SIZE - 1]);  // null-terminator
}

TEST_F(FuchsiaPortingTest, HexDumpZeroByte) {
  char buf[HEX_DUMP_BUF_SIZE];
  uint8_t data[] = {};

  ASSERT_EQ(buf, hex_dump_str(buf, sizeof(buf), data, sizeof(data)));

  // Hex value part
  EXPECT_EQ(' ', buf[0]);  // nothing dumped
  EXPECT_EQ(' ', buf[1]);
  EXPECT_EQ(' ', buf[2]);

  // ASCII part
  EXPECT_EQ(' ', buf[50]);                      // nothing dumped
  EXPECT_EQ('\0', buf[HEX_DUMP_BUF_SIZE - 1]);  // null-terminator
}
}  // namespace
