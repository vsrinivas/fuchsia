// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/remote_device_cache.h"

#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"

#include "garnet/drivers/bluetooth/lib/common/device_class.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/common/uint128.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_scanner.h"
#include "garnet/drivers/bluetooth/lib/sm/types.h"
#include "garnet/drivers/bluetooth/lib/sm/util.h"

namespace btlib {
namespace gap {
namespace {

using common::CreateStaticByteBuffer;
using common::DeviceAddress;
using common::MutableBufferView;
using common::StaticByteBuffer;

// All fields are initialized to zero as they are unused in these tests.
const hci::LEConnectionParameters kTestParams;

constexpr int8_t kTestRSSI = 10;

const DeviceAddress kAddrBrEdr(DeviceAddress::Type::kBREDR,
                               "AA:BB:CC:DD:EE:FF");
const DeviceAddress kAddrLePublic(DeviceAddress::Type::kLEPublic,
                                  "01:02:03:04:05:06");

// TODO(armansito): Make these adhere to privacy specification.
const DeviceAddress kAddrLeRandom(DeviceAddress::Type::kLERandom,
                                  "06:05:04:03:02:01");
const DeviceAddress kAddrLeRandom2(DeviceAddress::Type::kLERandom,
                                   "FF:EE:DD:CC:BB:AA");
const DeviceAddress kAddrLeAnon(DeviceAddress::Type::kLEAnonymous,
                                "06:05:04:03:02:01");

const auto kAdvData =
    CreateStaticByteBuffer(0x05,  // Length
                           0x09,  // AD type: Complete Local Name
                           'T', 'e', 's', 't');
const auto kEirData = kAdvData;

const btlib::sm::LTK kLTK;
const btlib::sm::Key kKey{};

class GAP_RemoteDeviceCacheTest : public ::gtest::TestLoopFixture {
 public:
  void SetUp() {}
  void TearDown() { RunLoopUntilIdle(); }

 protected:
  bool NewDevice(const DeviceAddress& addr, bool connectable) {
    auto* dev = cache_.NewDevice(addr, connectable);
    if (!dev) {
      return false;
    }
    device_ = dev;
    return true;
  }

  RemoteDeviceCache* cache() { return &cache_; }
  RemoteDevice* device() { return device_; }

 private:
  RemoteDeviceCache cache_;
  RemoteDevice* device_;
};

TEST_F(GAP_RemoteDeviceCacheTest, LookUp) {
  auto kAdvData0 = CreateStaticByteBuffer(0x05, 0x09, 'T', 'e', 's', 't');
  auto kAdvData1 = CreateStaticByteBuffer(0x0C, 0x09, 'T', 'e', 's', 't', ' ',
                                          'D', 'e', 'v', 'i', 'c', 'e');

  EXPECT_FALSE(cache()->FindDeviceByAddress(kAddrLePublic));
  EXPECT_FALSE(cache()->FindDeviceById("foo"));

  auto device = cache()->NewDevice(kAddrLePublic, true);
  ASSERT_TRUE(device);
  ASSERT_TRUE(device->le());
  EXPECT_EQ(TechnologyType::kLowEnergy, device->technology());
  EXPECT_TRUE(device->connectable());
  EXPECT_TRUE(device->temporary());
  EXPECT_EQ(kAddrLePublic, device->address());
  EXPECT_EQ(0u, device->le()->advertising_data().size());
  EXPECT_EQ(hci::kRSSIInvalid, device->rssi());

  // A look up should return the same instance.
  EXPECT_EQ(device, cache()->FindDeviceById(device->identifier()));
  EXPECT_EQ(device, cache()->FindDeviceByAddress(device->address()));

  // Adding a device with the same address should return nullptr.
  EXPECT_FALSE(cache()->NewDevice(kAddrLePublic, true));

  device->MutLe().SetAdvertisingData(kTestRSSI, kAdvData1);
  EXPECT_TRUE(
      common::ContainersEqual(kAdvData1, device->le()->advertising_data()));
  EXPECT_EQ(kTestRSSI, device->rssi());

  device->MutLe().SetAdvertisingData(kTestRSSI, kAdvData0);
  EXPECT_TRUE(
      common::ContainersEqual(kAdvData0, device->le()->advertising_data()));
  EXPECT_EQ(kTestRSSI, device->rssi());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       NewDeviceDoesNotCrashWhenNoCallbackIsRegistered) {
  RemoteDeviceCache().NewDevice(kAddrLePublic, true);
}

TEST_F(GAP_RemoteDeviceCacheTest, ForEachEmpty) {
  bool found = false;
  cache()->ForEach([&](const auto&) { found = true; });
  EXPECT_FALSE(found);
}

TEST_F(GAP_RemoteDeviceCacheTest, ForEach) {
  int count = 0;
  NewDevice(kAddrLePublic, true);
  cache()->ForEach([&](const auto& dev) {
    count++;
    EXPECT_EQ(device()->identifier(), dev.identifier());
    EXPECT_EQ(device()->address(), dev.address());
  });
  EXPECT_EQ(1, count);
}

TEST_F(GAP_RemoteDeviceCacheTest,
       NewDeviceInvokesCallbackWhenDeviceIsFirstRegistered) {
  bool was_called = false;
  cache()->set_device_updated_callback(
      [&was_called](const auto&) { was_called = true; });
  cache()->NewDevice(kAddrLePublic, true);
  EXPECT_TRUE(was_called);
}

TEST_F(GAP_RemoteDeviceCacheTest,
       NewDeviceDoesNotInvokeCallbackWhenDeviceIsReRegistered) {
  int call_count = 0;
  cache()->set_device_updated_callback(
      [&call_count](const auto&) { ++call_count; });
  cache()->NewDevice(kAddrLePublic, true);
  cache()->NewDevice(kAddrLePublic, true);
  EXPECT_EQ(1, call_count);
}

TEST_F(GAP_RemoteDeviceCacheTest, NewDeviceIdentityKnown) {
  EXPECT_TRUE(cache()->NewDevice(kAddrBrEdr, true)->identity_known());
  EXPECT_TRUE(cache()->NewDevice(kAddrLePublic, true)->identity_known());
  EXPECT_FALSE(cache()->NewDevice(kAddrLeRandom, true)->identity_known());
  EXPECT_FALSE(cache()->NewDevice(kAddrLeAnon, false)->identity_known());
}

TEST_F(GAP_RemoteDeviceCacheTest, NewDeviceInitialTechnologyIsClassic) {
  NewDevice(kAddrBrEdr, true);

  // A device initialized with a BR/EDR address should start out as a
  // classic-only.
  ASSERT_TRUE(device());
  EXPECT_TRUE(device()->bredr());
  EXPECT_FALSE(device()->le());
  EXPECT_TRUE(device()->identity_known());
  EXPECT_EQ(TechnologyType::kClassic, device()->technology());
}

TEST_F(GAP_RemoteDeviceCacheTest, NewDeviceInitialTechnologyLowEnergy) {
  // LE address types should initialize the device as LE-only.
  auto* le_publ_dev = cache()->NewDevice(kAddrLePublic, true /*connectable*/);
  auto* le_rand_dev = cache()->NewDevice(kAddrLeRandom, true /*connectable*/);
  auto* le_anon_dev = cache()->NewDevice(kAddrLeAnon, false /*connectable*/);
  ASSERT_TRUE(le_publ_dev);
  ASSERT_TRUE(le_rand_dev);
  ASSERT_TRUE(le_anon_dev);
  EXPECT_TRUE(le_publ_dev->le());
  EXPECT_TRUE(le_rand_dev->le());
  EXPECT_TRUE(le_anon_dev->le());
  EXPECT_FALSE(le_publ_dev->bredr());
  EXPECT_FALSE(le_rand_dev->bredr());
  EXPECT_FALSE(le_anon_dev->bredr());
  EXPECT_EQ(TechnologyType::kLowEnergy, le_publ_dev->technology());
  EXPECT_EQ(TechnologyType::kLowEnergy, le_rand_dev->technology());
  EXPECT_EQ(TechnologyType::kLowEnergy, le_anon_dev->technology());
  EXPECT_TRUE(le_publ_dev->identity_known());
  EXPECT_FALSE(le_rand_dev->identity_known());
  EXPECT_FALSE(le_anon_dev->identity_known());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       ClassicDeviceBecomesDualModeWithAdvertisingData) {
  NewDevice(kAddrBrEdr, true);
  ASSERT_TRUE(device());
  ASSERT_TRUE(device()->bredr());
  ASSERT_FALSE(device()->le());

  device()->MutLe().SetAdvertisingData(kTestRSSI, kAdvData);
  EXPECT_TRUE(device()->le());
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       ClassicDeviceBecomesDualModeWhenConnectedOverLowEnergy) {
  NewDevice(kAddrBrEdr, true);
  ASSERT_TRUE(device());
  ASSERT_TRUE(device()->bredr());
  ASSERT_FALSE(device()->le());

  device()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  EXPECT_TRUE(device()->le());
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       ClassicDeviceBecomesDualModeWithLowEnergyConnParams) {
  NewDevice(kAddrBrEdr, true);
  ASSERT_TRUE(device());
  ASSERT_TRUE(device()->bredr());
  ASSERT_FALSE(device()->le());

  device()->MutLe().SetConnectionParameters({});
  EXPECT_TRUE(device()->le());
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       ClassicDeviceBecomesDualModeWithLowEnergyPreferredConnParams) {
  NewDevice(kAddrBrEdr, true);
  ASSERT_TRUE(device());
  ASSERT_TRUE(device()->bredr());
  ASSERT_FALSE(device()->le());

  device()->MutLe().SetPreferredConnectionParameters({});
  EXPECT_TRUE(device()->le());
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       LowEnergyDeviceBecomesDualModeWithInquiryData) {
  NewDevice(kAddrLePublic, true);
  ASSERT_TRUE(device());
  ASSERT_TRUE(device()->le());
  ASSERT_FALSE(device()->bredr());

  hci::InquiryResult ir;
  ir.bd_addr = kAddrLePublic.value();
  device()->MutBrEdr().SetInquiryData(ir);
  EXPECT_TRUE(device()->bredr());
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());
}

TEST_F(GAP_RemoteDeviceCacheTest,
       LowEnergyDeviceBecomesDualModeWhenConnectedOverClassic) {
  NewDevice(kAddrLePublic, true);
  ASSERT_TRUE(device());
  ASSERT_TRUE(device()->le());
  ASSERT_FALSE(device()->bredr());

  device()->MutBrEdr().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  EXPECT_TRUE(device()->bredr());
  EXPECT_EQ(TechnologyType::kDualMode, device()->technology());
}

class GAP_RemoteDeviceCacheTest_BondingTest : public GAP_RemoteDeviceCacheTest {
 public:
  void SetUp() {
    was_called_ = false;
    NewDevice(kAddrLePublic, true);
    cache()->set_device_bonded_callback(
        [this](const auto&) { was_called_ = true; });
    EXPECT_FALSE(was_called_);
  }

 protected:
  bool bonded_callback_called() const { return was_called_; }

 private:
  bool was_called_;
};

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       AddBondedDeviceFailsWithExistingId) {
  sm::PairingData data;
  data.ltk = kLTK;
  EXPECT_FALSE(
      cache()->AddBondedDevice(device()->identifier(), kAddrLePublic, data));
  EXPECT_FALSE(bonded_callback_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       AddBondedDeviceFailsWithExistingAddress) {
  sm::PairingData data;
  data.ltk = kLTK;
  EXPECT_FALSE(cache()->AddBondedDevice("foo", device()->address(), data));
  EXPECT_FALSE(bonded_callback_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       AddBondedDeviceFailsWithoutMandatoryKeys) {
  sm::PairingData data;
  EXPECT_FALSE(cache()->AddBondedDevice("foo", kAddrLePublic, data));
  EXPECT_FALSE(bonded_callback_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest, AddBondedDeviceSuccess) {
  const std::string kId("test-id");
  sm::PairingData data;
  data.ltk = kLTK;

  EXPECT_TRUE(cache()->AddBondedDevice(kId, kAddrLeRandom, data));
  auto* dev = cache()->FindDeviceById(kId);
  ASSERT_TRUE(dev);
  EXPECT_EQ(dev, cache()->FindDeviceByAddress(kAddrLeRandom));
  EXPECT_EQ(kId, dev->identifier());
  EXPECT_EQ(kAddrLeRandom, dev->address());
  EXPECT_TRUE(dev->identity_known());
  ASSERT_TRUE(dev->le());
  EXPECT_TRUE(dev->le()->bonded());
  ASSERT_TRUE(dev->le()->bond_data());
  EXPECT_EQ(data, *dev->le()->bond_data());

  // The "new bond" callback should be called when restoring a previously bonded
  // device.
  EXPECT_FALSE(bonded_callback_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       AddBondedDeviceWithIrkIsAddedToResolvingList) {
  const std::string kId("test-id");
  sm::PairingData data;
  data.ltk = kLTK;
  data.irk = sm::Key(sm::SecurityProperties(), common::RandomUInt128());

  EXPECT_TRUE(cache()->AddBondedDevice(kId, kAddrLeRandom, data));
  auto* dev = cache()->FindDeviceByAddress(kAddrLeRandom);
  ASSERT_TRUE(dev);
  EXPECT_EQ(kAddrLeRandom, dev->address());

  // Looking up the device by RPA generated using the IRK should return the same
  // device.
  DeviceAddress rpa = sm::util::GenerateRpa(data.irk->value());
  EXPECT_EQ(dev, cache()->FindDeviceByAddress(rpa));
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       StoreLowEnergyBondFailsWithNoKeys) {
  sm::PairingData data;
  EXPECT_FALSE(cache()->StoreLowEnergyBond(device()->identifier(), data));
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest, StoreLowEnergyBondDeviceUnknown) {
  sm::PairingData data;
  data.ltk = kLTK;
  EXPECT_FALSE(cache()->StoreLowEnergyBond("foo", data));
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest, StoreLowEnergyBondWithLtk) {
  ASSERT_TRUE(device()->temporary());
  ASSERT_TRUE(device()->le());
  ASSERT_FALSE(device()->le()->bonded());

  sm::PairingData data;
  data.ltk = kLTK;
  EXPECT_TRUE(cache()->StoreLowEnergyBond(device()->identifier(), data));

  EXPECT_TRUE(bonded_callback_called());
  EXPECT_FALSE(device()->temporary());
  EXPECT_TRUE(device()->le()->bonded());
  EXPECT_TRUE(device()->le()->bond_data());
  EXPECT_EQ(data, *device()->le()->bond_data().value());
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest, StoreLowEnergyBondWithCsrk) {
  ASSERT_TRUE(device()->temporary());
  ASSERT_TRUE(device()->le());
  ASSERT_FALSE(device()->le()->bonded());

  sm::PairingData data;
  data.csrk = kKey;
  EXPECT_TRUE(cache()->StoreLowEnergyBond(device()->identifier(), data));

  EXPECT_TRUE(bonded_callback_called());
  EXPECT_FALSE(device()->temporary());
  EXPECT_TRUE(device()->le()->bonded());
  EXPECT_TRUE(device()->le()->bond_data());
  EXPECT_EQ(data, *device()->le()->bond_data().value());
}

// StoreLowEnergyBond fails if it contains the address of a different,
// previously known device.
TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       StoreLowEnergyBondWithExistingDifferentIdentity) {
  auto* dev = cache()->NewDevice(kAddrLeRandom, true);

  // Assign the other device's address as identity.
  sm::PairingData data;
  data.ltk = kLTK;
  data.identity_address = device()->address();
  EXPECT_FALSE(cache()->StoreLowEnergyBond(dev->identifier(), data));
  EXPECT_FALSE(dev->le()->bonded());
  EXPECT_TRUE(dev->temporary());
}

// StoreLowEnergyBond success if it contains an identity address that already
// matches the target device.
TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       StoreLowEnergyBondWithExistingMatchingIdentity) {
  sm::PairingData data;
  data.ltk = kLTK;
  data.identity_address = device()->address();
  EXPECT_TRUE(cache()->StoreLowEnergyBond(device()->identifier(), data));
  EXPECT_TRUE(device()->le()->bonded());
  EXPECT_EQ(device(), cache()->FindDeviceByAddress(*data.identity_address));
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       StoreLowEnergyBondWithNewIdentity) {
  ASSERT_TRUE(NewDevice(kAddrLeRandom, true));
  ASSERT_FALSE(device()->identity_known());

  sm::PairingData data;
  data.ltk = kLTK;
  data.identity_address = kAddrLeRandom2;  // assign a new identity address
  const auto old_address = device()->address();
  ASSERT_EQ(device(), cache()->FindDeviceByAddress(old_address));
  ASSERT_EQ(nullptr, cache()->FindDeviceByAddress(*data.identity_address));

  EXPECT_TRUE(cache()->StoreLowEnergyBond(device()->identifier(), data));
  EXPECT_TRUE(device()->le()->bonded());

  // Address should have been updated.
  ASSERT_NE(*data.identity_address, old_address);
  EXPECT_EQ(*data.identity_address, device()->address());
  EXPECT_TRUE(device()->identity_known());
  EXPECT_EQ(device(), cache()->FindDeviceByAddress(*data.identity_address));

  // The old address should still map to |dev|.
  ASSERT_EQ(device(), cache()->FindDeviceByAddress(old_address));
}

TEST_F(GAP_RemoteDeviceCacheTest_BondingTest,
       StoreLowEnergyBondWithIrkIsAddedToResolvingList) {
  ASSERT_TRUE(NewDevice(kAddrLeRandom, true));
  ASSERT_FALSE(device()->identity_known());

  sm::PairingData data;
  data.ltk = kLTK;
  data.identity_address = kAddrLeRandom;
  data.irk = sm::Key(sm::SecurityProperties(), common::RandomUInt128());

  EXPECT_TRUE(cache()->StoreLowEnergyBond(device()->identifier(), data));
  ASSERT_TRUE(device()->le()->bonded());
  ASSERT_TRUE(device()->identity_known());

  // Looking up the device by RPA generated using the IRK should return the same
  // device.
  DeviceAddress rpa = sm::util::GenerateRpa(data.irk->value());
  EXPECT_EQ(device(), cache()->FindDeviceByAddress(rpa));
}

class GAP_RemoteDeviceCacheTest_UpdateCallbackTest
    : public GAP_RemoteDeviceCacheTest {
 public:
  void SetUp() {
    was_called_ = false;
    NewDevice(kAddrLePublic, true);
    cache()->set_device_updated_callback(
        [this](const auto&) { was_called_ = true; });
    ir_.bd_addr = device()->address().value();
    irr_.bd_addr = device()->address().value();
    eirep_.bd_addr = device()->address().value();
    eir_data().SetToZeros();
    EXPECT_FALSE(was_called_);
  }

 protected:
  hci::InquiryResult& ir() { return ir_; }
  hci::InquiryResultRSSI& irr() { return irr_; }
  hci::ExtendedInquiryResultEventParams& eirep() { return eirep_; }

  MutableBufferView eir_data() {
    return MutableBufferView(&eirep_.extended_inquiry_response,
                             sizeof(eirep_.extended_inquiry_response));
  }
  bool was_called() const { return was_called_; }
  void ClearWasCalled() { was_called_ = false; }

 private:
  bool was_called_;
  hci::InquiryResult ir_;
  hci::InquiryResultRSSI irr_;
  hci::ExtendedInquiryResultEventParams eirep_;
};

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       ChangingLEConnectionStateTriggersUpdateCallback) {
  device()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  EXPECT_TRUE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       ChangingBrEdrConnectionStateTriggersUpdateCallback) {
  device()->MutBrEdr().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  EXPECT_TRUE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetAdvertisingDataTriggersUpdateCallbackOnNameSet) {
  device()->MutLe().SetAdvertisingData(kTestRSSI, kAdvData);
  EXPECT_TRUE(was_called());
  ASSERT_TRUE(device()->name());
  EXPECT_EQ("Test", *device()->name());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetLowEnergyAdvertisingDataUpdateCallbackProvidesUpdatedDevice) {
  ASSERT_NE(device()->rssi(), kTestRSSI);
  cache()->set_device_updated_callback([&](const auto& updated_dev) {
    ASSERT_TRUE(updated_dev.le());
    EXPECT_TRUE(common::ContainersEqual(kAdvData,
                                        updated_dev.le()->advertising_data()));
    EXPECT_EQ(updated_dev.rssi(), kTestRSSI);
  });
  device()->MutLe().SetAdvertisingData(kTestRSSI, kAdvData);
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetAdvertisingDataDoesNotTriggerUpdateCallbackOnSameName) {
  device()->MutLe().SetAdvertisingData(kTestRSSI, kAdvData);
  ASSERT_TRUE(was_called());

  ClearWasCalled();
  device()->MutLe().SetAdvertisingData(kTestRSSI, kAdvData);
  EXPECT_FALSE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultTriggersUpdateCallbackOnDeviceClassSet) {
  ir().class_of_device = common::DeviceClass({0x06, 0x02, 0x02});  // Phone.
  device()->MutBrEdr().SetInquiryData(ir());
  EXPECT_TRUE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultUpdateCallbackProvidesUpdatedDevice) {
  ir().class_of_device = common::DeviceClass({0x06, 0x02, 0x02});  // Phone.
  ASSERT_FALSE(device()->bredr());
  cache()->set_device_updated_callback([](const auto& updated_dev) {
    ASSERT_TRUE(updated_dev.bredr());
    ASSERT_TRUE(updated_dev.bredr()->device_class());
    EXPECT_EQ(updated_dev.bredr()->device_class()->major_class(),
              common::DeviceClass::MajorClass(0x02));
  });
  device()->MutBrEdr().SetInquiryData(ir());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultDoesNotTriggerUpdateCallbackOnSameDeviceClass) {
  ir().class_of_device = common::DeviceClass({0x06, 0x02, 0x02});  // Phone.
  device()->MutBrEdr().SetInquiryData(ir());
  ASSERT_TRUE(was_called());

  ClearWasCalled();
  device()->MutBrEdr().SetInquiryData(ir());
  EXPECT_FALSE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultRSSITriggersUpdateCallbackOnDeviceClassSet) {
  irr().class_of_device = common::DeviceClass({0x06, 0x02, 0x02});  // Phone.
  device()->MutBrEdr().SetInquiryData(irr());
  EXPECT_TRUE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultRSSIUpdateCallbackProvidesUpdatedDevice) {
  irr().class_of_device = common::DeviceClass({0x06, 0x02, 0x02});  // Phone.
  ASSERT_FALSE(device()->bredr());
  cache()->set_device_updated_callback([](const auto& updated_dev) {
    ASSERT_TRUE(updated_dev.bredr()->device_class());
    EXPECT_EQ(updated_dev.bredr()->device_class()->major_class(),
              common::DeviceClass::MajorClass(0x02));
  });
  device()->MutBrEdr().SetInquiryData(irr());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultRSSIDoesNotTriggerUpdateCallbackOnSameDeviceClass) {
  irr().class_of_device = common::DeviceClass({0x06, 0x02, 0x02});  // Phone.
  device()->MutBrEdr().SetInquiryData(irr());
  ASSERT_TRUE(was_called());

  ClearWasCalled();
  device()->MutBrEdr().SetInquiryData(irr());
  EXPECT_FALSE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromInquiryResultRSSIDoesNotTriggerUpdateCallbackOnRSSI) {
  irr().rssi = 1;
  device()->MutBrEdr().SetInquiryData(irr());
  ASSERT_TRUE(was_called());  // Callback due to |class_of_device|.

  ClearWasCalled();
  irr().rssi = 20;
  device()->MutBrEdr().SetInquiryData(irr());
  EXPECT_FALSE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsTriggersUpdateCallbackOnDeviceClassSet) {
  eirep().class_of_device = common::DeviceClass({0x06, 0x02, 0x02});  // Phone.
  device()->MutBrEdr().SetInquiryData(eirep());
  EXPECT_TRUE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsTriggersUpdateCallbackOnNameSet) {
  device()->MutBrEdr().SetInquiryData(eirep());
  ASSERT_TRUE(was_called());  // Callback due to |class_of_device|.

  ClearWasCalled();
  eir_data().Write(kEirData);
  device()->MutBrEdr().SetInquiryData(eirep());
  EXPECT_TRUE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsUpdateCallbackProvidesUpdatedDevice) {
  eirep().clock_offset = htole16(1);
  eirep().page_scan_repetition_mode = hci::PageScanRepetitionMode::kR1;
  eirep().rssi = kTestRSSI;
  eirep().class_of_device = common::DeviceClass({0x06, 0x02, 0x02});  // Phone.
  eir_data().Write(kEirData);
  ASSERT_FALSE(device()->bredr().HasValue());
  ASSERT_FALSE(device()->name().HasValue());
  ASSERT_EQ(device()->rssi(), hci::kRSSIInvalid);
  cache()->set_device_updated_callback([](const auto& updated_dev) {
    const auto& data = updated_dev.bredr();
    ASSERT_TRUE(data);
    ASSERT_TRUE(data->clock_offset().HasValue());
    ASSERT_TRUE(data->page_scan_repetition_mode().HasValue());
    ASSERT_TRUE(data->device_class().HasValue());
    ASSERT_TRUE(updated_dev.name().HasValue());

    EXPECT_EQ(*data->clock_offset(), 0x8001);
    EXPECT_EQ(*data->page_scan_repetition_mode(),
              hci::PageScanRepetitionMode::kR1);
    EXPECT_EQ(data->device_class()->major_class(),
              common::DeviceClass::MajorClass(0x02));
    EXPECT_EQ(updated_dev.rssi(), kTestRSSI);
    EXPECT_EQ(*updated_dev.name(), "Test");
  });
  device()->MutBrEdr().SetInquiryData(eirep());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsGeneratesExactlyOneUpdateCallbackRegardlessOfNumberOfFieldsChanged) {
  eirep().clock_offset = htole16(1);
  eirep().page_scan_repetition_mode = hci::PageScanRepetitionMode::kR1;
  eirep().rssi = kTestRSSI;
  eirep().class_of_device = common::DeviceClass({0x06, 0x02, 0x02});  // Phone.
  eir_data().Write(kEirData);

  size_t call_count = 0;
  cache()->set_device_updated_callback([&](const auto&) { ++call_count; });
  device()->MutBrEdr().SetInquiryData(eirep());
  EXPECT_EQ(call_count, 1U);
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsDoesNotTriggerUpdateCallbackOnSameDeviceClass) {
  eirep().class_of_device = common::DeviceClass({0x06, 0x02, 0x02});  // Phone.
  device()->MutBrEdr().SetInquiryData(eirep());
  ASSERT_TRUE(was_called());

  ClearWasCalled();
  device()->MutBrEdr().SetInquiryData(eirep());
  EXPECT_FALSE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsDoesNotTriggerUpdateCallbackOnSameName) {
  eir_data().Write(kEirData);
  device()->MutBrEdr().SetInquiryData(eirep());
  ASSERT_TRUE(was_called());

  ClearWasCalled();
  device()->MutBrEdr().SetInquiryData(eirep());
  EXPECT_FALSE(was_called());
}

TEST_F(
    GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsDoesNotTriggerUpdateCallbackOnRSSI) {
  eirep().rssi = 1;
  device()->MutBrEdr().SetInquiryData(eirep());
  ASSERT_TRUE(was_called());  // Callback due to |class_of_device|.

  ClearWasCalled();
  eirep().rssi = 20;
  device()->MutBrEdr().SetInquiryData(eirep());
  EXPECT_FALSE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetNameTriggersUpdateCallback) {
  device()->SetName("nombre");
  EXPECT_TRUE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetNameDoesNotTriggerUpdateCallbackOnSameName) {
  device()->SetName("nombre");
  ASSERT_TRUE(was_called());

  bool was_called_again = false;
  cache()->set_device_updated_callback(
      [&](const auto&) { was_called_again = true; });
  device()->SetName("nombre");
  EXPECT_FALSE(was_called_again);
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetLowEnergyConnectionParamsDoesNotTriggerUpdateCallback) {
  device()->MutLe().SetConnectionParameters({});
  EXPECT_FALSE(was_called());
}

TEST_F(GAP_RemoteDeviceCacheTest_UpdateCallbackTest,
       SetLowEnergyPreferredConnectionParamsDoesNotTriggerUpdateCallback) {
  device()->MutLe().SetPreferredConnectionParameters({});
  EXPECT_FALSE(was_called());
}

class GAP_RemoteDeviceCacheTest_ExpirationTest
    : public GAP_RemoteDeviceCacheTest {
 public:
  void SetUp() {
    NewDevice(kAddrLePublic, true);
    device_id_ = device()->identifier();
    device_addr_ = device()->address();
    ASSERT_TRUE(device()->temporary());
  }

 private:
  std::string device_id_;
  DeviceAddress device_addr_;
};

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       TemporaryDiesSixtySecondsAfterBirth) {
  RunLoopFor(kCacheTimeout);
  EXPECT_FALSE(cache()->FindDeviceById(device()->identifier()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       TemporaryLivesForSixtySecondsAfterBirth) {
  RunLoopFor(kCacheTimeout - zx::msec(1));
  EXPECT_TRUE(cache()->FindDeviceById(device()->identifier()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       TemporaryLivesForSixtySecondsSinceLastSeen) {
  RunLoopFor(kCacheTimeout - zx::msec(1));
  ASSERT_EQ(device(), cache()->FindDeviceById(device()->identifier()));

  // Tickle device, and verify it sticks around for another cache timeout.
  device()->SetName("nombre");
  RunLoopFor(kCacheTimeout - zx::msec(1));
  EXPECT_TRUE(cache()->FindDeviceById(device()->identifier()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       TemporaryDiesSixtySecondsAfterLastSeen) {
  RunLoopFor(kCacheTimeout - zx::msec(1));
  ASSERT_EQ(device(), cache()->FindDeviceById(device()->identifier()));

  // Tickle device, and verify it expires after cache timeout.
  device()->SetName("nombre");
  RunLoopFor(kCacheTimeout);
  EXPECT_FALSE(cache()->FindDeviceById(device()->identifier()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       CanMakeNonTemporaryJustBeforeSixtySeconds) {
  // At last possible moment, make device non-temporary,
  RunLoopFor(kCacheTimeout - zx::msec(1));
  ASSERT_EQ(device(), cache()->FindDeviceById(device()->identifier()));
  device()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  ASSERT_FALSE(device()->temporary());

  // Verify that devices survives.
  RunLoopFor(kCacheTimeout * 10);
  EXPECT_EQ(device(), cache()->FindDeviceById(device()->identifier()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       LEConnectedDeviceLivesMuchMoreThanSixtySeconds) {
  device()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  RunLoopFor(kCacheTimeout * 10);
  EXPECT_TRUE(cache()->FindDeviceById(device()->identifier()));
  EXPECT_FALSE(device()->temporary());
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       BREDRConnectedDeviceLivesMuchMoreThanSixtySeconds) {
  device()->MutBrEdr().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  RunLoopFor(kCacheTimeout * 10);
  EXPECT_TRUE(cache()->FindDeviceById(device()->identifier()));
  EXPECT_FALSE(device()->temporary());
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       LEPublicDeviceRemainsNonTemporaryOnDisconnect) {
  ASSERT_EQ(kAddrLePublic, device()->address());
  device()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  ASSERT_FALSE(device()->temporary());

  RunLoopFor(zx::sec(61));
  ASSERT_EQ(device(), cache()->FindDeviceById(device()->identifier()));
  ASSERT_TRUE(device()->identity_known());

  device()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kNotConnected);
  EXPECT_FALSE(device()->temporary());

  RunLoopFor(kCacheTimeout);
  EXPECT_EQ(device(), cache()->FindDeviceById(device()->identifier()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       LERandomDeviceBecomesTemporaryOnDisconnect) {
  ASSERT_TRUE(NewDevice(kAddrLeRandom, true));
  ASSERT_TRUE(device()->temporary());
  ASSERT_FALSE(device()->identity_known());

  device()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  ASSERT_FALSE(device()->temporary());
  ASSERT_FALSE(device()->identity_known());

  RunLoopFor(zx::sec(61));
  ASSERT_EQ(device(), cache()->FindDeviceById(device()->identifier()));
  ASSERT_FALSE(device()->identity_known());

  device()->MutLe().SetConnectionState(
      RemoteDevice::ConnectionState::kNotConnected);
  EXPECT_TRUE(device()->temporary());
  EXPECT_FALSE(device()->identity_known());

  RunLoopFor(zx::sec(61));
  EXPECT_FALSE(cache()->FindDeviceById(device()->identifier()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       BREDRDeviceRemainsNonTemporaryOnDisconnect) {
  NewDevice(kAddrBrEdr, true);
  device()->MutBrEdr().SetConnectionState(
      RemoteDevice::ConnectionState::kConnected);
  RunLoopFor(kCacheTimeout * 10);
  ASSERT_EQ(device(), cache()->FindDeviceById(device()->identifier()));
  ASSERT_TRUE(device()->identity_known());
  EXPECT_FALSE(device()->temporary());

  device()->MutBrEdr().SetConnectionState(
      RemoteDevice::ConnectionState::kNotConnected);
  EXPECT_EQ(device(), cache()->FindDeviceById(device()->identifier()));
  EXPECT_FALSE(device()->temporary());

  RunLoopFor(kCacheTimeout);
  EXPECT_EQ(device(), cache()->FindDeviceById(device()->identifier()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest, ExpirationUpdatesAddressMap) {
  RunLoopFor(kCacheTimeout);
  EXPECT_FALSE(cache()->FindDeviceByAddress(device()->address()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       SetAdvertisingDataUpdatesExpiration) {
  RunLoopFor(kCacheTimeout - zx::msec(1));
  device()->MutLe().SetAdvertisingData(kTestRSSI, StaticByteBuffer<1>{});
  RunLoopFor(zx::msec(1));
  EXPECT_TRUE(cache()->FindDeviceById(device()->identifier()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       SetBrEdrInquiryDataFromInquiryResultUpdatesExpiration) {
  hci::InquiryResult ir;
  ir.bd_addr = device()->address().value();
  RunLoopFor(kCacheTimeout - zx::msec(1));
  device()->MutBrEdr().SetInquiryData(ir);
  RunLoopFor(zx::msec(1));
  EXPECT_TRUE(cache()->FindDeviceById(device()->identifier()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest,
       SetBrEdrInquiryDataFromInquiryResultRSSIUpdatesExpiration) {
  hci::InquiryResultRSSI irr;
  irr.bd_addr = device()->address().value();
  RunLoopFor(kCacheTimeout - zx::msec(1));
  device()->MutBrEdr().SetInquiryData(irr);
  RunLoopFor(zx::msec(1));
  EXPECT_TRUE(cache()->FindDeviceById(device()->identifier()));
}

TEST_F(
    GAP_RemoteDeviceCacheTest_ExpirationTest,
    SetBrEdrInquiryDataFromExtendedInquiryResultEventParamsUpdatesExpiration) {
  hci::ExtendedInquiryResultEventParams eirep;
  eirep.bd_addr = device()->address().value();
  RunLoopFor(kCacheTimeout - zx::msec(1));
  device()->MutBrEdr().SetInquiryData(eirep);
  RunLoopFor(zx::msec(1));
  EXPECT_TRUE(cache()->FindDeviceById(device()->identifier()));
}

TEST_F(GAP_RemoteDeviceCacheTest_ExpirationTest, SetNameUpdatesExpiration) {
  RunLoopFor(kCacheTimeout - zx::msec(1));
  device()->SetName({});
  RunLoopFor(zx::msec(1));
  EXPECT_TRUE(cache()->FindDeviceById(device()->identifier()));
}

}  // namespace
}  // namespace gap
}  // namespace btlib
