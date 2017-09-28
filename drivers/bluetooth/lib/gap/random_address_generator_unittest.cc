// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/random_address_generator.h"

#include "garnet/drivers/bluetooth/lib/common/device_address.h"

#include "gtest/gtest.h"

namespace bluetooth {
namespace gap {
namespace {

using RandomAddressTest = ::testing::Test;

TEST_F(RandomAddressTest, StaticAddressFormat) {
  RandomAddressGenerator r;

  common::DeviceAddress s1 = r.StaticAddress();

  // They should have the two most significant bytes set

  // TODO(jamuraa): this is an "interesting" way to test the most significant
  // bits. The first char needs to be at least C = 12 (0b1100)
  std::string str = s1.value().ToString();
  unsigned char a = str[0];
  EXPECT_TRUE(a >= 'C');
  EXPECT_TRUE(a <= 'F');
}

TEST_F(RandomAddressTest, StaticAddressPersistence) {
  RandomAddressGenerator r;

  common::DeviceAddress s1 = r.StaticAddress();

  // The same address persists as long as the generator does.
  EXPECT_EQ(s1, r.StaticAddress());

  // But isn't the same in the next generator (modulo "broken" random data)
  RandomAddressGenerator r2;
  EXPECT_NE(s1, r2.StaticAddress());
}

TEST_F(RandomAddressTest, PrivateAddressFormat) {
  common::DeviceAddress s1 = RandomAddressGenerator::PrivateAddress();

  // They should have the two most significant bytes unset

  // TODO(jamuraa): see comment in StaticAddressFormat
  // The first char needs to be at less than 4 (0b0011)
  std::string str = s1.value().ToString();
  unsigned char a = str[0];
  EXPECT_TRUE(a < '4');
  EXPECT_TRUE(a >= '0');
}

TEST_F(RandomAddressTest, PrivateAddressRandomness) {
  RandomAddressGenerator r;

  common::DeviceAddress s1 = RandomAddressGenerator::PrivateAddress();

  // In almost all cases, generating a bunch of random addresses should not
  // match each other.
  for (size_t num = 0; num < 5; num++) {
    EXPECT_NE(s1, RandomAddressGenerator::PrivateAddress());
  }
}

}  // namespace

}  // namespace gap

}  // namespace bluetooth
