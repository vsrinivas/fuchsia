// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/hci/legacy_low_energy_advertiser.h"

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/common/uuid.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/defaults.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_device.h"
#include "lib/fxl/macros.h"

namespace btlib {

namespace hci {
namespace {

using ::btlib::testing::FakeController;
using TestingBase = ::btlib::testing::FakeControllerTest<FakeController>;

constexpr ConnectionHandle kHandle = 0x0001;

const common::DeviceAddress kPublicAddress(
    common::DeviceAddress::Type::kLEPublic,
    "00:00:00:00:00:01");
const common::DeviceAddress kPeerAddress(common::DeviceAddress::Type::kLERandom,
                                         "00:00:00:00:00:02");

constexpr size_t kDefaultAdSize = 20;

void NopConnectionCallback(ConnectionPtr) {}

class HCI_LegacyLowEnergyAdvertiserTest : public TestingBase {
 public:
  HCI_LegacyLowEnergyAdvertiserTest() = default;
  ~HCI_LegacyLowEnergyAdvertiserTest() override = default;

 protected:
  // TestingBase overrides:
  void SetUp() override {
    TestingBase::SetUp();

    FakeController::Settings settings;
    settings.ApplyLegacyLEConfig();
    settings.bd_addr = kPublicAddress;
    test_device()->set_settings(settings);

    advertiser_ = std::make_unique<LegacyLowEnergyAdvertiser>(transport());

    test_device()->Start();
  }

  void TearDown() override {
    advertiser_ = nullptr;
    test_device()->Stop();
    TestingBase::TearDown();
  }

  LegacyLowEnergyAdvertiser* advertiser() const { return advertiser_.get(); }

  LowEnergyAdvertiser::AdvertisingResultCallback GetSuccessCallback() {
    return [this](uint32_t interval_ms, Status status) {
      last_status_ = status;
      EXPECT_EQ(kSuccess, status);
      message_loop()->PostQuitTask();
    };
  }

  LowEnergyAdvertiser::AdvertisingResultCallback GetErrorCallback() {
    return [this](uint32_t interval_ms, Status status) {
      last_status_ = status;
      EXPECT_NE(kSuccess, status);
      message_loop()->PostQuitTask();
    };
  }

  // Retrieves the last status, and resets the last status to empty.
  common::Optional<Status> MoveLastStatus() { return std::move(last_status_); }

  // Makes some fake advertising data of a specific |packed_size|
  common::DynamicByteBuffer GetExampleData(size_t size = kDefaultAdSize) {
    common::DynamicByteBuffer result(size);
    // Count backwards.
    for (size_t i = 0; i < size; i++) {
      result[i] = (uint8_t)((size - i) % 255);
    }
    return result;
  }

 private:
  std::unique_ptr<LegacyLowEnergyAdvertiser> advertiser_;

  common::Optional<Status> last_status_;

  FXL_DISALLOW_COPY_AND_ASSIGN(HCI_LegacyLowEnergyAdvertiserTest);
};

// TODO(jamuraa): Use typed tests to test LowEnergyAdvertiser common properties

// - Error when the advertisement data is too large
TEST_F(HCI_LegacyLowEnergyAdvertiserTest, AdvertisementSizeTest) {
  // 4 bytes long (adv length: 7 bytes)
  auto reasonable_data = common::CreateStaticByteBuffer(0x20, 0x06, 0xaa, 0xfe,
                                                        'T', 'e', 's', 't');
  // 30 bytes long (adv length: 33 bytes)
  auto oversize_data = common::CreateStaticByteBuffer(
      0x20, 0x20, 0xaa, 0xfe, 'T', 'h', 'e', 'q', 'u', 'i', 'c', 'k', 'b', 'r',
      'o', 'w', 'n', 'f', 'o', 'x', 'w', 'a', 'g', 'g', 'e', 'd', 'i', 't', 's',
      't', 'a', 'i', 'l', '.');

  common::DynamicByteBuffer scan_data;

  // Should accept ads that are of reasonable size
  advertiser()->StartAdvertising(kPublicAddress, reasonable_data, scan_data,
                                 nullptr, 1000, false, GetSuccessCallback());

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());

  advertiser()->StopAdvertising(kPublicAddress);

  // And reject ads that are too big
  advertiser()->StartAdvertising(kPublicAddress, oversize_data, scan_data,
                                 nullptr, 1000, false, GetErrorCallback());
  EXPECT_TRUE(MoveLastStatus());
}

// - Stops the advertisement when an incoming connection comes
// - Calls the connectioncallback correctly when it's setup
// - Checks that advertising state is cleaned up.
// - Checks that it is possible to restart advertising.
TEST_F(HCI_LegacyLowEnergyAdvertiserTest, ConnectionTest) {
  common::DynamicByteBuffer ad = GetExampleData();
  common::DynamicByteBuffer scan_data;

  ConnectionPtr link;
  auto conn_cb = [&link](auto cb_link) { link = std::move(cb_link); };
  advertiser()->StartAdvertising(kPublicAddress, ad, scan_data, conn_cb, 1000,
                                 false, GetSuccessCallback());
  RunMessageLoop();
  EXPECT_TRUE(MoveLastStatus());

  // The connection manager will hand us a connection when one gets created.
  LEConnectionParameters params;
  advertiser()->OnIncomingConnection(std::make_unique<Connection>(
      kHandle, Connection::Role::kSlave, kPeerAddress, params, transport()));

  ASSERT_TRUE(link);
  EXPECT_EQ(kHandle, link->handle());
  link->set_closed();

  // Advertising state should get cleared.
  bool disabling = true;
  auto disabled_cb = [this, &disabling] {
    // StopAdvertising() sends multiple HCI commands. We only check that the
    // first one succeeded. StartAdvertising cancels the rest of the sequence
    // below.
    if (disabling && !test_device()->le_advertising_state().enabled &&
        test_device()->le_advertising_state().data_length == 0u) {
      message_loop()->QuitNow();
    }
  };
  test_device()->SetAdvertisingStateCallback(disabled_cb,
                                             message_loop()->task_runner());
  RunMessageLoop();
  EXPECT_FALSE(test_device()->le_advertising_state().enabled);

  disabling = false;
  advertiser()->StartAdvertising(kPublicAddress, ad, scan_data, conn_cb, 1000,
                                 false, GetSuccessCallback());
  RunMessageLoop();
  EXPECT_TRUE(MoveLastStatus());
}

// Tests that advertising can be restarted right away in a connection callback.
TEST_F(HCI_LegacyLowEnergyAdvertiserTest, RestartInConnectionCallback) {
  common::DynamicByteBuffer ad = GetExampleData();
  common::DynamicByteBuffer scan_data;

  ConnectionPtr link;
  auto conn_cb = [&, this](auto cb_link) {
    link = std::move(cb_link);
    advertiser()->StartAdvertising(kPublicAddress, ad, scan_data,
                                   NopConnectionCallback, 1000, false,
                                   GetSuccessCallback());
  };

  advertiser()->StartAdvertising(kPublicAddress, ad, scan_data, conn_cb, 1000,
                                 false, GetSuccessCallback());
  RunMessageLoop();
  EXPECT_TRUE(MoveLastStatus());
  EXPECT_TRUE(test_device()->le_advertising_state().enabled);

  bool enabled = true;
  test_device()->SetAdvertisingStateCallback(
      [this, &enabled] {
        // Quit the message loop if the advertising state changes.
        if (enabled != test_device()->le_advertising_state().enabled) {
          enabled = !enabled;
          message_loop()->QuitNow();
        }
      },
      message_loop()->task_runner());

  LEConnectionParameters params;
  advertiser()->OnIncomingConnection(std::make_unique<Connection>(
      kHandle, Connection::Role::kSlave, kPeerAddress, params, transport()));

  // Advertising should get disabled and re-enabled.
  RunMessageLoop();
  EXPECT_FALSE(test_device()->le_advertising_state().enabled);

  RunMessageLoop();
  EXPECT_TRUE(test_device()->le_advertising_state().enabled);
}

// - Starts the advertisement when asked and verifies that the parameters have
//   been passed down correctly.
// - Stops advertisement
// - Uses the random address given and sets it.
TEST_F(HCI_LegacyLowEnergyAdvertiserTest, StartAndStop) {
  constexpr uint16_t kIntervalMs = 500;
  constexpr uint16_t kIntervalSlices = 800;
  common::DynamicByteBuffer ad = GetExampleData();
  common::DynamicByteBuffer scan_data;

  common::DeviceAddress addr = kPeerAddress;

  advertiser()->StartAdvertising(addr, ad, scan_data, nullptr, kIntervalMs,
                                 false, GetSuccessCallback());

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());

  EXPECT_TRUE(test_device()->le_advertising_state().enabled);
  EXPECT_EQ(addr, test_device()->le_random_address());
  EXPECT_EQ(kIntervalSlices, test_device()->le_advertising_state().interval);

  EXPECT_TRUE(ContainersEqual(
      test_device()->le_advertising_state().advertised_view(), ad));
  EXPECT_EQ(0u, test_device()->le_advertising_state().scan_rsp_view().size());

  test_device()->SetAdvertisingStateCallback(
      [this]() {
        if (!test_device()->le_advertising_state().enabled) {
          message_loop()->PostQuitTask();
        }
      },
      message_loop()->task_runner());

  EXPECT_TRUE(advertiser()->StopAdvertising(addr));

  RunMessageLoop();

  EXPECT_FALSE(test_device()->le_advertising_state().enabled);
}

TEST_F(HCI_LegacyLowEnergyAdvertiserTest, StartWhileStarting) {
  common::DynamicByteBuffer ad = GetExampleData();
  common::DynamicByteBuffer scan_data;
  common::DeviceAddress addr = kPeerAddress;

  advertiser()->StartAdvertising(addr, ad, scan_data, nullptr, 1000, false,
                                 [](auto, auto) {});
  EXPECT_FALSE(test_device()->le_advertising_state().enabled);

  advertiser()->StartAdvertising(addr, ad, scan_data, nullptr, 1000, false,
                                 GetErrorCallback());
  EXPECT_FALSE(test_device()->le_advertising_state().enabled);
  auto status = MoveLastStatus();
  ASSERT_TRUE(status);
  EXPECT_EQ(Status::kRepeatedAttempts, *status);
}

TEST_F(HCI_LegacyLowEnergyAdvertiserTest, StartWhileStopping) {
  common::DynamicByteBuffer ad = GetExampleData();
  common::DynamicByteBuffer scan_data;
  common::DeviceAddress addr = kPeerAddress;

  // Get to a started state.
  advertiser()->StartAdvertising(addr, ad, scan_data, nullptr, 1000, false,
                                 GetSuccessCallback());
  RunMessageLoop();
  EXPECT_TRUE(MoveLastStatus());
  EXPECT_TRUE(test_device()->le_advertising_state().enabled);

  // Initiate a request to Stop and wait until it's partially in progress.
  bool disabling = true;
  auto disabled_cb = [this, &disabling] {
    if (disabling && !test_device()->le_advertising_state().enabled) {
      message_loop()->QuitNow();
    }
  };
  test_device()->SetAdvertisingStateCallback(disabled_cb,
                                             message_loop()->task_runner());

  EXPECT_TRUE(advertiser()->StopAdvertising(addr));
  RunMessageLoop();

  // The stop sequence should have started.
  EXPECT_FALSE(test_device()->le_advertising_state().enabled);

  // Starting now should cancel the stop sequence and succeed.
  disabling = false;
  advertiser()->StartAdvertising(addr, ad, scan_data, nullptr, 1000, false,
                                 GetSuccessCallback());
  RunMessageLoop();
  EXPECT_TRUE(MoveLastStatus());
  EXPECT_TRUE(test_device()->le_advertising_state().enabled);
}

// - StopAdvertisement noops when the advertisement address is wrong
// - Sets the advertisement data to null when stopped to prevent data leakage
//   (re-enable advertising without changing data, intercept)
TEST_F(HCI_LegacyLowEnergyAdvertiserTest, StopAdvertisingConditions) {
  common::DynamicByteBuffer ad = GetExampleData();
  common::DynamicByteBuffer scan_data;

  advertiser()->StartAdvertising(kPeerAddress, ad, scan_data, nullptr, 1000,
                                 false, GetSuccessCallback());

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());

  EXPECT_TRUE(test_device()->le_advertising_state().enabled);
  EXPECT_TRUE(ContainersEqual(
      test_device()->le_advertising_state().advertised_view(), ad));
  EXPECT_EQ(kPeerAddress, test_device()->le_random_address());

  EXPECT_FALSE(advertiser()->StopAdvertising(kPublicAddress));

  EXPECT_TRUE(test_device()->le_advertising_state().enabled);
  EXPECT_TRUE(ContainersEqual(
      test_device()->le_advertising_state().advertised_view(), ad));

  test_device()->SetAdvertisingStateCallback(
      [this]() {
        if (!test_device()->le_advertising_state().enabled &&
            test_device()->le_advertising_state().data_length == 0 &&
            test_device()->le_advertising_state().scan_rsp_length == 0) {
          message_loop()->PostQuitTask();
        }
      },
      message_loop()->task_runner());

  EXPECT_TRUE(advertiser()->StopAdvertising(kPeerAddress));

  RunMessageLoop();

  EXPECT_FALSE(test_device()->le_advertising_state().enabled);
  EXPECT_EQ(0u, test_device()->le_advertising_state().advertised_view().size());
  EXPECT_EQ(0u, test_device()->le_advertising_state().scan_rsp_view().size());
}

// - Rejects StartAdvertising for a different address when Advertising already
TEST_F(HCI_LegacyLowEnergyAdvertiserTest, NoAdvertiseTwice) {
  common::DynamicByteBuffer ad = GetExampleData();
  common::DynamicByteBuffer scan_data;

  advertiser()->StartAdvertising(kPeerAddress, ad, scan_data, nullptr, 1000,
                                 false, GetSuccessCallback());
  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_TRUE(test_device()->le_advertising_state().enabled);
  EXPECT_TRUE(ContainersEqual(
      test_device()->le_advertising_state().advertised_view(), ad));

  uint8_t before = ad[0];
  ad[0] = 0xff;
  advertiser()->StartAdvertising(kPublicAddress, ad, scan_data, nullptr, 1000,
                                 false, GetErrorCallback());
  ad[0] = before;
  EXPECT_TRUE(MoveLastStatus());
  EXPECT_TRUE(test_device()->le_advertising_state().enabled);
  EXPECT_EQ(kPeerAddress, test_device()->le_random_address());
  EXPECT_TRUE(ContainersEqual(
      test_device()->le_advertising_state().advertised_view(), ad));
}

// - Updates data and params for the same address when advertising already
TEST_F(HCI_LegacyLowEnergyAdvertiserTest, AdvertiseUpdate) {
  common::DynamicByteBuffer ad = GetExampleData();
  common::DynamicByteBuffer scan_data;

  advertiser()->StartAdvertising(kPeerAddress, ad, scan_data, nullptr, 1000,
                                 false, GetSuccessCallback());
  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_TRUE(test_device()->le_advertising_state().enabled);
  EXPECT_TRUE(ContainersEqual(
      test_device()->le_advertising_state().advertised_view(), ad));

  ad[0] = 0xff;
  advertiser()->StartAdvertising(kPeerAddress, ad, scan_data, nullptr, 2500,
                                 false, GetSuccessCallback());
  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_TRUE(test_device()->le_advertising_state().enabled);
  EXPECT_EQ(kPeerAddress, test_device()->le_random_address());
  EXPECT_TRUE(ContainersEqual(
      test_device()->le_advertising_state().advertised_view(), ad));
  // 2500 ms = 4000 timeslices
  EXPECT_EQ(4000, test_device()->le_advertising_state().interval);
}

// - Rejects anonymous advertisement (unsupported)
TEST_F(HCI_LegacyLowEnergyAdvertiserTest, NoAnonymous) {
  common::DynamicByteBuffer ad = GetExampleData();
  common::DynamicByteBuffer scan_data;

  advertiser()->StartAdvertising(kPeerAddress, ad, scan_data, nullptr, 1000,
                                 true, GetErrorCallback());
  EXPECT_TRUE(MoveLastStatus());
  EXPECT_FALSE(test_device()->le_advertising_state().enabled);
}

}  // namespace
}  // namespace hci
}  // namespace btlib
