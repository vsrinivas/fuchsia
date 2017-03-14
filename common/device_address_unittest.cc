// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/common/device_address.h"

#include "gtest/gtest.h"

namespace bluetooth {
namespace common {
namespace {

struct TestPayload {
  uint8_t arg0;
  DeviceAddress bdaddr;
} __attribute__((packed));

TEST(DeviceAddressTest, ToString) {
  DeviceAddress bdaddr({1, 15, 2, 255, 127, 3});
  EXPECT_EQ("03:7F:FF:02:0F:01", bdaddr.ToString());

  bdaddr = DeviceAddress();
  EXPECT_EQ("00:00:00:00:00:00", bdaddr.ToString());
}

TEST(DeviceAddressTest, SetFromString) {
  DeviceAddress bdaddr;
  EXPECT_FALSE(bdaddr.SetFromString(""));
  EXPECT_FALSE(bdaddr.SetFromString("FF"));
  EXPECT_FALSE(bdaddr.SetFromString("FF:FF:FF:FF:"));
  EXPECT_FALSE(bdaddr.SetFromString("FF:FF:FF:FF:FF:F"));
  EXPECT_FALSE(bdaddr.SetFromString("FF:FF:FF:FF:FF:FZ"));
  EXPECT_FALSE(bdaddr.SetFromString("FF:FF:FF:FF:FF:FZ"));
  EXPECT_FALSE(bdaddr.SetFromString("FF:FF:FF:FF:FF:FF "));
  EXPECT_FALSE(bdaddr.SetFromString(" FF:FF:FF:FF:FF:FF"));

  EXPECT_TRUE(bdaddr.SetFromString("FF:FF:FF:FF:FF:FF"));
  EXPECT_EQ("FF:FF:FF:FF:FF:FF", bdaddr.ToString());

  EXPECT_TRUE(bdaddr.SetFromString("03:7F:FF:02:0F:01"));
  EXPECT_EQ("03:7F:FF:02:0F:01", bdaddr.ToString());

  // Test the constructor with a valid string (an invalid one would fail fatally).
  bdaddr = DeviceAddress("03:7F:FF:02:0F:01");
  EXPECT_EQ("03:7F:FF:02:0F:01", bdaddr.ToString());
}

TEST(DeviceAddressTest, CastFromBytes) {
  std::array<uint8_t, 7> bytes{{10, 1, 15, 2, 255, 127, 3}};
  EXPECT_EQ(bytes.size(), sizeof(TestPayload));

  auto* bdaddr = reinterpret_cast<DeviceAddress*>(bytes.data());
  EXPECT_EQ("7F:FF:02:0F:01:0A", bdaddr->ToString());

  auto* test_payload = reinterpret_cast<TestPayload*>(bytes.data());
  EXPECT_EQ(10, test_payload->arg0);
  EXPECT_EQ("03:7F:FF:02:0F:01", test_payload->bdaddr.ToString());
}

TEST(DeviceAddressTest, Comparison) {
  DeviceAddress bdaddr0, bdaddr1;
  EXPECT_EQ(bdaddr0, bdaddr1);

  bdaddr0 = DeviceAddress({1, 2, 3, 4, 5, 6});
  EXPECT_NE(bdaddr0, bdaddr1);

  bdaddr1 = bdaddr0;
  EXPECT_EQ(bdaddr0, bdaddr1);
}

}  // namespace
}  // namespace common
}  // namespace bluetooth
