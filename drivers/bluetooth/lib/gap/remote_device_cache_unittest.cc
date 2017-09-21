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

TEST(RemoteDeviceCacheTest, StoreLowEnergyScanResult) {
  const common::DeviceAddress kAddress0(common::DeviceAddress::Type::kLEPublic,
                                        "01:02:03:04:05:06");
  const common::DeviceAddress kAddress1(common::DeviceAddress::Type::kLERandom,
                                        "06:05:04:03:02:01");
  auto kAdvData0 = common::CreateStaticByteBuffer(0x05, 0x09, 'T', 'e', 's', 't');
  auto kAdvData1 = common::CreateStaticByteBuffer(0x0C, 0x09, 'T', 'e', 's', 't', ' ', 'D', 'e',
                                                  'v', 'i', 'c', 'e');

  const hci::LowEnergyScanResult kScanResult0(kAddress0, true, hci::kRSSIInvalid);
  const hci::LowEnergyScanResult kScanResult1(kAddress1, false, hci::kRSSIInvalid);

  RemoteDeviceCache cache;

  EXPECT_FALSE(cache.FindDeviceByAddress(kAddress0));
  EXPECT_FALSE(cache.FindDeviceById("foo"));

  auto device = cache.StoreLowEnergyScanResult(kScanResult0, kAdvData0);

  ASSERT_TRUE(device);
  EXPECT_EQ(TechnologyType::kLowEnergy, device->technology());
  EXPECT_TRUE(device->connectable());
  EXPECT_TRUE(device->temporary());
  EXPECT_EQ(kAddress0, device->address());
  EXPECT_TRUE(common::ContainersEqual(kAdvData0, device->advertising_data()));

  // A look up should return the same instance.
  EXPECT_EQ(device, cache.FindDeviceById(device->identifier()));
  EXPECT_EQ(device, cache.FindDeviceByAddress(device->address()));

  // Adding a device with the same address should return the same instance.
  EXPECT_EQ(device, cache.StoreLowEnergyScanResult(kScanResult0, kAdvData1));
  EXPECT_EQ(TechnologyType::kLowEnergy, device->technology());
  EXPECT_TRUE(device->connectable());
  EXPECT_TRUE(device->temporary());
  EXPECT_EQ(kAddress0, device->address());
  EXPECT_TRUE(common::ContainersEqual(kAdvData1, device->advertising_data()));

  // Setting a shorter advertising data should work without making reallocations.
  cache.StoreLowEnergyScanResult(kScanResult0, kAdvData0);
  EXPECT_TRUE(common::ContainersEqual(kAdvData0, device->advertising_data()));

  // Insert an entry with a different address.
  auto device1 = cache.StoreLowEnergyScanResult(kScanResult1, kAdvData1);
  EXPECT_NE(device1, device);
  EXPECT_EQ(TechnologyType::kLowEnergy, device1->technology());
  EXPECT_FALSE(device1->connectable());
  EXPECT_TRUE(device->temporary());
  EXPECT_EQ(kAddress1, device1->address());
  EXPECT_TRUE(common::ContainersEqual(kAdvData1, device1->advertising_data()));

  EXPECT_EQ(device1, cache.FindDeviceById(device1->identifier()));
}

TEST(RemoteDeviceCacheTest, StoreLowEnergyConnectionNewDevice) {
  const common::DeviceAddress kAddress(common::DeviceAddress::Type::kLEPublic, "01:02:03:04:05:06");
  hci::Connection::LowEnergyParameters params;

  RemoteDeviceCache cache;

  auto device = cache.StoreLowEnergyConnection(kAddress, hci::Connection::LinkType::kLE, params);
  ASSERT_TRUE(device);

  EXPECT_EQ(kAddress, device->address());
  EXPECT_TRUE(device->connectable());
  EXPECT_FALSE(device->temporary());
}

TEST(RemoteDeviceCacheTest, StoreLowEnergyConnectionExisting) {
  const common::DeviceAddress kAddress(common::DeviceAddress::Type::kLEPublic, "01:02:03:04:05:06");
  hci::Connection::LowEnergyParameters params;

  RemoteDeviceCache cache;

  auto device = cache.NewDevice(kAddress, TechnologyType::kLowEnergy, true, true);
  ASSERT_TRUE(device);
  ASSERT_TRUE(device->temporary());

  auto updated = cache.StoreLowEnergyConnection(kAddress, hci::Connection::LinkType::kLE, params);
  ASSERT_TRUE(updated);
  EXPECT_EQ(device, updated);
  EXPECT_FALSE(device->temporary());
}

}  // namespace
}  // namespace gap
}  // namespace bluetooth
