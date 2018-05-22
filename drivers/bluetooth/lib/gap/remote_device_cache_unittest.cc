// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/remote_device_cache.h"

#include "garnet/drivers/bluetooth/lib/common/device_class.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_scanner.h"
#include "gtest/gtest.h"

namespace btlib {
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

TEST(GAP_RemoteDeviceCacheTest,
     NewDeviceDoesNotCrashWhenNoCallbackIsReigstered) {
  RemoteDeviceCache().NewDevice(kAddrPublic, true);
}

TEST(GAP_RemoteDeviceCacheTest,
     NewDeviceInvokesCallbackWhenDeviceIsFirstRegistered) {
  RemoteDeviceCache cache;
  bool was_called = false;
  cache.set_device_updated_callback(
      [&was_called](const auto&) { was_called = true; });
  cache.NewDevice(kAddrPublic, true);
  EXPECT_TRUE(was_called);
}

TEST(GAP_RemoteDeviceCacheTest,
     NewDeviceDoesNotInvokeCallbackWhenDeviceIsReRegistered) {
  RemoteDeviceCache cache;
  int call_count = 0;
  cache.set_device_updated_callback(
      [&call_count](const auto&) { ++call_count; });
  cache.NewDevice(kAddrPublic, true);
  cache.NewDevice(kAddrPublic, true);
  EXPECT_EQ(1, call_count);
}

class GAP_RemoteDeviceCacheTest_UpdateCallbackTest : public ::testing::Test {
 public:
  void SetUp() {
    was_called_ = false;
    device_ = cache_.NewDevice(kAddrPublic, true);
    cache_.set_device_updated_callback(
        [this](const auto&) { was_called_ = true; });
    EXPECT_FALSE(was_called_);
  }

 protected:
  RemoteDeviceCache* cache() { return &cache_; }
  RemoteDevice* device() { return device_; }
  bool was_called() const { return was_called_; }

 private:
  RemoteDeviceCache cache_;
  RemoteDevice* device_;
  bool was_called_;
};

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       ChangingLEConnectionStateTriggersUpdateCallback) {
  device()->set_le_connection_state(RemoteDevice::ConnectionState::kConnected);
  EXPECT_TRUE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       ChangingBrEdrConnectionStateTriggersUpdateCallback) {
  device()->set_bredr_connection_state(
      RemoteDevice::ConnectionState::kConnected);
  EXPECT_TRUE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetLEAdvertisingDataTriggersUpdateCallbackOnNameSet) {
  device()->SetLEAdvertisingData(
      kTestRSSI,
      common::CreateStaticByteBuffer(0x05,  // Length
                                     0x09,  // AD Type: Complete Local Name
                                     'T', 'e', 's', 't'));
  EXPECT_TRUE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetLEAdvertisingDataUpdateCallbackProvidesUpdatedDevice) {
  const auto kAdvData =
      common::CreateStaticByteBuffer(0x05, 0x09, 'T', 'e', 's', 't');
  ASSERT_NE(device()->rssi(), kTestRSSI);
  cache()->set_device_updated_callback([&](const auto& updated_dev) {
    EXPECT_TRUE(
        common::ContainersEqual(kAdvData, updated_dev.advertising_data()));
    EXPECT_EQ(updated_dev.rssi(), kTestRSSI);
  });
  device()->SetLEAdvertisingData(
      kTestRSSI,
      common::CreateStaticByteBuffer(0x05,  // Length
                                     0x09,  // AD Type: Complete Local Name
                                     'T', 'e', 's', 't'));
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetLEAdvertisingDataDoesNotTriggerUpdateCallbackOnSameName) {
  device()->SetLEAdvertisingData(
      kTestRSSI,
      common::CreateStaticByteBuffer(0x05,  // Length
                                     0x09,  // AD Type: Complete Local Name
                                     'T', 'e', 's', 't'));
  ASSERT_TRUE(was_called());

  bool was_called_again = false;
  cache()->set_device_updated_callback(
      [&](const auto&) { was_called_again = true; });
  device()->SetLEAdvertisingData(
      kTestRSSI,
      common::CreateStaticByteBuffer(0x05,  // Length
                                     0x09,  // AD Type: Complete Local Name
                                     'T', 'e', 's', 't'));
  EXPECT_FALSE(was_called_again);
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetInquiryDataTriggersUpdateCallbackOnDeviceClassSet) {
  hci::InquiryResult ir;
  ir.bd_addr = device()->address().value();
  ir.class_of_device = common::DeviceClass({0x06, 0x02, 0x02});  // Phone.
  device()->SetInquiryData(ir);
  EXPECT_TRUE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetInquiryDataUpdateCallbackProvidesUpdatedDevice) {
  hci::InquiryResult ir;
  ir.bd_addr = device()->address().value();
  ir.class_of_device = common::DeviceClass({0x06, 0x02, 0x02});  // Phone.
  ASSERT_FALSE(device()->device_class().HasValue());
  cache()->set_device_updated_callback([](const auto& updated_dev) {
    ASSERT_TRUE(updated_dev.device_class().HasValue());
    EXPECT_EQ(updated_dev.device_class()->major_class(),
              common::DeviceClass::MajorClass(0x02));
  });
  device()->SetInquiryData(ir);
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetInquiryDataDoesNotTriggerUpdateCallbackOnSameDeviceClass) {
  hci::InquiryResult ir;
  ir.bd_addr = device()->address().value();
  ir.class_of_device = common::DeviceClass({0x06, 0x02, 0x02});  // Phone.
  device()->SetInquiryData(ir);
  ASSERT_TRUE(was_called());

  bool was_called_again = false;
  cache()->set_device_updated_callback(
      [&](const auto&) { was_called_again = true; });
  device()->SetInquiryData(ir);
  EXPECT_FALSE(was_called_again);
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetNameTriggersUpdateCallback) {
  device()->SetName("nombre");
  EXPECT_TRUE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetLEConnectionParamsDoesNotTriggerUpdateCallback) {
  device()->set_le_connection_params({});
  EXPECT_FALSE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetLEPreferredConnectionParamsDoesNotTriggerUpdateCallback) {
  device()->set_le_preferred_connection_params({});
  EXPECT_FALSE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       TryMakeTemporaryTriggersUpdateCallbackOnSuccessfulChange) {
  ASSERT_TRUE(device()->TryMakeNonTemporary());
  EXPECT_TRUE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       TryMakeTemporaryDoesNotTriggerUpdateCallbackWhenAlreadyTemporary) {
  ASSERT_TRUE(device()->TryMakeNonTemporary());
  ASSERT_TRUE(was_called());

  bool was_called_again = false;
  cache()->set_device_updated_callback(
      [&](const auto&) { was_called_again = true; });
  ASSERT_TRUE(device()->TryMakeNonTemporary());
  EXPECT_FALSE(was_called_again);
}

TEST(GAP_RemoteDeviceCacheTest_UpdateCallbackTest_WithoutFixture,
     TryMakeTemporaryDoesNotTriggerUpdateCallbackOnFailure) {
  // WithoutFixture to avoid automatic NewDevice(kAddrPublic, true).
  RemoteDeviceCache cache;
  RemoteDevice* unconnectable_device = cache.NewDevice(kAddrPublic, false);
  bool was_called = false;
  cache.set_device_updated_callback([&](const auto&) { was_called = true; });
  ASSERT_FALSE(was_called);
  ASSERT_FALSE(unconnectable_device->TryMakeNonTemporary());
  EXPECT_FALSE(was_called);
}

}  // namespace
}  // namespace gap
}  // namespace btlib
