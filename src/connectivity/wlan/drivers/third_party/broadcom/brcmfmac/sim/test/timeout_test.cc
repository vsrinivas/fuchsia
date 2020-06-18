// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/wlanif.h>
#include <wifi/wifi-config.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::brcmfmac {

// Some default AP and association request values
constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};

constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});

constexpr uint32_t kDwellTimeMs = 120;
constexpr zx::duration kBeaconInterval = zx::msec(60);

class TimeoutTest : public SimTest {
 public:
  // How long an individual test will run for. We need an end time because tests run until no more
  // events remain and so we need to stop aps from beaconing to drain the event queue.
  static constexpr zx::duration kTestDuration = zx::sec(100);

  void Init();
  void Finish();

  // Run through the join => auth => assoc flow
  void StartAssoc();

  // Start a passive scan.
  void StartScan();

  // Start a deauth request from client.
  void StartDeauth();

 protected:
  // This is the interface we will use for our single client interface
  SimInterface client_ifc_;
  std::list<simulation::FakeAp*> aps_;

  // Sme event counters
  size_t scan_result_count_ = 0;
  size_t scan_end_count_ = 0;
  size_t assoc_resp_count_ = 0;
  size_t deauth_conf_count_ = 0;

  // Sme event results
  uint8_t scan_result_;
  uint8_t assoc_result_;

 private:
  static wlanif_impl_ifc_protocol_ops_t sme_ops_;
  wlanif_impl_ifc_protocol sme_protocol_ = {.ops = &sme_ops_, .ctx = this};

  // Event handlers
  void OnJoinConf(const wlanif_join_confirm_t* resp);
  void OnAuthConf(const wlanif_auth_confirm_t* resp);
  void OnAssocConf(const wlanif_assoc_confirm_t* resp);
  void OnScanResult(const wlanif_scan_result_t* result);
  void OnScanEnd(const wlanif_scan_end_t* end);
  void OnDeauthConf(const wlanif_deauth_confirm_t* resp);
};

// Since we're acting as wlanif, we need handlers for any protocol calls we may receive
wlanif_impl_ifc_protocol_ops_t TimeoutTest::sme_ops_ = {
    .on_scan_result =
        [](void* cookie, const wlanif_scan_result_t* result) {
          static_cast<TimeoutTest*>(cookie)->OnScanResult(result);
        },
    .on_scan_end =
        [](void* cookie, const wlanif_scan_end_t* end) {
          static_cast<TimeoutTest*>(cookie)->OnScanEnd(end);
        },
    .join_conf =
        [](void* cookie, const wlanif_join_confirm_t* resp) {
          static_cast<TimeoutTest*>(cookie)->OnJoinConf(resp);
        },
    .auth_conf =
        [](void* cookie, const wlanif_auth_confirm_t* resp) {
          static_cast<TimeoutTest*>(cookie)->OnAuthConf(resp);
        },
    .deauth_conf =
        [](void* cookie, const wlanif_deauth_confirm_t* resp) {
          static_cast<TimeoutTest*>(cookie)->OnDeauthConf(resp);
        },
    .deauth_ind =
        [](void* cookie, const wlanif_deauth_indication_t* ind) {
          // Ignore
        },
    .assoc_conf =
        [](void* cookie, const wlanif_assoc_confirm_t* resp) {
          static_cast<TimeoutTest*>(cookie)->OnAssocConf(resp);
        },
    .signal_report =
        [](void* cookie, const wlanif_signal_report_indication* ind) {
          // Ignore
        },
};

void TimeoutTest::StartScan() {
  wlanif_scan_req_t req = {
      .txn_id = 0,
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .scan_type = WLAN_SCAN_TYPE_PASSIVE,
      .num_channels = 11,
      .channel_list = {9, 10},
      .min_channel_time = kDwellTimeMs,
      .max_channel_time = kDwellTimeMs,
      .num_ssids = 0,
  };
  client_ifc_.if_impl_ops_->start_scan(client_ifc_.if_impl_ctx_, &req);
}

void TimeoutTest::StartAssoc() {
  // Send join request
  wlanif_join_req join_req = {};
  std::memcpy(join_req.selected_bss.bssid, kDefaultBssid.byte, ETH_ALEN);
  join_req.selected_bss.ssid.len = kDefaultSsid.len;
  memcpy(join_req.selected_bss.ssid.data, kDefaultSsid.ssid, WLAN_MAX_SSID_LEN);
  join_req.selected_bss.chan = kDefaultChannel;
  client_ifc_.if_impl_ops_->join_req(client_ifc_.if_impl_ctx_, &join_req);
}

void TimeoutTest::StartDeauth() {
  wlanif_deauth_req_t deauth_req = {};

  std::memcpy(deauth_req.peer_sta_address, kDefaultBssid.byte, ETH_ALEN);
  client_ifc_.if_impl_ops_->deauth_req(client_ifc_.if_impl_ctx_, &deauth_req);
}

// Create our device instance and hook up the callbacks
void TimeoutTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_, &sme_protocol_), ZX_OK);
  SCHEDULE_CALL(kTestDuration, &TimeoutTest::Finish, this);
}

void TimeoutTest::Finish() {
  for (auto ap : aps_) {
    ap->DisableBeacon();
  }
  aps_.clear();
}

void TimeoutTest::OnJoinConf(const wlanif_join_confirm_t* resp) {
  // Send auth request
  wlanif_auth_req_t auth_req;
  std::memcpy(auth_req.peer_sta_address, kDefaultBssid.byte, ETH_ALEN);
  auth_req.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
  auth_req.auth_failure_timeout = 1000;  // ~1s (although value is ignored for now)
  client_ifc_.if_impl_ops_->auth_req(client_ifc_.if_impl_ctx_, &auth_req);
}

void TimeoutTest::OnAuthConf(const wlanif_auth_confirm_t* resp) {
  // Send assoc request
  wlanif_assoc_req_t assoc_req = {.rsne_len = 0, .vendor_ie_len = 0};
  memcpy(assoc_req.peer_sta_address, kDefaultBssid.byte, ETH_ALEN);
  client_ifc_.if_impl_ops_->assoc_req(client_ifc_.if_impl_ctx_, &assoc_req);
}

void TimeoutTest::OnAssocConf(const wlanif_assoc_confirm_t* resp) {
  assoc_resp_count_++;
  ASSERT_LE(assoc_resp_count_, 1U);
  assoc_result_ = resp->result_code;
}

void TimeoutTest::OnScanResult(const wlanif_scan_result_t* result) { scan_result_count_++; }

void TimeoutTest::OnScanEnd(const wlanif_scan_end_t* end) {
  scan_result_ = end->code;
  scan_end_count_++;
}

void TimeoutTest::OnDeauthConf(const wlanif_deauth_confirm_t* resp) { deauth_conf_count_++; }

// Verify scan timeout is triggered.
TEST_F(TimeoutTest, ScanTimeout) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(kBeaconInterval);
  aps_.push_back(&ap);

  // Ignore scan request in sim-fw.
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("escan", ZX_OK, client_ifc_.iface_id_);

  SCHEDULE_CALL(zx::msec(10), &TimeoutTest::StartScan, this);

  env_->Run();

  // Receiving scan_end in SME with error status.
  EXPECT_EQ(scan_end_count_, 1U);
  EXPECT_EQ(scan_result_count_, 0U);
  EXPECT_EQ(scan_result_, WLAN_SCAN_RESULT_INTERNAL_ERROR);
}

// Verify association timeout is triggered.
TEST_F(TimeoutTest, AssocTimeout) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap);

  // Ignore association req in sim-fw.
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjCmd(BRCMF_C_SET_SSID, ZX_OK, client_ifc_.iface_id_);
  SCHEDULE_CALL(zx::msec(10), &TimeoutTest::StartAssoc, this);

  env_->Run();

  // Receiving assoc_resp in SME with error status.
  EXPECT_EQ(assoc_resp_count_, 1U);
  EXPECT_EQ(assoc_result_, WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);
}

// verify the disassociation timeout is triggered.
TEST_F(TimeoutTest, DisassocTimeout) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  aps_.push_back(&ap);

  // Ignore disassociation req in sim-fw.
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjCmd(BRCMF_C_DISASSOC, ZX_OK, client_ifc_.iface_id_);
  SCHEDULE_CALL(zx::msec(10), &TimeoutTest::StartDeauth, this);

  env_->Run();

  // deauth_conf have no return status, just verify it's received.
  EXPECT_EQ(deauth_conf_count_, 1U);
}

// This test case will verify the following senerio: After the driver issuing a connect command to
// firmware, sme sends a deauth_req to driver before firmware response, and sme issue a scan after
// that, the scan will be successfully executed.
TEST_F(TimeoutTest, ScanAfterAssocTimeout) {
  Init();

  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(kBeaconInterval);
  aps_.push_back(&ap);

  // Ignore association req in sim-fw.
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjCmd(BRCMF_C_SET_SSID, ZX_OK, client_ifc_.iface_id_);
  // There are three timers for them, and all have been cancelled.
  SCHEDULE_CALL(zx::msec(10), &TimeoutTest::StartAssoc, this);
  SCHEDULE_CALL(zx::sec(1), &TimeoutTest::StartDeauth, this);
  SCHEDULE_CALL(zx::sec(3), &TimeoutTest::StartScan, this);

  env_->Run();

  // This when we issue a deauth request right after and assoc_req, the successful deauth_req will
  // stop the connect timer for assoc_req, thus no assoc_conf event will be received.
  EXPECT_EQ(assoc_resp_count_, 0U);
  EXPECT_EQ(deauth_conf_count_, 1U);
  // There is only one AP in the environmnet, but two scan results will be heard from SME since the
  // scan dwell time is twice the beacon interval.
  EXPECT_EQ(scan_result_count_, 2U);
  EXPECT_EQ(scan_end_count_, 1U);
  EXPECT_EQ(scan_result_, WLAN_SCAN_RESULT_SUCCESS);
}

}  // namespace wlan::brcmfmac
