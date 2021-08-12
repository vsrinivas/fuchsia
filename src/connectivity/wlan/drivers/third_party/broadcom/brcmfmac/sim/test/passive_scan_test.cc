// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/wlanif/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>

#include <functional>
#include <memory>
#include <utility>

#include <lib/zx/clock.h>
#include <ddk/hw/wlan/wlaninfo/c/banjo.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-frame.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/macaddr.h"

namespace wlan::brcmfmac {

using simulation::InformationElement;
using simulation::SimBeaconFrame;
using ::testing::IsTrue;
using ::testing::NotNull;
using ::testing::SizeIs;

class PassiveScanTest;

struct ApInfo {
  explicit ApInfo(simulation::Environment* env, const common::MacAddr& bssid, const cssid_t& ssid,
                  const wlan_channel_t& channel)
      : ap_(env, bssid, ssid, channel) {}

  simulation::FakeAp ap_;
  size_t beacons_seen_count_ = 0;
};

class PassiveScanTestInterface : public SimInterface {
 public:
  // Add a functor that can be run on each scan result by the VerifyScanResult method.
  // This allows scan results to be inspected (e.g. with EXPECT_EQ) as they come in, rather than
  // storing scan results for analysis after the sim env run has completed.
  void AddVerifierFunction(std::function<void(const wlanif_scan_result_t&)>);

  // Remove any verifier functions from the object.
  void ClearVerifierFunction();

  // Run the verifier method (if one was added) on the given scan result.
  void VerifyScanResult(wlanif_scan_result_t result);

  void OnScanResult(const wlanif_scan_result_t* result) override;
  void OnScanEnd(const wlanif_scan_end_t* end) override;

  PassiveScanTest* test_ = nullptr;

 private:
  std::function<void(const wlanif_scan_result_t&)> verifier_fn_;
};

class PassiveScanTest : public SimTest {
 public:
  // Set our beacon interval to 80% of the passive scan dwell time
  static constexpr zx::duration kBeaconInterval =
      zx::msec((SimInterface::kDefaultPassiveScanDwellTimeMs / 5) * 4);

  void Init();

  // Create a new AP with the specified parameters, and tell it to start beaconing.
  void StartFakeAp(const common::MacAddr& bssid, const cssid_t& ssid, const wlan_channel_t& channel,
                   zx::duration beacon_interval = kBeaconInterval);

  // Start a fake AP with a beacon mutator that will be applied to each beacon before it is sent.
  // The fake AP will begin beaconing immediately.
  void StartFakeApWithErrInjBeacon(
      const common::MacAddr& bssid, const cssid_t& ssid, const wlan_channel_t& channel,
      std::function<SimBeaconFrame(const SimBeaconFrame&)> beacon_mutator,
      zx::duration beacon_interval = kBeaconInterval);

  // All simulated APs
  std::list<std::unique_ptr<ApInfo>> aps_;

 protected:
  // This is the interface we will use for our single client interface
  PassiveScanTestInterface client_ifc_;
};

void PassiveScanTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);
  client_ifc_.test_ = this;
  client_ifc_.ClearVerifierFunction();
}

void PassiveScanTest::StartFakeAp(const common::MacAddr& bssid, const cssid_t& ssid,
                                  const wlan_channel_t& channel, zx::duration beacon_interval) {
  auto ap_info = std::make_unique<ApInfo>(env_.get(), bssid, ssid, channel);
  ap_info->ap_.EnableBeacon(beacon_interval);
  aps_.push_back(std::move(ap_info));
}

void PassiveScanTest::StartFakeApWithErrInjBeacon(
    const common::MacAddr& bssid, const cssid_t& ssid, const wlan_channel_t& channel,
    std::function<SimBeaconFrame(const SimBeaconFrame&)> beacon_mutator,
    zx::duration beacon_interval) {
  auto ap_info = std::make_unique<ApInfo>(env_.get(), bssid, ssid, channel);
  ap_info->ap_.AddErrInjBeacon(beacon_mutator);
  ap_info->ap_.EnableBeacon(beacon_interval);
  aps_.push_back(std::move(ap_info));
}

void PassiveScanTestInterface::AddVerifierFunction(
    std::function<void(const wlanif_scan_result_t&)> verifier_fn) {
  verifier_fn_ = std::move(verifier_fn);
}

void PassiveScanTestInterface::ClearVerifierFunction() { verifier_fn_ = nullptr; }

void PassiveScanTestInterface::VerifyScanResult(wlanif_scan_result_t result) {
  if (verifier_fn_ != nullptr) {
    verifier_fn_(result);
  }
}

// Verify that each incoming scan result is as expected, using VerifyScanResult.
void PassiveScanTestInterface::OnScanResult(const wlanif_scan_result_t* result) {
  SimInterface::OnScanResult(result);
  ASSERT_THAT(result, NotNull());
  VerifyScanResult(*result);
}

void PassiveScanTestInterface::OnScanEnd(const wlanif_scan_end_t* end) {
  SimInterface::OnScanEnd(end);
}

constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = CHANNEL_BANDWIDTH_CBW20, .secondary80 = 0};
constexpr cssid_t kDefaultSsid = {.len = 15, .data = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});

TEST_F(PassiveScanTest, BasicFunctionality) {
  const int64_t test_start_timestamp_nanos = zx::clock::get_monotonic().get();
  constexpr zx::duration kScanStartTime = zx::sec(1);
  constexpr zx::duration kDefaultTestDuration = zx::sec(100);
  constexpr uint64_t kScanId = 0x1248;

  // Create our simulated device
  Init();

  // Start up a single AP
  StartFakeAp(kDefaultBssid, kDefaultSsid, kDefaultChannel);

  // Request a future scan
  env_->ScheduleNotification(
      std::bind(&PassiveScanTestInterface::StartScan, &client_ifc_, kScanId, false),
      kScanStartTime);

  // The lambda arg will be run on each result, inside PassiveScanTestInterface::VerifyScanResults.
  client_ifc_.AddVerifierFunction([&test_start_timestamp_nanos](const wlanif_scan_result_t& result) {
    // Verify timestamp is after test start
    ASSERT_GT(result.timestamp_nanos, test_start_timestamp_nanos);
    
    // Verify BSSID.
    ASSERT_EQ(sizeof(result.bss.bssid), sizeof(common::MacAddr::byte));
    const common::MacAddr result_bssid(result.bss.bssid);
    EXPECT_EQ(result_bssid.Cmp(kDefaultBssid), 0);

    // Verify SSID.
    auto ssid = brcmf_find_ssid_in_ies(result.bss.ies_list, result.bss.ies_count);
    EXPECT_THAT(ssid, SizeIs(kDefaultSsid.len));
    ASSERT_LE(kDefaultSsid.len, sizeof(kDefaultSsid.data));
    EXPECT_EQ(std::memcmp(ssid.data(), kDefaultSsid.data, kDefaultSsid.len), 0);

    // Verify channel
    EXPECT_EQ(result.bss.channel.primary, kDefaultChannel.primary);
    EXPECT_EQ(result.bss.channel.cbw, kDefaultChannel.cbw);
    EXPECT_EQ(result.bss.channel.secondary80, kDefaultChannel.secondary80);

    // Verify has RSSI value
    ASSERT_LT(result.bss.rssi_dbm, 0);
  });

  env_->Run(kDefaultTestDuration);
}

TEST_F(PassiveScanTest, ScanWithMalformedBeaconMissingSsidInformationElement) {
  const int64_t test_start_timestamp_nanos = zx::clock::get_monotonic().get();
  constexpr zx::duration kScanStartTime = zx::sec(1);
  constexpr zx::duration kDefaultTestDuration = zx::sec(100);
  constexpr uint64_t kScanId = 0x1248;

  // Create our simulated device
  Init();

  // Functor that will remove the SSID information element from a beacon frame.
  auto beacon_mutator = [](const SimBeaconFrame& beacon) {
    auto tmp_beacon(beacon);
    tmp_beacon.RemoveIe(InformationElement::IE_TYPE_SSID);
    return tmp_beacon;
  };

  // Start up a single AP, with beacon error injection.
  StartFakeApWithErrInjBeacon(kDefaultBssid, kDefaultSsid, kDefaultChannel, beacon_mutator);

  // Request a future scan
  env_->ScheduleNotification(
      std::bind(&PassiveScanTestInterface::StartScan, &client_ifc_, kScanId, false),
      kScanStartTime);

  client_ifc_.AddVerifierFunction([&test_start_timestamp_nanos](const wlanif_scan_result_t& result) {
    // Verify timestamp is after test start
    ASSERT_GT(result.timestamp_nanos, test_start_timestamp_nanos);
    
    // Verify BSSID.
    ASSERT_EQ(sizeof(result.bss.bssid), sizeof(common::MacAddr::byte));
    const common::MacAddr result_bssid(result.bss.bssid);
    EXPECT_EQ(result_bssid.Cmp(kDefaultBssid), 0);

    // Verify that SSID is empty, since there was no SSID IE.
    auto ssid = brcmf_find_ssid_in_ies(result.bss.ies_list, result.bss.ies_count);
    EXPECT_EQ(ssid.size(), 0u);
    ASSERT_LE(kDefaultSsid.len, sizeof(kDefaultSsid.data));

    // Verify channel
    EXPECT_EQ(result.bss.channel.primary, kDefaultChannel.primary);
    EXPECT_EQ(result.bss.channel.cbw, kDefaultChannel.cbw);
    EXPECT_EQ(result.bss.channel.secondary80, kDefaultChannel.secondary80);

    // Verify has RSSI value
    ASSERT_LT(result.bss.rssi_dbm, 0);
  });

  env_->Run(kDefaultTestDuration);
}

// This test case verifies that the driver returns SHOULD_WAIT as the scan result code when firmware
// is busy.
TEST_F(PassiveScanTest, ScanWhenFirmwareBusy) {
  constexpr zx::duration kScanStartTime = zx::sec(1);
  constexpr zx::duration kDefaultTestDuration = zx::sec(100);
  constexpr uint64_t kScanId = 0x1248;

  // Create our simulated device
  Init();

  // Start up a single AP, with beacon error injection.
  StartFakeAp(kDefaultBssid, kDefaultSsid, kDefaultChannel);

  // Set up our injector
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("escan", ZX_OK, BCME_BUSY);

  // Request a future scan
  env_->ScheduleNotification(
      std::bind(&PassiveScanTestInterface::StartScan, &client_ifc_, kScanId, false),
      kScanStartTime);

  env_->Run(kDefaultTestDuration);

  EXPECT_EQ(client_ifc_.ScanResultList(kScanId)->size(), 0U);
  ASSERT_NE(client_ifc_.ScanResultCode(kScanId), std::nullopt);
  EXPECT_EQ(client_ifc_.ScanResultCode(kScanId).value(), WLAN_SCAN_RESULT_SHOULD_WAIT);
}

TEST_F(PassiveScanTest, ScanWhileAssocInProgress) {
  // Scan request for driver should come before connection succeeds.
  constexpr zx::duration kScanStartTime = zx::msec(3);
  constexpr zx::duration kAssocStartTime = zx::msec(1);
  constexpr zx::duration kDefaultTestDuration = zx::sec(100);
  constexpr uint64_t kScanId = 0x1248;

  // Create our simulated device
  Init();

  // Start up an AP for association.
  StartFakeAp(kDefaultBssid, kDefaultSsid, kDefaultChannel);

  client_ifc_.AssociateWith(aps_.front()->ap_, kAssocStartTime);
  // Request a future scan
  env_->ScheduleNotification(
      std::bind(&PassiveScanTestInterface::StartScan, &client_ifc_, kScanId, false),
      kScanStartTime);

  env_->Run(kDefaultTestDuration);

  EXPECT_EQ(client_ifc_.ScanResultList(kScanId)->size(), 0U);
  ASSERT_NE(client_ifc_.ScanResultCode(kScanId), std::nullopt);
  EXPECT_EQ(client_ifc_.ScanResultCode(kScanId).value(), WLAN_SCAN_RESULT_SHOULD_WAIT);
}

TEST_F(PassiveScanTest, ScanAbortedInFirmware) {
  // Assoc request for driver should come while scanning.
  constexpr zx::duration kScanStartTime = zx::msec(1);
  constexpr zx::duration kAssocStartTime = zx::msec(10);
  constexpr zx::duration kDefaultTestDuration = zx::sec(100);
  constexpr uint64_t kScanId = 0x1248;

  // Create our simulated device
  Init();

  // Start up an AP for association.
  StartFakeAp(kDefaultBssid, kDefaultSsid, kDefaultChannel);

  // Request a future scan
  env_->ScheduleNotification(
      std::bind(&PassiveScanTestInterface::StartScan, &client_ifc_, kScanId, false),
      kScanStartTime);

  // Request an association right after the scan
  client_ifc_.AssociateWith(aps_.front()->ap_, kAssocStartTime);

  env_->Run(kDefaultTestDuration);

  EXPECT_EQ(client_ifc_.ScanResultList(kScanId)->size(), 0U);
  ASSERT_NE(client_ifc_.ScanResultCode(kScanId), std::nullopt);
  EXPECT_EQ(client_ifc_.ScanResultCode(kScanId).value(),
            WLAN_SCAN_RESULT_CANCELED_BY_DRIVER_OR_FIRMWARE);
}
}  // namespace wlan::brcmfmac
