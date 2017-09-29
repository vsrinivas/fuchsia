// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/remote_device_cache.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_scanner.h"
#include "gtest/gtest.h"

namespace bluetooth {
namespace gap {
namespace {

// All fields are initialized to zero as they are unused in these tests.
const hci::LEConnectionParameters kTestParams;

constexpr int8_t kTestRSSI = 10;

const common::DeviceAddress kAddrPublic(common::DeviceAddress::Type::kLEPublic,
                                        "01:02:03:04:05:06");

// TODO(armansito): Make these adhere to privacy specfication.
const common::DeviceAddress kAddrRandom(common::DeviceAddress::Type::kLERandom,
                                        "06:05:04:03:02:01");
const common::DeviceAddress kAddrAnon(common::DeviceAddress::Type::kLEAnonymous,
                                      "06:05:04:03:02:01");

TEST(GAP_RemoteDeviceCacheTest, LookUp) {
  auto kAdvData0 =
      common::CreateStaticByteBuffer(0x05, 0x09, 'T', 'e', 's', 't');
  auto kAdvData1 = common::CreateStaticByteBuffer(
      0x0C, 0x09, 'T', 'e', 's', 't', ' ', 'D', 'e', 'v', 'i', 'c', 'e');

  RemoteDeviceCache cache;

  EXPECT_FALSE(cache.FindDeviceByAddress(kAddrPublic));
  EXPECT_FALSE(cache.FindDeviceById("foo"));

  auto device = cache.NewDevice(kAddrPublic, true);
  ASSERT_TRUE(device);
  EXPECT_EQ(TechnologyType::kLowEnergy, device->technology());
  EXPECT_TRUE(device->connectable());
  EXPECT_TRUE(device->temporary());
  EXPECT_EQ(kAddrPublic, device->address());
  EXPECT_EQ(0u, device->advertising_data().size());
  EXPECT_EQ(hci::kRSSIInvalid, device->rssi());

  // A look up should return the same instance.
  EXPECT_EQ(device, cache.FindDeviceById(device->identifier()));
  EXPECT_EQ(device, cache.FindDeviceByAddress(device->address()));

  // Adding a device with the same address should return nullptr.
  EXPECT_FALSE(cache.NewDevice(kAddrPublic, true));

  device->SetLEAdvertisingData(kTestRSSI, kAdvData1);
  EXPECT_TRUE(common::ContainersEqual(kAdvData1, device->advertising_data()));
  EXPECT_EQ(kTestRSSI, device->rssi());

  device->SetLEAdvertisingData(kTestRSSI, kAdvData0);
  EXPECT_TRUE(common::ContainersEqual(kAdvData0, device->advertising_data()));
  EXPECT_EQ(kTestRSSI, device->rssi());
}

TEST(GAP_RemoteDeviceCacheTest, TryMakeNonTemporaryNonConn) {
  RemoteDeviceCache cache;
  auto device = cache.NewDevice(kAddrPublic, false);
  EXPECT_TRUE(device->temporary());
  EXPECT_FALSE(device->TryMakeNonTemporary());
  EXPECT_TRUE(device->temporary());
}

TEST(GAP_RemoteDeviceCacheTest, TryMakeNonTemporaryRandomAddr) {
  RemoteDeviceCache cache;
  auto device = cache.NewDevice(kAddrRandom, true);
  EXPECT_TRUE(device->temporary());
  EXPECT_FALSE(device->TryMakeNonTemporary());
  EXPECT_TRUE(device->temporary());
}

TEST(GAP_RemoteDeviceCacheTest, TryMakeNonTemporaryAnonAddr) {
  RemoteDeviceCache cache;
  auto device = cache.NewDevice(kAddrAnon, true);
  EXPECT_TRUE(device->temporary());
  EXPECT_FALSE(device->TryMakeNonTemporary());
  EXPECT_TRUE(device->temporary());
}

TEST(GAP_RemoteDeviceCacheTest, TryMakeNonTemporarySuccess) {
  RemoteDeviceCache cache;
  auto device = cache.NewDevice(kAddrPublic, true);
  EXPECT_TRUE(device->temporary());
  EXPECT_TRUE(device->TryMakeNonTemporary());
  EXPECT_FALSE(device->temporary());
}

}  // namespace
}  // namespace gap
}  // namespace bluetooth
