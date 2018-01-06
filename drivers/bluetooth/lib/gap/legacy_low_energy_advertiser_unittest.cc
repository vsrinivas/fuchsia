// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/legacy_low_energy_advertiser.h"

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/common/uuid.h"
#include "garnet/drivers/bluetooth/lib/gap/random_address_generator.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/defaults.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_device.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace gap {
namespace {

using ::btlib::testing::FakeController;
using TestingBase = ::btlib::testing::FakeControllerTest<FakeController>;

constexpr hci::ConnectionHandle kHandle = 0x0001;

const common::DeviceAddress kPublicAddress(
    common::DeviceAddress::Type::kLEPublic,
    "00:00:00:00:00:01");
const common::DeviceAddress kPeerAddress(common::DeviceAddress::Type::kLERandom,
                                         "00:00:00:00:00:02");

constexpr size_t kDefaultAdSize = 20;

void NopConnectionCallback(hci::ConnectionPtr) {}

class GAP_LegacyLowEnergyAdvertiserTest : public TestingBase {
 public:
  GAP_LegacyLowEnergyAdvertiserTest() = default;
  ~GAP_LegacyLowEnergyAdvertiserTest() override = default;

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
    return [this](uint32_t interval_ms, hci::Status status) {
      last_status_ = status;
      EXPECT_EQ(hci::kSuccess, status);
      message_loop()->PostQuitTask();
    };
  }

  LowEnergyAdvertiser::AdvertisingResultCallback GetErrorCallback() {
    return [this](uint32_t interval_ms, hci::Status status) {
      last_status_ = status;
      EXPECT_NE(hci::kSuccess, status);
      message_loop()->PostQuitTask();
    };
  }

  // Retrieves the last status, and resets the last status to empty.
  common::Optional<hci::Status> MoveLastStatus() {
    return std::move(last_status_);
  }

  // Makes some fake advertising data of a specific |packed_size|
  AdvertisingData GetExampleData(size_t packed_size = kDefaultAdSize) {
    AdvertisingData result;
    if (packed_size == 3) {
      // A random local name of one char. (chosen by dice roll)
      result.SetLocalName("f");
      return result;
    }
    // packed_size >= 4.  Fill with some menufacturer data fields of appropriate
    // length.
    auto buffer = common::CreateStaticByteBuffer(0x00, 0x01, 0x02, 0x03, 0x04,
                                                 0x05, 0x06, 0x07, 0x08);
    size_t bytes_left = packed_size;
    while (bytes_left > 0) {
      // Each field to take 10 bytes total, unless the next header (4 bytes)
      // won't fit. In which case we add enough bytes to finish up.
      size_t data_bytes = bytes_left < 14 ? (bytes_left - 4) : 6;
      result.SetManufacturerData(0xb000 + bytes_left,
                                 buffer.view(0, data_bytes));
      bytes_left = packed_size - result.CalculateBlockSize();
    }
    return result;
  }

 private:
  std::unique_ptr<LegacyLowEnergyAdvertiser> advertiser_;

  common::Optional<hci::Status> last_status_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GAP_LegacyLowEnergyAdvertiserTest);
};

// TODO(jamuraa): Use typed tests to test LowEnergyAdvertiser common properties

// - Error when the advertisement data is too large
TEST_F(GAP_LegacyLowEnergyAdvertiserTest, AdvertisementSizeTest) {
  AdvertisingData ad, scan_data;

  // 4 bytes long (adv length: 7 bytes)
  auto reasonable_data = common::CreateStaticByteBuffer('T', 'e', 's', 't');
  // 30 bytes long (adv length: 33 bytes)
  auto oversize_data = common::CreateStaticByteBuffer(
      'T', 'h', 'e', 'q', 'u', 'i', 'c', 'k', 'b', 'r', 'o', 'w', 'n', 'f', 'o',
      'x', 'w', 'a', 'g', 'g', 'e', 'd', 'i', 't', 's', 't', 'a', 'i', 'l',
      '.');

  // Should accept ads that are of reasonable size
  ad.SetServiceData(common::UUID((uint16_t)0xfeaa), reasonable_data);
  advertiser()->StartAdvertising(kPublicAddress, ad, scan_data, nullptr, 1000,
                                 false, GetSuccessCallback());

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());

  advertiser()->StopAdvertising(kPublicAddress);

  // And reject ads that are too big
  ad.SetServiceData(common::UUID((uint16_t)0xfeaa), oversize_data);
  advertiser()->StartAdvertising(kPublicAddress, ad, scan_data, nullptr, 1000,
                                 false, GetErrorCallback());
  EXPECT_TRUE(MoveLastStatus());
}

// - Stops the advertisement when an incoming connection comes
// - Calls the connectioncallback correctly when it's setup
// - Checks that advertising state is cleaned up.
// - Checks that it is possible to restart advertising.
TEST_F(GAP_LegacyLowEnergyAdvertiserTest, ConnectionTest) {
  AdvertisingData ad = GetExampleData();
  AdvertisingData scan_data;

  hci::ConnectionPtr link;
  auto conn_cb = [&link](auto cb_link) { link = std::move(cb_link); };
  advertiser()->StartAdvertising(kPublicAddress, ad, scan_data, conn_cb, 1000,
                                 false, GetSuccessCallback());
  RunMessageLoop();
  EXPECT_TRUE(MoveLastStatus());

  // The connection manager will hand us a connection when one gets created.
  hci::LEConnectionParameters params;
  advertiser()->OnIncomingConnection(
      std::make_unique<hci::Connection>(kHandle, hci::Connection::Role::kSlave,
                                        kPeerAddress, params, transport()));

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
TEST_F(GAP_LegacyLowEnergyAdvertiserTest, RestartInConnectionCallback) {
  AdvertisingData ad = GetExampleData();
  AdvertisingData scan_data;

  hci::ConnectionPtr link;
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

  hci::LEConnectionParameters params;
  advertiser()->OnIncomingConnection(
      std::make_unique<hci::Connection>(kHandle, hci::Connection::Role::kSlave,
                                        kPeerAddress, params, transport()));

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
TEST_F(GAP_LegacyLowEnergyAdvertiserTest, StartAndStop) {
  constexpr uint16_t kIntervalMs = 500;
  constexpr uint16_t kIntervalSlices = 800;
  AdvertisingData ad = GetExampleData();
  AdvertisingData scan_data;

  common::DeviceAddress addr = RandomAddressGenerator::PrivateAddress();

  advertiser()->StartAdvertising(addr, ad, scan_data, nullptr, kIntervalMs,
                                 false, GetSuccessCallback());

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());

  EXPECT_TRUE(test_device()->le_advertising_state().enabled);
  EXPECT_EQ(addr, test_device()->le_random_address());
  EXPECT_EQ(kIntervalSlices, test_device()->le_advertising_state().interval);

  AdvertisingData controller_ad;
  EXPECT_TRUE(AdvertisingData::FromBytes(
      test_device()->le_advertising_state().advertised_view(), &controller_ad));
  EXPECT_EQ(ad, controller_ad);
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

TEST_F(GAP_LegacyLowEnergyAdvertiserTest, StartWhileStarting) {
  AdvertisingData ad = GetExampleData();
  AdvertisingData scan_data;
  common::DeviceAddress addr = RandomAddressGenerator::PrivateAddress();

  advertiser()->StartAdvertising(addr, ad, scan_data, nullptr, 1000, false,
                                 [](auto, auto) {});
  EXPECT_FALSE(test_device()->le_advertising_state().enabled);

  advertiser()->StartAdvertising(addr, ad, scan_data, nullptr, 1000, false,
                                 GetErrorCallback());
  EXPECT_FALSE(test_device()->le_advertising_state().enabled);
  auto status = MoveLastStatus();
  ASSERT_TRUE(status);
  EXPECT_EQ(hci::Status::kRepeatedAttempts, *status);
}

TEST_F(GAP_LegacyLowEnergyAdvertiserTest, StartWhileStopping) {
  AdvertisingData ad = GetExampleData();
  AdvertisingData scan_data;
  common::DeviceAddress addr = RandomAddressGenerator::PrivateAddress();

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

  // Stop should still be in progress.
  EXPECT_FALSE(test_device()->le_advertising_state().enabled);
  EXPECT_NE(0u, test_device()->le_advertising_state().data_length);

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
TEST_F(GAP_LegacyLowEnergyAdvertiserTest, StopAdvertisingConditions) {
  AdvertisingData ad = GetExampleData();
  AdvertisingData scan_data;

  common::DeviceAddress addr = RandomAddressGenerator::PrivateAddress();

  advertiser()->StartAdvertising(addr, ad, scan_data, nullptr, 1000, false,
                                 GetSuccessCallback());

  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());

  EXPECT_TRUE(test_device()->le_advertising_state().enabled);
  {
    AdvertisingData controller_ad;
    EXPECT_TRUE(AdvertisingData::FromBytes(
        test_device()->le_advertising_state().advertised_view(),
        &controller_ad));
    EXPECT_EQ(ad, controller_ad);
  }
  EXPECT_EQ(addr, test_device()->le_random_address());

  EXPECT_FALSE(advertiser()->StopAdvertising(kPublicAddress));

  EXPECT_TRUE(test_device()->le_advertising_state().enabled);
  {
    AdvertisingData controller_ad;
    EXPECT_TRUE(AdvertisingData::FromBytes(
        test_device()->le_advertising_state().advertised_view(),
        &controller_ad));
    EXPECT_EQ(ad, controller_ad);
  }

  test_device()->SetAdvertisingStateCallback(
      [this]() {
        if (!test_device()->le_advertising_state().enabled &&
            test_device()->le_advertising_state().data_length == 0 &&
            test_device()->le_advertising_state().scan_rsp_length == 0) {
          message_loop()->PostQuitTask();
        }
      },
      message_loop()->task_runner());

  EXPECT_TRUE(advertiser()->StopAdvertising(addr));

  RunMessageLoop();

  EXPECT_FALSE(test_device()->le_advertising_state().enabled);
  EXPECT_EQ(0u, test_device()->le_advertising_state().advertised_view().size());
  EXPECT_EQ(0u, test_device()->le_advertising_state().scan_rsp_view().size());
}

// - Rejects StartAdvertising when Advertising already
TEST_F(GAP_LegacyLowEnergyAdvertiserTest, NoAdvertiseTwice) {
  AdvertisingData ad = GetExampleData();
  AdvertisingData scan_data;

  common::DeviceAddress addr = RandomAddressGenerator::PrivateAddress();

  advertiser()->StartAdvertising(addr, ad, scan_data, nullptr, 1000, false,
                                 GetSuccessCallback());
  RunMessageLoop();

  EXPECT_TRUE(MoveLastStatus());
  EXPECT_TRUE(test_device()->le_advertising_state().enabled);

  advertiser()->StartAdvertising(addr, ad, scan_data, nullptr, 1000, false,
                                 GetErrorCallback());
  EXPECT_TRUE(MoveLastStatus());
  EXPECT_TRUE(test_device()->le_advertising_state().enabled);
}

// - Rejects anonymous advertisement (unsupported)
TEST_F(GAP_LegacyLowEnergyAdvertiserTest, NoAnonymous) {
  AdvertisingData ad = GetExampleData();
  AdvertisingData scan_data;

  common::DeviceAddress addr = RandomAddressGenerator::PrivateAddress();

  advertiser()->StartAdvertising(addr, ad, scan_data, nullptr, 1000, true,
                                 GetErrorCallback());
  EXPECT_TRUE(MoveLastStatus());
  EXPECT_FALSE(test_device()->le_advertising_state().enabled);
}

}  // namespace
}  // namespace gap
}  // namespace btlib
