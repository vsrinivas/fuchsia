// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.
//
// Unittest code for the platform-support code in platform/.

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/compiler.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"

namespace {

class PlatformTest : public zxtest::Test {
 public:
  PlatformTest() {}
  ~PlatformTest() {}
};

TEST_F(PlatformTest, __set_bit) {
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

TEST_F(PlatformTest, set_bit) {
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

TEST_F(PlatformTest, clear_bit) {
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

TEST_F(PlatformTest, bits_to_ints) {
  EXPECT_EQ(0, BITS_TO_INTS(0));
  EXPECT_EQ(1, BITS_TO_INTS(1));
  EXPECT_EQ(1, BITS_TO_INTS(BITS_PER_INT - 1));
  EXPECT_EQ(1, BITS_TO_INTS(BITS_PER_INT));
  EXPECT_EQ(2, BITS_TO_INTS(BITS_PER_INT + 1));
}

TEST_F(PlatformTest, bits_to_longs) {
  EXPECT_EQ(0, BITS_TO_LONGS(0));
  EXPECT_EQ(1, BITS_TO_LONGS(1));
  EXPECT_EQ(1, BITS_TO_LONGS(BITS_PER_LONG - 1));
  EXPECT_EQ(1, BITS_TO_LONGS(BITS_PER_LONG));
  EXPECT_EQ(2, BITS_TO_LONGS(BITS_PER_LONG + 1));
}

TEST_F(PlatformTest, eth_broadcast_addr) {
  uint8_t mac[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde};
  eth_broadcast_addr(mac);

  uint8_t expected[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xde};
  EXPECT_BYTES_EQ(expected, mac, sizeof(expected));
}

TEST_F(PlatformTest, find_first_bit) {
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

TEST_F(PlatformTest, find_last_bit) {
  unsigned test0[] = {
      0x00000000,
      0x00000000,
  };
  EXPECT_EQ(1, find_last_bit(test0, 1));    // Not found
  EXPECT_EQ(31, find_last_bit(test0, 31));  // Not found
  EXPECT_EQ(32, find_last_bit(test0, 32));  // Not found
  EXPECT_EQ(64, find_last_bit(test0, 64));  // Not found

  unsigned test1[] = {
      0x40000000,
      0x00010001,
  };
  ASSERT_EQ(sizeof(*test1), 4);
  EXPECT_EQ(1, find_last_bit(test1, 1));  // Not found
  EXPECT_EQ(30, find_last_bit(test1, 31));
  EXPECT_EQ(30, find_last_bit(test1, 32));
  EXPECT_EQ(32, find_last_bit(test1, 33));
  EXPECT_EQ(32, find_last_bit(test1, 48));
  EXPECT_EQ(48, find_last_bit(test1, 64));

  unsigned test2[] = {
      0x00000000,
      0x80000000,
  };
  EXPECT_EQ(31, find_last_bit(test2, 31));  // Not found
  EXPECT_EQ(32, find_last_bit(test2, 32));  // Not found
  EXPECT_EQ(33, find_last_bit(test2, 33));  // Not found
  EXPECT_EQ(63, find_last_bit(test2, 64));
}

TEST_F(PlatformTest, find_next_bit) {
  unsigned test[] = {
      0x00001000,
      0x00000000,
  };

  EXPECT_EQ(32, find_next_bit(test, 32, 16));  // Not found
  EXPECT_EQ(12, find_next_bit(test, 32, 0));
  EXPECT_EQ(12, find_next_bit(test, 64, 0));
}

TEST_F(PlatformTest, ieee80211_sn_less) {
  EXPECT_FALSE(ieee80211_sn_less(0x0000, 0x0000));
  EXPECT_FALSE(ieee80211_sn_less(0x0040, 0x0040));
  EXPECT_TRUE(ieee80211_sn_less(0x0000, 0x0001));
  EXPECT_FALSE(ieee80211_sn_less(0x0001, 0x0000));

  EXPECT_TRUE(ieee80211_sn_less(0x0000, 0x07ff));
  EXPECT_FALSE(ieee80211_sn_less(0x07ff, 0x0000));

  EXPECT_FALSE(ieee80211_sn_less(0x0000, 0x0800));
  EXPECT_FALSE(ieee80211_sn_less(0x0800, 0x0000));
  EXPECT_TRUE(ieee80211_sn_less(0x0801, 0x0000));

  EXPECT_TRUE(ieee80211_sn_less(0x0003, 0x0800));

  EXPECT_TRUE(ieee80211_sn_less(0x0f00, 0x0000));

  EXPECT_FALSE(ieee80211_sn_less(0x0fff, 0x07ff));
  EXPECT_FALSE(ieee80211_sn_less(0x07ff, 0x0fff));

  EXPECT_TRUE(ieee80211_sn_less(0x0fff, 0x07fe));
  EXPECT_TRUE(ieee80211_sn_less(0x0800, 0x0fff));
}

}  // namespace
