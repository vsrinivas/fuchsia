// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/hci/legacy_low_energy_scanner.h"

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/hci/defaults.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_device.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace hci {
namespace {

using ::bluetooth::testing::FakeController;
using ::bluetooth::testing::FakeDevice;
using TestingBase = ::bluetooth::testing::FakeControllerTest<FakeController>;

constexpr int64_t kScanPeriodMs = 500;

constexpr char kPlainAdvData[] = "Test";
constexpr char kPlainScanRsp[] = "Data";
constexpr char kAdvDataAndScanRsp[] = "TestData";

const common::DeviceAddress kAddress0(common::DeviceAddress::Type::kLEPublic,
                                      "00:00:00:00:00:01");
const common::DeviceAddress kAddress1(common::DeviceAddress::Type::kLERandom,
                                      "00:00:00:00:00:02");
const common::DeviceAddress kAddress2(common::DeviceAddress::Type::kLERandom,
                                      "00:00:00:00:00:03");
const common::DeviceAddress kAddress3(common::DeviceAddress::Type::kLERandom,
                                      "00:00:00:00:00:04");
const common::DeviceAddress kAddress4(common::DeviceAddress::Type::kLERandom,
                                      "00:00:00:00:00:05");
const common::DeviceAddress kAddress5(common::DeviceAddress::Type::kLERandom,
                                      "00:00:00:00:00:06");

class LegacyLowEnergyScannerTest : public TestingBase,
                                   public LowEnergyScanner::Delegate {
 public:
  LegacyLowEnergyScannerTest() = default;
  ~LegacyLowEnergyScannerTest() override = default;

 protected:
  // TestingBase overrides:
  void SetUp() override {
    TestingBase::SetUp();

    FakeController::Settings settings;
    settings.ApplyLegacyLEConfig();
    test_device()->set_settings(settings);

    scanner_ = std::make_unique<LegacyLowEnergyScanner>(
        this, transport(), message_loop()->task_runner());

    test_device()->Start();
  }

  void TearDown() override {
    scanner_ = nullptr;
    test_device()->Stop();
    TestingBase::TearDown();
  }

  using DeviceFoundCallback = std::function<void(const LowEnergyScanResult&,
                                                 const common::ByteBuffer&)>;
  void set_device_found_callback(const DeviceFoundCallback& cb) {
    device_found_cb_ = cb;
  }

  // LowEnergyScanner::Observer overrides:
  void OnDeviceFound(const LowEnergyScanResult& result,
                     const common::ByteBuffer& data) override {
    if (device_found_cb_)
      device_found_cb_(result, data);
  }

  // Adds 6 fake devices using kAddress[0-5] above.
  void AddFakeDevices() {
    // We use malformed data for testing purposes, as we don't care about
    // integrity here.
    auto adv_data = common::CreateStaticByteBuffer('T', 'e', 's', 't');
    auto scan_rsp = common::CreateStaticByteBuffer('D', 'a', 't', 'a');
    auto empty_data = common::DynamicByteBuffer();

    // Generates ADV_IND, scan response is reported in a single HCI event.
    auto fake_device = std::make_unique<FakeDevice>(kAddress0, true, true);
    fake_device->SetAdvertisingData(adv_data);
    fake_device->SetScanResponse(true, scan_rsp);
    test_device()->AddLEDevice(std::move(fake_device));

    // Generates ADV_SCAN_IND, scan response is reported over multiple HCI
    // events.
    fake_device = std::make_unique<FakeDevice>(kAddress1, false, true);
    fake_device->SetAdvertisingData(adv_data);
    fake_device->SetScanResponse(false, scan_rsp);
    test_device()->AddLEDevice(std::move(fake_device));

    // Generates ADV_IND, empty scan response is reported over multiple HCI
    // events.
    fake_device = std::make_unique<FakeDevice>(kAddress2, true, true);
    fake_device->SetAdvertisingData(adv_data);
    fake_device->SetScanResponse(false, empty_data);
    test_device()->AddLEDevice(std::move(fake_device));

    // Generates ADV_IND, empty adv data and non-empty scan response is reported
    // over multiple HCI events.
    fake_device = std::make_unique<FakeDevice>(kAddress3, true, true);
    fake_device->SetScanResponse(false, scan_rsp);
    test_device()->AddLEDevice(std::move(fake_device));

    // Generates ADV_IND, a scan response is never sent even though ADV_IND is
    // scannable.
    fake_device = std::make_unique<FakeDevice>(kAddress4, true, false);
    fake_device->SetAdvertisingData(adv_data);
    test_device()->AddLEDevice(std::move(fake_device));

    // Generates ADV_NONCONN_IND
    fake_device = std::make_unique<FakeDevice>(kAddress5, false, false);
    fake_device->SetAdvertisingData(adv_data);
    test_device()->AddLEDevice(std::move(fake_device));
  }

  LegacyLowEnergyScanner* scanner() const { return scanner_.get(); }

 private:
  DeviceFoundCallback device_found_cb_;
  std::unique_ptr<LegacyLowEnergyScanner> scanner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LegacyLowEnergyScannerTest);
};

TEST_F(LegacyLowEnergyScannerTest, StartScanHCIErrors) {
  EXPECT_EQ(LowEnergyScanner::State::kIdle, scanner()->state());
  EXPECT_FALSE(scanner()->IsScanning());
  EXPECT_FALSE(test_device()->le_scan_state().enabled);

  LowEnergyScanner::Status status;
  auto cb = [&status, this](LowEnergyScanner::Status in_status) {
    status = in_status;
    message_loop()->QuitNow();
  };

  // Set Scan Parameters will fail.
  test_device()->SetDefaultResponseStatus(hci::kLESetScanParameters,
                                          hci::Status::kHardwareFailure);
  EXPECT_EQ(0, test_device()->le_scan_state().scan_interval);

  EXPECT_TRUE(scanner()->StartScan(
      false, hci::defaults::kLEScanInterval, hci::defaults::kLEScanWindow,
      false, hci::LEScanFilterPolicy::kNoWhiteList, kScanPeriodMs, cb));

  EXPECT_EQ(LowEnergyScanner::State::kInitiating, scanner()->state());

  // Calling StartScan() should fail as the state is not kIdle.
  EXPECT_FALSE(scanner()->StartScan(
      false, hci::defaults::kLEScanInterval, hci::defaults::kLEScanWindow,
      false, hci::LEScanFilterPolicy::kNoWhiteList, kScanPeriodMs, cb));

  RunMessageLoop();

  // Status should be failure and the scan parameters shouldn't have applied.
  EXPECT_EQ(LowEnergyScanner::Status::kFailed, status);
  EXPECT_EQ(0, test_device()->le_scan_state().scan_interval);
  EXPECT_FALSE(test_device()->le_scan_state().enabled);
  EXPECT_EQ(LowEnergyScanner::State::kIdle, scanner()->state());
  EXPECT_FALSE(scanner()->IsScanning());

  // Set Scan Parameters will succeed but Set Scan Enable will fail.
  test_device()->ClearDefaultResponseStatus(hci::kLESetScanParameters);
  test_device()->SetDefaultResponseStatus(hci::kLESetScanEnable,
                                          hci::Status::kHardwareFailure);

  EXPECT_TRUE(scanner()->StartScan(
      false, hci::defaults::kLEScanInterval, hci::defaults::kLEScanWindow,
      false, hci::LEScanFilterPolicy::kNoWhiteList, kScanPeriodMs, cb));

  EXPECT_EQ(LowEnergyScanner::State::kInitiating, scanner()->state());
  RunMessageLoop();

  // Status should be failure but the scan parameters should have applied.
  EXPECT_EQ(LowEnergyScanner::Status::kFailed, status);
  EXPECT_EQ(hci::defaults::kLEScanInterval,
            test_device()->le_scan_state().scan_interval);
  EXPECT_EQ(hci::defaults::kLEScanWindow,
            test_device()->le_scan_state().scan_window);
  EXPECT_EQ(hci::LEScanFilterPolicy::kNoWhiteList,
            test_device()->le_scan_state().filter_policy);
  EXPECT_FALSE(test_device()->le_scan_state().enabled);
  EXPECT_EQ(LowEnergyScanner::State::kIdle, scanner()->state());
  EXPECT_FALSE(scanner()->IsScanning());
}

TEST_F(LegacyLowEnergyScannerTest, StartScan) {
  EXPECT_EQ(LowEnergyScanner::State::kIdle, scanner()->state());
  EXPECT_FALSE(scanner()->IsScanning());
  EXPECT_FALSE(test_device()->le_scan_state().enabled);

  LowEnergyScanner::Status status;
  auto cb = [&status, this](LowEnergyScanner::Status in_status) {
    status = in_status;
    message_loop()->QuitNow();
  };

  EXPECT_TRUE(scanner()->StartScan(
      true /* active */, hci::defaults::kLEScanInterval,
      hci::defaults::kLEScanWindow, true /* filter_duplicates */,
      hci::LEScanFilterPolicy::kNoWhiteList, kScanPeriodMs, cb));

  EXPECT_EQ(LowEnergyScanner::State::kInitiating, scanner()->state());
  RunMessageLoop();

  // Scan should have started.
  EXPECT_EQ(LowEnergyScanner::Status::kStarted, status);
  EXPECT_EQ(hci::defaults::kLEScanInterval,
            test_device()->le_scan_state().scan_interval);
  EXPECT_EQ(hci::defaults::kLEScanWindow,
            test_device()->le_scan_state().scan_window);
  EXPECT_EQ(hci::LEScanFilterPolicy::kNoWhiteList,
            test_device()->le_scan_state().filter_policy);
  EXPECT_EQ(hci::LEScanType::kActive, test_device()->le_scan_state().scan_type);
  EXPECT_TRUE(test_device()->le_scan_state().filter_duplicates);
  EXPECT_TRUE(test_device()->le_scan_state().enabled);
  EXPECT_EQ(LowEnergyScanner::State::kScanning, scanner()->state());
  EXPECT_TRUE(scanner()->IsScanning());

  // Calling StartScan should fail as a scan is already in progress.
  EXPECT_FALSE(scanner()->StartScan(
      true /* active */, hci::defaults::kLEScanInterval,
      hci::defaults::kLEScanWindow, true /* filter_duplicates */,
      hci::LEScanFilterPolicy::kNoWhiteList, kScanPeriodMs, cb));

  // After 200 ms (kScanPeriodMs) the scan should stop by itself.
  RunMessageLoop();
  EXPECT_EQ(LowEnergyScanner::Status::kComplete, status);
  EXPECT_FALSE(test_device()->le_scan_state().enabled);
  EXPECT_EQ(LowEnergyScanner::State::kIdle, scanner()->state());
  EXPECT_FALSE(scanner()->IsScanning());
}

TEST_F(LegacyLowEnergyScannerTest, StopScan) {
  EXPECT_EQ(LowEnergyScanner::State::kIdle, scanner()->state());
  EXPECT_FALSE(scanner()->IsScanning());
  EXPECT_FALSE(test_device()->le_scan_state().enabled);

  LowEnergyScanner::Status status;
  auto cb = [&status, this](LowEnergyScanner::Status in_status) {
    status = in_status;
    message_loop()->QuitNow();
  };

  // Calling StopScan should fail while a scan is not in progress.
  EXPECT_FALSE(scanner()->StopScan());

  // Pass a long scan period value. This should not matter as we will terminate
  // the scan directly.
  EXPECT_TRUE(scanner()->StartScan(
      true /* active */, hci::defaults::kLEScanInterval,
      hci::defaults::kLEScanWindow, true /* filter_duplicates */,
      hci::LEScanFilterPolicy::kNoWhiteList, 10 * kScanPeriodMs, cb));

  EXPECT_EQ(LowEnergyScanner::State::kInitiating, scanner()->state());
  RunMessageLoop();

  // Scan should have started.
  EXPECT_EQ(LowEnergyScanner::Status::kStarted, status);
  EXPECT_TRUE(test_device()->le_scan_state().enabled);
  EXPECT_EQ(LowEnergyScanner::State::kScanning, scanner()->state());
  EXPECT_TRUE(scanner()->IsScanning());

  // StopScan() should terminate the scan session and the status should be
  // kStopped.
  EXPECT_TRUE(scanner()->StopScan());
  RunMessageLoop();

  EXPECT_EQ(LowEnergyScanner::Status::kStopped, status);
  EXPECT_FALSE(test_device()->le_scan_state().enabled);
  EXPECT_EQ(LowEnergyScanner::State::kIdle, scanner()->state());
  EXPECT_FALSE(scanner()->IsScanning());
}

TEST_F(LegacyLowEnergyScannerTest, StopScanWhileInitiating) {
  EXPECT_EQ(LowEnergyScanner::State::kIdle, scanner()->state());
  EXPECT_FALSE(scanner()->IsScanning());
  EXPECT_FALSE(test_device()->le_scan_state().enabled);

  LowEnergyScanner::Status status;
  auto cb = [&status, this](LowEnergyScanner::Status in_status) {
    status = in_status;
    message_loop()->QuitNow();
  };

  EXPECT_TRUE(scanner()->StartScan(
      true /* active */, hci::defaults::kLEScanInterval,
      hci::defaults::kLEScanWindow, true /* filter_duplicates */,
      hci::LEScanFilterPolicy::kNoWhiteList, kScanPeriodMs, cb));

  EXPECT_EQ(LowEnergyScanner::State::kInitiating, scanner()->state());

  // Call StopScan(). This should cancel the HCI command sequence set up by
  // StartScan() so that the it never completes. The HCI_LE_Set_Scan_Parameters
  // command *may* get sent but the scan should never get enabled.
  EXPECT_TRUE(scanner()->StopScan());
  RunMessageLoop();

  EXPECT_EQ(LowEnergyScanner::Status::kStopped, status);
  EXPECT_FALSE(test_device()->le_scan_state().enabled);
  EXPECT_EQ(LowEnergyScanner::State::kIdle, scanner()->state());
  EXPECT_FALSE(scanner()->IsScanning());
}

TEST_F(LegacyLowEnergyScannerTest, ActiveScanResults) {
  AddFakeDevices();

  LowEnergyScanner::Status status;
  auto cb = [&status, this](LowEnergyScanner::Status in_status) {
    status = in_status;
    if (status == LowEnergyScanner::Status::kComplete)
      message_loop()->QuitNow();
  };

  std::map<common::DeviceAddress, std::pair<LowEnergyScanResult, std::string>>
      results;
  set_device_found_callback([&results](const auto& result, const auto& data) {
    results[result.address] = std::make_pair(result, data.ToString());
  });

  // Perform an active scan.
  EXPECT_TRUE(scanner()->StartScan(
      true, hci::defaults::kLEScanInterval, hci::defaults::kLEScanWindow, true,
      hci::LEScanFilterPolicy::kNoWhiteList, kScanPeriodMs, cb));

  EXPECT_EQ(LowEnergyScanner::State::kInitiating, scanner()->state());
  RunMessageLoop();

  EXPECT_EQ(6u, results.size());

  // Verify the 6 results against the fake devices that were set up by
  // AddFakeDevices(). Since the scan period ended naturally, LowEnergyScanner
  // should generate a device found event for all pending reports even if a scan
  // response was not received for a scannable device (see Fake Device 4, i.e.
  // kAddress4): Result 0
  auto iter = results.find(kAddress0);
  EXPECT_NE(iter, results.end());

  auto& result_pair = iter->second;
  EXPECT_EQ(kAdvDataAndScanRsp, result_pair.second);
  EXPECT_EQ(kAddress0, result_pair.first.address);
  EXPECT_TRUE(result_pair.first.connectable);
  results.erase(iter);

  // Result 1
  iter = results.find(kAddress1);
  EXPECT_NE(iter, results.end());

  result_pair = iter->second;
  EXPECT_EQ(kAdvDataAndScanRsp, result_pair.second);
  EXPECT_EQ(kAddress1, result_pair.first.address);
  EXPECT_FALSE(result_pair.first.connectable);
  results.erase(iter);

  // Result 2
  iter = results.find(kAddress2);
  EXPECT_NE(iter, results.end());

  result_pair = iter->second;
  EXPECT_EQ(kPlainAdvData, result_pair.second);
  EXPECT_EQ(kAddress2, result_pair.first.address);
  EXPECT_TRUE(result_pair.first.connectable);
  results.erase(iter);

  // Result 3
  iter = results.find(kAddress3);
  EXPECT_NE(iter, results.end());

  result_pair = iter->second;
  EXPECT_EQ(kPlainScanRsp, result_pair.second);
  EXPECT_EQ(kAddress3, result_pair.first.address);
  EXPECT_TRUE(result_pair.first.connectable);
  results.erase(iter);

  // Result 4
  iter = results.find(kAddress4);
  EXPECT_NE(iter, results.end());

  result_pair = iter->second;
  EXPECT_EQ(kPlainAdvData, result_pair.second);
  EXPECT_EQ(kAddress4, result_pair.first.address);
  EXPECT_TRUE(result_pair.first.connectable);
  results.erase(iter);

  // Result 5
  iter = results.find(kAddress5);
  EXPECT_NE(iter, results.end());

  result_pair = iter->second;
  EXPECT_EQ(kPlainAdvData, result_pair.second);
  EXPECT_EQ(kAddress5, result_pair.first.address);
  EXPECT_FALSE(result_pair.first.connectable);
  results.erase(iter);

  EXPECT_TRUE(results.empty());
}

TEST_F(LegacyLowEnergyScannerTest, StopDuringActiveScan) {
  AddFakeDevices();

  LowEnergyScanner::Status status;
  auto cb = [&status, this](LowEnergyScanner::Status in_status) {
    status = in_status;
    if (status == LowEnergyScanner::Status::kStarted ||
        status == LowEnergyScanner::Status::kStopped) {
      message_loop()->QuitNow();
    }
  };

  std::map<common::DeviceAddress, std::pair<LowEnergyScanResult, std::string>>
      results;
  set_device_found_callback(
      [&results, this](const auto& result, const auto& data) {
        results[result.address] = std::make_pair(result, data.ToString());

        // Stop the scan after observing the last fake device that we added.
        // FakeController sends device found events for fake devices in the
        // order in which they were added.
        if (result.address == kAddress5)
          message_loop()->QuitNow();
      });

  // Perform an active scan indefinitely. This means that the scan period will
  // never complete by itself.
  EXPECT_TRUE(scanner()->StartScan(true, hci::defaults::kLEScanInterval,
                                   hci::defaults::kLEScanWindow, true,
                                   hci::LEScanFilterPolicy::kNoWhiteList,
                                   LowEnergyScanner::kPeriodInfinite, cb));
  EXPECT_EQ(LowEnergyScanner::State::kInitiating, scanner()->state());
  RunMessageLoop();
  EXPECT_EQ(LowEnergyScanner::State::kScanning, scanner()->state());

  // Run the message loop until we've seen an event for the last device that we
  // added. Fake Device 4 (i.e. kAddress4) is scannable but it never sends a
  // scan response so we expect that remain in the scanner's pending reports
  // list.
  RunMessageLoop();
  EXPECT_EQ(5u, results.size());
  EXPECT_EQ(results.find(kAddress4), results.end());

  // Stop the scan. Since we are terminating the scan period early,
  // LowEnergyScanner should not send a report for the pending device.
  EXPECT_TRUE(scanner()->StopScan());
  RunMessageLoop();
  EXPECT_EQ(LowEnergyScanner::State::kIdle, scanner()->state());

  EXPECT_EQ(5u, results.size());
  EXPECT_EQ(results.find(kAddress4), results.end());
}

TEST_F(LegacyLowEnergyScannerTest, PassiveScanResults) {
  AddFakeDevices();

  LowEnergyScanner::Status status;
  auto cb = [&status, this](LowEnergyScanner::Status in_status) {
    status = in_status;
    if (status == LowEnergyScanner::Status::kComplete)
      message_loop()->QuitNow();
  };

  std::map<common::DeviceAddress, std::pair<LowEnergyScanResult, std::string>>
      results;
  set_device_found_callback([&results](const auto& result, const auto& data) {
    results[result.address] = std::make_pair(result, data.ToString());
  });

  // Perform a passive scan.
  EXPECT_TRUE(scanner()->StartScan(
      false, hci::defaults::kLEScanInterval, hci::defaults::kLEScanWindow, true,
      hci::LEScanFilterPolicy::kNoWhiteList, kScanPeriodMs, cb));

  EXPECT_EQ(LowEnergyScanner::State::kInitiating, scanner()->state());
  RunMessageLoop();

  EXPECT_EQ(6u, results.size());

  // Verify the 6 results against the fake devices that were set up by
  // AddFakeDevices(). All Scan Response PDUs should have been ignored. Result 0
  auto iter = results.find(kAddress0);
  EXPECT_NE(iter, results.end());

  auto& result_pair = iter->second;
  EXPECT_EQ(kPlainAdvData, result_pair.second);
  EXPECT_EQ(kAddress0, result_pair.first.address);
  EXPECT_TRUE(result_pair.first.connectable);
  results.erase(iter);

  // Result 1
  iter = results.find(kAddress1);
  EXPECT_NE(iter, results.end());

  result_pair = iter->second;
  EXPECT_EQ(kPlainAdvData, result_pair.second);
  EXPECT_EQ(kAddress1, result_pair.first.address);
  EXPECT_FALSE(result_pair.first.connectable);
  results.erase(iter);

  // Result 2
  iter = results.find(kAddress2);
  EXPECT_NE(iter, results.end());

  result_pair = iter->second;
  EXPECT_EQ(kPlainAdvData, result_pair.second);
  EXPECT_EQ(kAddress2, result_pair.first.address);
  EXPECT_TRUE(result_pair.first.connectable);
  results.erase(iter);

  // Result 3
  iter = results.find(kAddress3);
  EXPECT_NE(iter, results.end());

  result_pair = iter->second;
  EXPECT_EQ("", result_pair.second);
  EXPECT_EQ(kAddress3, result_pair.first.address);
  EXPECT_TRUE(result_pair.first.connectable);
  results.erase(iter);

  // Result 4
  iter = results.find(kAddress4);
  EXPECT_NE(iter, results.end());

  result_pair = iter->second;
  EXPECT_EQ(kPlainAdvData, result_pair.second);
  EXPECT_EQ(kAddress4, result_pair.first.address);
  EXPECT_TRUE(result_pair.first.connectable);
  results.erase(iter);

  // Result 5
  iter = results.find(kAddress5);
  EXPECT_NE(iter, results.end());

  result_pair = iter->second;
  EXPECT_EQ(kPlainAdvData, result_pair.second);
  EXPECT_EQ(kAddress5, result_pair.first.address);
  EXPECT_FALSE(result_pair.first.connectable);
  results.erase(iter);

  EXPECT_TRUE(results.empty());
}

}  // namespace
}  // namespace hci
}  // namespace bluetooth
