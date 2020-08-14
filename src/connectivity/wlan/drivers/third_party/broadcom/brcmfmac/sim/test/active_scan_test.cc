// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

struct ApInfo {
  explicit ApInfo(simulation::Environment* env, const common::MacAddr& bssid,
                  const wlan_ssid_t& ssid, const wlan_channel_t& chan)
      : ap_(env, bssid, ssid, chan) {}

  simulation::FakeAp ap_;
  bool probe_resp_seen_ = false;
};

struct ClientIfc : public SimInterface {
  void OnScanResult(const wlanif_scan_result_t* result) override;
  void OnScanEnd(const wlanif_scan_end_t* end) override;

  // Txn ID for the current scan
  uint64_t scan_txn_id_ = 0;

  // Result of the previous scan
  wlan_scan_result_t scan_result_code_ = ZX_OK;

  std::list<wlanif_scan_result> scan_results_;
};

class ActiveScanTest : public SimTest {
 public:
  static constexpr zx::duration kBeaconInterval = zx::msec(100);
  static constexpr uint32_t kDwellTimeMs = 120;

  ActiveScanTest() = default;
  void Init();
  void StartFakeAp(const common::MacAddr& bssid, const wlan_ssid_t& ssid,
                   const wlan_channel_t& chan, zx::duration beacon_interval = kBeaconInterval);

  void StartScan(const wlanif_scan_req_t* req);
  void VerifyScanResults();
  void EndSimulation();

  bool all_aps_seen_ = false;

  void GetFirmwarePfnMac();

 protected:
  // This is the interface we will use for our single client interface
  ClientIfc client_ifc_;

 private:
  // StationIfc methods
  void Rx(std::shared_ptr<const simulation::SimFrame> frame,
          std::shared_ptr<const simulation::WlanRxInfo> info) override;

  // All simulated APs
  std::list<std::unique_ptr<ApInfo>> aps_;

  // Mac address of sim_fw
  common::MacAddr sim_fw_mac_;
  common::MacAddr last_pfn_mac_ = common::kZeroMac;
  std::optional<common::MacAddr> sim_fw_pfn_mac_;
};

void ClientIfc::OnScanResult(const wlanif_scan_result_t* result) {
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(scan_txn_id_, result->txn_id);
  scan_results_.emplace_back(*result);
}

void ClientIfc::OnScanEnd(const wlanif_scan_end_t* end) { scan_result_code_ = end->code; }

void ActiveScanTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);

  // Get the interface MAC address
  client_ifc_.GetMacAddr(&sim_fw_mac_);
}

void ActiveScanTest::StartFakeAp(const common::MacAddr& bssid, const wlan_ssid_t& ssid,
                                 const wlan_channel_t& chan, zx::duration beacon_interval) {
  auto ap_info = std::make_unique<ApInfo>(env_.get(), bssid, ssid, chan);
  // Beacon is also enabled here to make sure this is not disturbing the correct result.
  ap_info->ap_.EnableBeacon(beacon_interval);
  aps_.push_back(std::move(ap_info));
}

// Tell the DUT to run a scan
void ActiveScanTest::StartScan(const wlanif_scan_req_t* req) {
  client_ifc_.if_impl_ops_->start_scan(client_ifc_.if_impl_ctx_, req);
}

// Called when simulation time has run out. Takes down all fake APs and the simulated DUT.
void ActiveScanTest::EndSimulation() {
  for (auto ap_info = aps_.begin(); ap_info != aps_.end(); ap_info++) {
    (*ap_info)->ap_.DisableBeacon();
  }
}

void ActiveScanTest::GetFirmwarePfnMac() {
  brcmf_simdev* sim = device_->GetSim();
  if (!sim_fw_pfn_mac_) {
    struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, client_ifc_.iface_id_);
    zx_status_t status =
        brcmf_fil_iovar_data_get(ifp, "pfn_macaddr", sim_fw_pfn_mac_->byte, ETH_ALEN, nullptr);
    EXPECT_EQ(status, ZX_OK);
  }
}

void ActiveScanTest::VerifyScanResults() {
  for (auto result : client_ifc_.scan_results_) {
    int matches_seen = 0;

    for (auto ap_info = aps_.begin(); ap_info != aps_.end(); ap_info++) {
      common::MacAddr mac_addr = (*ap_info)->ap_.GetBssid();
      ASSERT_EQ(sizeof(result.bss.bssid), sizeof(mac_addr.byte));
      if (!std::memcmp(result.bss.bssid, mac_addr.byte, sizeof(mac_addr.byte))) {
        (*ap_info)->probe_resp_seen_ = true;
        matches_seen++;

        // Verify SSID
        wlan_ssid_t ssid_info = (*ap_info)->ap_.GetSsid();
        EXPECT_EQ(result.bss.ssid.len, ssid_info.len);
        ASSERT_LE(ssid_info.len, sizeof(ssid_info.ssid));
        EXPECT_EQ(memcmp(result.bss.ssid.data, ssid_info.ssid, ssid_info.len), 0);

        // Verify channel
        wlan_channel_t channel = (*ap_info)->ap_.GetChannel();
        EXPECT_EQ(result.bss.chan.primary, channel.primary);
        EXPECT_EQ(result.bss.chan.cbw, channel.cbw);
        EXPECT_EQ(result.bss.chan.secondary80, channel.secondary80);

        // Verify has RSSI value
        ASSERT_LT(result.bss.rssi_dbm, 0);
      }
    }

    // There should be exactly one AP per result.
    EXPECT_EQ(matches_seen, 1);
  }

  for (auto ap_info = aps_.begin(); ap_info != aps_.end(); ap_info++) {
    if ((*ap_info)->probe_resp_seen_ == false) {
      // Failure
      return;
    }
  }

  // pfn mac should be different from the one from last scan.
  EXPECT_NE(last_pfn_mac_, *sim_fw_pfn_mac_);
  last_pfn_mac_ = *sim_fw_pfn_mac_;

  // pfn mac will be set back to firmware mac after active scan.
  GetFirmwarePfnMac();
  EXPECT_EQ(sim_fw_mac_, *sim_fw_pfn_mac_);

  sim_fw_pfn_mac_.reset();

  // The probe response from all APs were seen
  all_aps_seen_ = true;
}

void ActiveScanTest::Rx(std::shared_ptr<const simulation::SimFrame> frame,
                        std::shared_ptr<const simulation::WlanRxInfo> info) {
  GetFirmwarePfnMac();

  ASSERT_EQ(frame->FrameType(), simulation::SimFrame::FRAME_TYPE_MGMT);

  auto mgmt_frame = std::static_pointer_cast<const simulation::SimManagementFrame>(frame);

  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_PROBE_REQ) {
    // When a probe request is sent out, the src mac address should not be real mac address.
    auto probe_req = std::static_pointer_cast<const simulation::SimProbeReqFrame>(mgmt_frame);
    EXPECT_NE(probe_req->src_addr_, sim_fw_mac_);
    EXPECT_EQ(probe_req->src_addr_, *sim_fw_pfn_mac_);
  }

  if (mgmt_frame->MgmtFrameType() == simulation::SimManagementFrame::FRAME_TYPE_PROBE_RESP) {
    auto probe_resp = std::static_pointer_cast<const simulation::SimProbeRespFrame>(mgmt_frame);
    EXPECT_NE(probe_resp->dst_addr_, sim_fw_mac_);
    EXPECT_EQ(probe_resp->dst_addr_, *sim_fw_pfn_mac_);
  }
}

// AP 1&2 on channel 2.
constexpr wlan_channel_t kDefaultChannel1 = {
    .primary = 2, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_ssid_t kAp1Ssid = {.len = 16, .ssid = "Fuchsia Fake AP1"};
const common::MacAddr kAp1Bssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
constexpr wlan_ssid_t kAp2Ssid = {.len = 16, .ssid = "Fuchsia Fake AP2"};
const common::MacAddr kAp2Bssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbd});

// AP 3 on channel 4.
constexpr wlan_channel_t kDefaultChannel2 = {
    .primary = 4, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_ssid_t kAp3Ssid = {.len = 16, .ssid = "Fuchsia Fake AP3"};
const common::MacAddr kAp3Bssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbe});

// This test case might fail in a very low possibility because it's random.
TEST_F(ActiveScanTest, RandomMacThreeAps) {
  // Start time and end time of this test case
  constexpr zx::duration kScanStartTime = zx::sec(1);
  constexpr zx::duration kDefaultTestDuration = zx::sec(10);

  // Create simulated device
  Init();

  // Start the first AP
  StartFakeAp(kAp1Bssid, kAp1Ssid, kDefaultChannel1);
  StartFakeAp(kAp2Bssid, kAp2Ssid, kDefaultChannel1);
  StartFakeAp(kAp3Bssid, kAp3Ssid, kDefaultChannel2);

  wlanif_scan_req_t req = {
      .txn_id = ++client_ifc_.scan_txn_id_,
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .scan_type = WLAN_SCAN_TYPE_ACTIVE,
      .num_channels = 5,
      .channel_list = {1, 2, 3, 4, 5},
      .min_channel_time = kDwellTimeMs,
      .max_channel_time = kDwellTimeMs,
      .num_ssids = 0,
  };

  SCHEDULE_CALL(kScanStartTime, &ActiveScanTest::StartScan, this, &req);

  // Schedule scan end in environment
  SCHEDULE_CALL(kDefaultTestDuration, &ActiveScanTest::EndSimulation, this);

  env_->Run();

  VerifyScanResults();
  EXPECT_EQ(all_aps_seen_, true);
  EXPECT_EQ(client_ifc_.scan_result_code_, ZX_OK);
}

TEST_F(ActiveScanTest, ScanTwice) {
  constexpr zx::duration kScanStartTime = zx::sec(1);
  constexpr zx::duration kDefaultTestDuration = zx::sec(10);

  Init();

  wlanif_scan_req_t req = {
      .txn_id = ++client_ifc_.scan_txn_id_,
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .scan_type = WLAN_SCAN_TYPE_ACTIVE,
      .num_channels = 5,
      .channel_list = {1, 2, 3, 4, 5},
      .min_channel_time = kDwellTimeMs,
      .max_channel_time = kDwellTimeMs,
      .num_ssids = 0,
  };

  SCHEDULE_CALL(kScanStartTime, &ActiveScanTest::StartScan, this, &req);

  env_->Run();

  VerifyScanResults();

  SCHEDULE_CALL(kScanStartTime, &ActiveScanTest::StartScan, this, &req);

  SCHEDULE_CALL(kDefaultTestDuration, &ActiveScanTest::EndSimulation, this);

  env_->Run();

  VerifyScanResults();
  EXPECT_EQ(client_ifc_.scan_result_code_, ZX_OK);
}

// This test is to verify brcmfmac driver will return an error when an invalid ssid list
// is indicated in active scan test request.
TEST_F(ActiveScanTest, OverSizeSsid) {
  constexpr zx::duration kFirstScanStartTime = zx::sec(1);
  constexpr zx::duration kSecondScanStartTime = zx::sec(2);
  constexpr zx::duration kDefaultTestDuration = zx::sec(10);

  Init();

  StartFakeAp(kAp1Bssid, kAp1Ssid, kDefaultChannel1);

  wlanif_ssid_t invalid_scan_ssid = {
      .len = 33,
      .data = "1234567890",
  };

  wlanif_ssid_t valid_scan_ssid = {
      .len = 16,
      .data = "Fuchsia Fake AP1",
  };

  // Case contains over-size ssid in ssid field of request.
  wlanif_scan_req_t req_break_ssid = {
      .txn_id = ++client_ifc_.scan_txn_id_,
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .ssid = invalid_scan_ssid,
      .scan_type = WLAN_SCAN_TYPE_ACTIVE,
      .num_channels = 5,
      .channel_list = {1, 2, 3, 4, 5},
      .min_channel_time = kDwellTimeMs,
      .max_channel_time = kDwellTimeMs,
      .num_ssids = 0,
  };

  // Case contains over-size ssid in ssid_list in request.
  wlanif_scan_req_t req_break_ssid_list = {.txn_id = ++client_ifc_.scan_txn_id_,
                                           .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
                                           .scan_type = WLAN_SCAN_TYPE_ACTIVE,
                                           .num_channels = 5,
                                           .channel_list = {1, 2, 3, 4, 5},
                                           .min_channel_time = kDwellTimeMs,
                                           .max_channel_time = kDwellTimeMs,
                                           .num_ssids = 2,
                                           .ssid_list = {valid_scan_ssid, invalid_scan_ssid}};

  // Two active scans are scheduled,
  SCHEDULE_CALL(kFirstScanStartTime, &ActiveScanTest::StartScan, this, &req_break_ssid);
  SCHEDULE_CALL(kSecondScanStartTime, &ActiveScanTest::StartScan, this, &req_break_ssid_list);

  SCHEDULE_CALL(kDefaultTestDuration, &ActiveScanTest::EndSimulation, this);

  env_->Run();

  VerifyScanResults();
  EXPECT_EQ(client_ifc_.scan_result_code_, WLAN_SCAN_RESULT_INTERNAL_ERROR);
}

}  // namespace wlan::brcmfmac
