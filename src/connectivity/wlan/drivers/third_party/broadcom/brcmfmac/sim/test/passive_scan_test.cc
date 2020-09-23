// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

class PassiveScanTest;

struct ApInfo {
  explicit ApInfo(simulation::Environment* env, const common::MacAddr& bssid,
                  const wlan_ssid_t& ssid, const wlan_channel_t& chan)
      : ap_(env, bssid, ssid, chan) {}

  simulation::FakeAp ap_;
  size_t beacons_seen_count_ = 0;
};

class PassiveScanTestInterface : public SimInterface {
 public:
  void OnScanResult(const wlanif_scan_result_t* result) override;
  void OnScanEnd(const wlanif_scan_end_t* end) override;

  PassiveScanTest* test_ = nullptr;
};

class PassiveScanTest : public SimTest {
 public:
  // Set our beacon interval to 80% of the passive scan dwell time
  static constexpr zx::duration kBeaconInterval =
      zx::msec((SimInterface::kDefaultPassiveScanDwellTimeMs / 5) * 4);

  void Init();
  void StartFakeAp(const common::MacAddr& bssid, const wlan_ssid_t& ssid,
                   const wlan_channel_t& chan, zx::duration beacon_interval = kBeaconInterval);

  // All simulated APs
  std::list<std::unique_ptr<ApInfo>> aps_;

  bool all_aps_seen_ = false;

 protected:
  // This is the interface we will use for our single client interface
  PassiveScanTestInterface client_ifc_;
};

void PassiveScanTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);
  client_ifc_.test_ = this;
}

// Create a new AP with the specified parameters, and tell it to start beaconing
void PassiveScanTest::StartFakeAp(const common::MacAddr& bssid, const wlan_ssid_t& ssid,
                                  const wlan_channel_t& chan, zx::duration beacon_interval) {
  auto ap_info = std::make_unique<ApInfo>(env_.get(), bssid, ssid, chan);
  ap_info->ap_.EnableBeacon(beacon_interval);
  aps_.push_back(std::move(ap_info));
}

// Keep track of which AP we received the scan result for, using the BSSID as a unique identifier.
void PassiveScanTestInterface::OnScanResult(const wlanif_scan_result_t* result) {
  SimInterface::OnScanResult(result);

  int matches_seen = 0;

  for (const auto& ap_info : test_->aps_) {
    common::MacAddr mac_addr = ap_info->ap_.GetBssid();
    ASSERT_EQ(sizeof(result->bss.bssid), sizeof(mac_addr.byte));
    if (!std::memcmp(result->bss.bssid, mac_addr.byte, sizeof(mac_addr.byte))) {
      ap_info->beacons_seen_count_++;
      matches_seen++;

      // Verify SSID
      const wlan_ssid_t ssid_info = ap_info->ap_.GetSsid();
      EXPECT_EQ(result->bss.ssid.len, ssid_info.len);
      ASSERT_LE(ssid_info.len, sizeof(ssid_info.ssid));
      EXPECT_EQ(memcmp(result->bss.ssid.data, ssid_info.ssid, ssid_info.len), 0);

      // Verify channel
      const wlan_channel_t channel = ap_info->ap_.GetChannel();
      EXPECT_EQ(result->bss.chan.primary, channel.primary);
      EXPECT_EQ(result->bss.chan.cbw, channel.cbw);
      EXPECT_EQ(result->bss.chan.secondary80, channel.secondary80);
    }
  }

  // There should be exactly one AP per result.
  EXPECT_EQ(matches_seen, 1);
}

void PassiveScanTestInterface::OnScanEnd(const wlanif_scan_end_t* end) {
  SimInterface::OnScanEnd(end);

  for (auto ap_info = test_->aps_.begin(); ap_info != test_->aps_.end(); ap_info++) {
    if ((*ap_info)->beacons_seen_count_ == 0) {
      // Failure
      return;
    }
  }

  // The beacons from all APs were seen
  test_->all_aps_seen_ = true;
}

constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});

TEST_F(PassiveScanTest, BasicFunctionality) {
  constexpr zx::duration kScanStartTime = zx::sec(1);
  constexpr zx::duration kDefaultTestDuration = zx::sec(100);
  constexpr uint64_t kScanId = 0x1248;

  // Create our simulated device
  Init();

  // Start up a single AP
  StartFakeAp(kDefaultBssid, kDefaultSsid, kDefaultChannel);

  // Request a future scan
  SCHEDULE_CALL(kScanStartTime, &PassiveScanTestInterface::StartScan, &client_ifc_, kScanId, false);

  env_->Run(kDefaultTestDuration);

  EXPECT_EQ(all_aps_seen_, true);
}

}  // namespace wlan::brcmfmac
