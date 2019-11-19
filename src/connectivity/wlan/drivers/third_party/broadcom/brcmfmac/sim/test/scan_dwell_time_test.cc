// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include "gtest/gtest.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

// Fake AP configuration
constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
constexpr zx::duration kBeaconInterval = zx::msec(1000);

// Time to remain on each channel - should be > beacon interval
constexpr uint32_t kDwellTimeMs = 1200;

// Delay between scans
constexpr zx::duration kScanGapTime = zx::msec(10);

// How many scans we will run. Each time we will expect to see a beacon from the fake AP.
constexpr size_t kTotalScanCount = 10;

struct ApInfo {
  explicit ApInfo(simulation::Environment* env, const common::MacAddr& bssid,
                  const wlan_ssid_t& ssid, const wlan_channel_t& chan)
      : ap_(env, bssid, ssid, chan) {}

  simulation::FakeAp ap_;
  size_t beacons_seen_count_ = 0;
};

class ScanTest : public SimTest {
 public:
  static constexpr zx::duration kDefaultBeaconInterval = zx::msec(100);

  ScanTest() = default;
  void Init();
  void StartFakeAp(const common::MacAddr& bssid, const wlan_ssid_t& ssid,
                   const wlan_channel_t& chan,
                   zx::duration beacon_interval = kDefaultBeaconInterval);

  // Event handlers
  void EndSimulation();
  void StartScan();

  // SME standin functions
  void OnScanResult(const wlanif_scan_result_t* result);
  void OnScanEnd(const wlanif_scan_end_t* end);

  enum { NOT_RUNNING, RUNNING } scan_state_ = NOT_RUNNING;
  size_t scans_remaining_ = kTotalScanCount;

 private:
  // StationIfc methods
  void Rx(void* pkt) override;
  // RxBeacon handler not needed because the test doesn't need to observe them
  void ReceiveNotification(void* payload) override;

  // This is the interface we will use for our single client interface
  std::unique_ptr<SimInterface> client_ifc_;

  // Our simulated AP
  std::unique_ptr<ApInfo> ap_info_;

  // Txn ID for the current scan
  uint64_t scan_txn_id_ = 0;

  // SME callbacks
  static wlanif_impl_ifc_protocol_ops_t sme_ops_;
  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};
};

// Since we're acting as wlanif, we need handlers for any protocol calls we may receive
wlanif_impl_ifc_protocol_ops_t ScanTest::sme_ops_ = {
    .on_scan_result =
        [](void* cookie, const wlanif_scan_result_t* result) {
          static_cast<ScanTest*>(cookie)->OnScanResult(result);
        },
    .on_scan_end =
        [](void* cookie, const wlanif_scan_end_t* end) {
          static_cast<ScanTest*>(cookie)->OnScanEnd(end);
        },
};

void ScanTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(CreateInterface(WLAN_INFO_MAC_ROLE_CLIENT, sme_protocol_, &client_ifc_), ZX_OK);
}

// Create a new AP with the specified parameters, and tell it to start beaconing
void ScanTest::StartFakeAp(const common::MacAddr& bssid, const wlan_ssid_t& ssid,
                           const wlan_channel_t& chan, zx::duration beacon_interval) {
  ap_info_ = std::make_unique<ApInfo>(env_.get(), bssid, ssid, chan);
  ap_info_->ap_.EnableBeacon(beacon_interval);
}

// Should never be called
void ScanTest::Rx(void* pkt) { GTEST_FAIL(); }

// Called when simulation time has run out. Takes down all fake APs and the simulated DUT.
void ScanTest::EndSimulation() {
  ap_info_->ap_.DisableBeacon();
  zx_status_t status = device_->WlanphyImplDestroyIface(client_ifc_->iface_id_);
  EXPECT_EQ(status, ZX_OK);
  // TODO - check status. brcmfmac doesn't support destroying an interface yet.
}

// Tell the DUT to run a scan
void ScanTest::StartScan() {
  wlanif_scan_req_t req = {
      .txn_id = ++scan_txn_id_,
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .scan_type = WLAN_SCAN_TYPE_PASSIVE,
      .num_channels = 1,
      .channel_list = {kDefaultChannel.primary},
      .min_channel_time = kDwellTimeMs,
      .max_channel_time = kDwellTimeMs,
      .num_ssids = 0,
  };
  client_ifc_->if_impl_ops_->start_scan(client_ifc_->if_impl_ctx_, &req);
  scan_state_ = RUNNING;
  scans_remaining_--;
}

void ScanTest::ReceiveNotification(void* payload) {
  auto fn = static_cast<std::function<void()>*>(payload);
  (*fn)();
  delete fn;
}

// Keep track of which AP we received the scan result for, using the BSSID as a unique identifier.
void ScanTest::OnScanResult(const wlanif_scan_result_t* result) {
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(scan_txn_id_, result->txn_id);
  EXPECT_EQ(kDefaultBssid, common::MacAddr(result->bss.bssid));
  ap_info_->beacons_seen_count_++;
}

void ScanTest::OnScanEnd(const wlanif_scan_end_t* end) {
  scan_state_ = NOT_RUNNING;

  // Make sure that we received a beacon during the scan
  ASSERT_GT(ap_info_->beacons_seen_count_, (size_t)0);

  if (scans_remaining_ > 0) {
    // Schedule next scan
    ap_info_->beacons_seen_count_ = 0;
    auto scan_handler = new std::function<void()>;
    *scan_handler = std::bind(&ScanTest::StartScan, this);
    env_->ScheduleNotification(this, kScanGapTime, scan_handler);
  }
}

TEST_F(ScanTest, PassiveDwellTime) {
  constexpr zx::duration kScanStartTime = zx::sec(1);
  constexpr zx::duration kDefaultTestDuration = zx::sec(100);

  // Create our simulated device
  Init();

  // Start up a single AP
  StartFakeAp(kDefaultBssid, kDefaultSsid, kDefaultChannel, kBeaconInterval);

  // Start scans
  auto scan_handler = new std::function<void()>;
  *scan_handler = std::bind(&ScanTest::StartScan, this);
  env_->ScheduleNotification(this, kScanStartTime, scan_handler);

  // Request a future notification so we can shut down the test
  auto end_handler = new std::function<void()>;
  *end_handler = std::bind(&ScanTest::EndSimulation, this);
  env_->ScheduleNotification(this, kDefaultTestDuration, end_handler);

  env_->Run();

  EXPECT_EQ(scans_remaining_, (size_t)0);
  EXPECT_EQ(scan_state_, NOT_RUNNING);
}

}  // namespace wlan::brcmfmac
