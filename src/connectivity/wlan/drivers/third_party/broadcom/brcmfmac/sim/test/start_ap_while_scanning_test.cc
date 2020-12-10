// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = WLAN_CHANNEL_BANDWIDTH__20, .secondary80 = 0};
constexpr wlan_ssid_t kDefaultSsid = {.len = 15, .ssid = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
constexpr uint64_t kDefaultScanId = 0x112233;

class ScanTest;

class ScanTestIfc : public SimInterface {
 public:
  void OnScanEnd(const wlanif_scan_end_t* end) override;
  void OnStartConf(const wlanif_start_confirm_t* resp) override;

  ScanTest* test_;
};

// Override SimTest to coordinate operations between two interfaces. Specifically, when a Start AP
// operation comes in on the softAP interface, verify that an in-progress scan operation on a
// client interface is cancelled.
class ScanTest : public SimTest {
 public:
  void StartAp();

  // Event handlers, invoked by events received on interfaces.
  void OnScanEnd(const wlanif_scan_end_t* end);
  void OnStartConf(const wlanif_start_confirm_t* resp);

 protected:
  void Init();

  ScanTestIfc client_ifc_;
  ScanTestIfc softap_ifc_;

  enum {NOT_STARTED, STARTED, DONE} ap_start_progress_ = NOT_STARTED;
};

void ScanTestIfc::OnScanEnd(const wlanif_scan_end_t* end) {
  // Notify test interface framework
  SimInterface::OnScanEnd(end);

  // Notify test
  test_->OnScanEnd(end);
}

// When we receive confirmation that the AP start operation has completed, let the test know
void ScanTestIfc::OnStartConf(const wlanif_start_confirm_t* resp) {
  // Notify test interface framework
  SimInterface::OnStartConf(resp);

  // Notify test
  test_->OnStartConf(resp);
}

void ScanTest::Init() {
  SimTest::Init();
  client_ifc_.test_ = this;
  softap_ifc_.test_ = this;
}

void ScanTest::OnScanEnd(const wlanif_scan_end_t* end) {
  brcmf_simdev* simdev = device_->GetSim();

  // Verify that Start AP has been called
  ASSERT_NE(ap_start_progress_, NOT_STARTED);

  // Scan should have been cancelled by Start AP request
  EXPECT_EQ(end->code, WLAN_SCAN_RESULT_INTERNAL_ERROR);

  // Verify that the state of the Start AP operation lines up with our expectations
  EXPECT_EQ(ap_start_progress_ == STARTED, brcmf_is_ap_start_pending(simdev->drvr->config));
}

void ScanTest::OnStartConf(const wlanif_start_confirm_t* resp) {
  ap_start_progress_ = DONE;
}

void ScanTest::StartAp() {
  ap_start_progress_ = STARTED;
  softap_ifc_.StartSoftAp(SimInterface::kDefaultSoftApSsid, kDefaultChannel);
}

// This test will attempt to start a softAP interface while a scan is in progress on a client
// interface. It will verify that:
// - The scan is aborted.
// - When the AP is started, it is properly tracked in the driver's internal state so a follow-up
//   scan will not be allowed. Note that this requires driver interspection. We'd like to do this
//   through simple DDK calls, but it requires specific timing for the call to happen after the
//   start AP operation is begun but before the internal state is set, and we don't have the
//   simulator infrastructure in place to support this yet.
// - The start AP operation completes successfully.
TEST_F(ScanTest, ScanApStartInterference) {
  Init();

  // Start a fake AP
  simulation::FakeAp ap(env_.get(), kDefaultBssid, kDefaultSsid, kDefaultChannel);
  ap.EnableBeacon(zx::msec(60));

  StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_);
  StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_);

  SCHEDULE_CALL(zx::msec(10), &SimInterface::StartScan, &client_ifc_, kDefaultScanId, false);
  SCHEDULE_CALL(zx::msec(200), &ScanTest::StartAp, this);

  static constexpr zx::duration kTestDuration = zx::sec(100);
  env_->Run(kTestDuration);

  // Scan should have been cancelled by AP start operation
  EXPECT_NE(client_ifc_.ScanResultCode(kDefaultScanId), std::nullopt);

  // Make sure the AP iface started successfully.
  EXPECT_EQ(softap_ifc_.stats_.start_confirmations.size(), 1U);
  EXPECT_EQ(softap_ifc_.stats_.start_confirmations.back().result_code, WLAN_START_RESULT_SUCCESS);
}

}  // namespace wlan::brcmfmac
