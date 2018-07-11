// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/remote_device_cache.h"

#include "garnet/drivers/bluetooth/lib/common/device_class.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_scanner.h"
#include "lib/gtest/test_loop_fixture.h"
#include "garnet/drivers/bluetooth/lib/sm/types.h"
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
const btlib::sm::LTK kLTK;

class GAP_RemoteDeviceCacheTest : public ::gtest::TestLoopFixture {
 public:
  void SetUp() {}
  void TearDown() { RunLoopUntilIdle(); }
};

TEST_F(GAP_RemoteDeviceCacheTest, LookUp) {
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

TEST_F(GAP_RemoteDeviceCacheTest, TryMakeNonTemporaryNonConn) {
  RemoteDeviceCache cache;
  auto device = cache.NewDevice(kAddrPublic, false);
  EXPECT_TRUE(device->temporary());
  EXPECT_FALSE(device->TryMakeNonTemporary());
  EXPECT_TRUE(device->temporary());
}

TEST_F(GAP_RemoteDeviceCacheTest, TryMakeNonTemporaryRandomAddr) {
  RemoteDeviceCache cache;
  auto device = cache.NewDevice(kAddrRandom, true);
  EXPECT_TRUE(device->temporary());
  EXPECT_FALSE(device->TryMakeNonTemporary());
  EXPECT_TRUE(device->temporary());
}

TEST_F(GAP_RemoteDeviceCacheTest, TryMakeNonTemporaryAnonAddr) {
  RemoteDeviceCache cache;
  auto device = cache.NewDevice(kAddrAnon, true);
  EXPECT_TRUE(device->temporary());
  EXPECT_FALSE(device->TryMakeNonTemporary());
  EXPECT_TRUE(device->temporary());
}

TEST_F(GAP_RemoteDeviceCacheTest, TryMakeNonTemporarySuccess) {
  RemoteDeviceCache cache;
  auto device = cache.NewDevice(kAddrPublic, true);
  EXPECT_TRUE(device->temporary());
  EXPECT_TRUE(device->TryMakeNonTemporary());
  EXPECT_FALSE(device->temporary());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       NewDeviceDoesNotCrashWhenNoCallbackIsReigstered) {
  RemoteDeviceCache().NewDevice(kAddrPublic, true);
}

TEST_F(GAP_RemoteDeviceCacheTest,
       NewDeviceInvokesCallbackWhenDeviceIsFirstRegistered) {
  RemoteDeviceCache cache;
  bool was_called = false;
  cache.set_device_updated_callback(
      [&was_called](const auto&) { was_called = true; });
  cache.NewDevice(kAddrPublic, true);
  EXPECT_TRUE(was_called);
}

TEST_F(GAP_RemoteDeviceCacheTest,
       NewDeviceDoesNotInvokeCallbackWhenDeviceIsReRegistered) {
  RemoteDeviceCache cache;
  int call_count = 0;
  cache.set_device_updated_callback(
      [&call_count](const auto&) { ++call_count; });
  cache.NewDevice(kAddrPublic, true);
  cache.NewDevice(kAddrPublic, true);
  EXPECT_EQ(1, call_count);
}

class GAP_RemoteDeviceCacheTest_BondedCallbackTest : public GAP_RemoteDeviceCacheTest {
 public:
  void SetUp() {
    was_called_ = false;
    device_ = cache_.NewDevice(kAddrPublic, true);
    cache_.set_device_bonded_callback(
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

TEST_F(GAP_RemoteDeviceCacheTest_BondedCallbackTest,
       StoreLTKTriggersBondedCallback) {
  cache()->StoreLTK(device()->identifier(), kLTK);
  EXPECT_TRUE(was_called());
}

class GAP_RemoteDeviceCacheTest_UpdateCallbackTest
    : public GAP_RemoteDeviceCacheTest {
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
  device()->SetLEConnectionState(RemoteDevice::ConnectionState::kConnected);
  EXPECT_TRUE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       ChangingBrEdrConnectionStateTriggersUpdateCallback) {
  device()->SetBREDRConnectionState(RemoteDevice::ConnectionState::kConnected);
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
       AddExistingBondedDeviceFails) {
  auto res = cache()->AddBondedDevice(device()->identifier(),
                                      device()->address(), kLTK);
  EXPECT_FALSE(res);
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

using GAP_RemoteDeviceCacheTest_UpdateCallbackTest_NoConnectablePublicDevice =
    GAP_RemoteDeviceCacheTest;
TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest_NoConnectablePublicDevice,
       TryMakeTemporaryDoesNotTriggerUpdateCallbackOnFailure) {
  RemoteDeviceCache cache;
  RemoteDevice* unconnectable_device = cache.NewDevice(kAddrPublic, false);
  bool was_called = false;
  cache.set_device_updated_callback([&](const auto&) { was_called = true; });
  ASSERT_FALSE(was_called);
  ASSERT_FALSE(unconnectable_device->TryMakeNonTemporary());
  EXPECT_FALSE(was_called);
}

class GAP_RemoteDeviceCacheTest_ExpirationTest
    : public GAP_RemoteDeviceCacheTest {
 public:
  void SetUp() {
    device_ptr_ = cache_.NewDevice(kAddrPublic, true);
    device_id_ = device_ptr_->identifier();
    device_addr_ = device_ptr_->address();
  }

 protected:
  RemoteDeviceCache* cache() { return &cache_; }
  RemoteDevice* device_ptr() { return device_ptr_; }
  std::string device_id() { return device_id_; }
  common::DeviceAddress device_addr() { return device_addr_; }

 private:
  RemoteDeviceCache cache_;
  RemoteDevice* device_ptr_;
  std::string device_id_;
  common::DeviceAddress device_addr_;
};

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       TemporaryDiesSixtySecondsAfterBirth) {
  AdvanceTimeBy(zx::sec(60));
  RunLoopUntilIdle();
  EXPECT_FALSE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       TemporaryLivesForSixtySecondsAfterBirth) {
  AdvanceTimeBy(zx::sec(60) - zx::msec(1));
  RunLoopUntilIdle();
  EXPECT_TRUE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       TemporaryLivesForSixtySecondsSinceLastSeen) {
  AdvanceTimeBy(zx::sec(60) - zx::msec(1));
  RunLoopUntilIdle();
  ASSERT_EQ(device_ptr(), cache()->FindDeviceById(device_id()));

  // Tickle device, and verify it sticks around for another cache timeout.
  device_ptr()->SetName("nombre");
  AdvanceTimeBy(zx::sec(60) - zx::msec(1));
  RunLoopUntilIdle();
  EXPECT_TRUE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       TemporaryDiesSixtySecondsAfterLastSeen) {
  AdvanceTimeBy(zx::sec(60) - zx::msec(1));
  RunLoopUntilIdle();
  ASSERT_EQ(device_ptr(), cache()->FindDeviceById(device_id()));

  // Tickle device, and verify it expires after cache timeout.
  device_ptr()->SetName("nombre");
  AdvanceTimeBy(zx::sec(60));
  RunLoopUntilIdle();
  EXPECT_FALSE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       NonTemporaryLivesMuchMoreThanSixtySeconds) {
  ASSERT_TRUE(device_ptr()->TryMakeNonTemporary());
  AdvanceTimeBy(zx::sec(60) * 10);
  RunLoopUntilIdle();
  EXPECT_TRUE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       CanMakeNonTemporaryJustBeforeSixtySeconds) {
  // At last possible moment, make device non-temporary,
  AdvanceTimeBy(zx::sec(60) - zx::msec(1));
  RunLoopUntilIdle();
  ASSERT_EQ(device_ptr(), cache()->FindDeviceById(device_id()));
  ASSERT_TRUE(device_ptr()->TryMakeNonTemporary());

  // Verify that devices survives.
  AdvanceTimeBy(zx::sec(60) * 10);
  RunLoopUntilIdle();
  EXPECT_TRUE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       LEConnectedDeviceLivesMuchMoreThanSixtySeconds) {
  device_ptr()->SetLEConnectionState(RemoteDevice::ConnectionState::kConnected);
  AdvanceTimeBy(zx::sec(60) * 10);
  RunLoopUntilIdle();
  EXPECT_TRUE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       BREDRConnectedDeviceLivesMuchMoreThanSixtySeconds) {
  device_ptr()->SetBREDRConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  AdvanceTimeBy(zx::sec(60) * 10);
  RunLoopUntilIdle();
  EXPECT_TRUE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       LEDisconnectTriggersExpirationAfterSixtySeconds) {
  device_ptr()->SetLEConnectionState(RemoteDevice::ConnectionState::kConnected);
  AdvanceTimeBy(zx::sec(60) * 10);
  RunLoopUntilIdle();
  ASSERT_TRUE(cache()->FindDeviceById(device_id()));

  device_ptr()->SetLEConnectionState(
      RemoteDevice::ConnectionState::kNotConnected);
  AdvanceTimeBy(zx::sec(60));
  RunLoopUntilIdle();
  EXPECT_FALSE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       BREDRDisconnectTriggersExpirationAfterSixySeconds) {
  device_ptr()->SetBREDRConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  AdvanceTimeBy(zx::sec(60) * 10);
  RunLoopUntilIdle();
  ASSERT_TRUE(cache()->FindDeviceById(device_id()));

  device_ptr()->SetBREDRConnectionState(
      RemoteDevice::ConnectionState::kNotConnected);
  AdvanceTimeBy(zx::sec(60));
  RunLoopUntilIdle();
  EXPECT_FALSE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest, ExpirationUpdatesAddressMap) {
  AdvanceTimeBy(zx::sec(60));
  RunLoopUntilIdle();
  EXPECT_FALSE(cache()->FindDeviceByAddress(device_addr()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       SetLEAdvertisingDataUpdatesExpiration) {
  AdvanceTimeBy(zx::sec(60) - zx::msec(1));
  device_ptr()->SetLEAdvertisingData(kTestRSSI, common::StaticByteBuffer<1>{});
  AdvanceTimeBy(zx::msec(1));
  RunLoopUntilIdle();
  EXPECT_TRUE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       SetInquiryDataFromInquiryResultUpdatesExpiration) {
  hci::InquiryResult ir;
  ir.bd_addr = device_addr().value();
  AdvanceTimeBy(zx::sec(60) - zx::msec(1));
  device_ptr()->SetInquiryData(ir);
  AdvanceTimeBy(zx::msec(1));
  RunLoopUntilIdle();
  EXPECT_TRUE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       SetInquiryDataFromInquiryResultRSSIUpdatesExpiration) {
  hci::InquiryResultRSSI irr;
  irr.bd_addr = device_addr().value();
  AdvanceTimeBy(zx::sec(60) - zx::msec(1));
  device_ptr()->SetInquiryData(irr);
  AdvanceTimeBy(zx::msec(1));
  RunLoopUntilIdle();
  EXPECT_TRUE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       SetInquiryDataFromExtendedInquiryResultEventParamsUpdatesExpiration) {
  hci::ExtendedInquiryResultEventParams eirep;
  eirep.bd_addr = device_addr().value();
  AdvanceTimeBy(zx::sec(60) - zx::msec(1));
  device_ptr()->SetInquiryData(eirep);
  AdvanceTimeBy(zx::msec(1));
  RunLoopUntilIdle();
  EXPECT_TRUE(cache()->FindDeviceById(device_id()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest, SetNameUpdatesExpiration) {
  AdvanceTimeBy(zx::sec(60) - zx::msec(1));
  device_ptr()->SetName({});
  AdvanceTimeBy(zx::msec(1));
  RunLoopUntilIdle();
  EXPECT_TRUE(cache()->FindDeviceById(device_id()));
}

}  // namespace
}  // namespace gap
}  // namespace btlib
