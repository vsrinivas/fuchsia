// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/common/c/banjo.h>

#include <wifi/wifi-config.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

constexpr wlan_channel_t kDefaultChannel = {
    .primary = 9, .cbw = CHANNEL_BANDWIDTH_CBW20, .secondary80 = 0};
constexpr cssid_t kDefaultSsid = {.len = 15, .data = "Fuchsia Fake AP"};
const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});
constexpr uint64_t kFirstScanId = 0x112233;
constexpr uint64_t kSecondScanId = 0x112234;

class ScanAndApStartTest;

class ScanTestIfc : public SimInterface {
 public:
  void OnScanEnd(const wlanif_scan_end_t* end) override;
  void OnStartConf(const wlanif_start_confirm_t* resp) override;

  ScanAndApStartTest* test_;
};

// Override SimTest to coordinate operations between two interfaces. Specifically, when a Start AP
// operation comes in on the softAP interface, verify that an in-progress scan operation on a
// client interface is cancelled.
class ScanAndApStartTest : public SimTest {
 public:
  void StartAp();

  // Event handlers, invoked by events received on interfaces.
  void OnScanEnd(const wlanif_scan_end_t* end);
  void OnStartConf(const wlanif_start_confirm_t* resp);

  std::unique_ptr<simulation::FakeAp> ap_;

 protected:
  void Init();

  ScanTestIfc client_ifc_;
  ScanTestIfc softap_ifc_;

  enum { NOT_STARTED, STARTED, DONE } ap_start_progress_ = NOT_STARTED;
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

void ScanAndApStartTest::Init() {
  SimTest::Init();
  client_ifc_.test_ = this;
  softap_ifc_.test_ = this;

  // Start a fake AP for scan.
  ap_ = std::make_unique<simulation::FakeAp>(env_.get(), kDefaultBssid, kDefaultSsid,
                                             kDefaultChannel);
  ap_->EnableBeacon(zx::msec(60));

  StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_);
  StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_);
}

void ScanAndApStartTest::OnScanEnd(const wlanif_scan_end_t* end) {
  brcmf_simdev* simdev = device_->GetSim();

  // Verify that Start AP has been called
  ASSERT_NE(ap_start_progress_, NOT_STARTED);

  // Verify that the state of the Start AP operation lines up with our expectations
  EXPECT_EQ(ap_start_progress_ == STARTED, brcmf_is_ap_start_pending(simdev->drvr->config));
}

void ScanAndApStartTest::OnStartConf(const wlanif_start_confirm_t* resp) {
  ap_start_progress_ = DONE;
}

void ScanAndApStartTest::StartAp() {
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
TEST_F(ScanAndApStartTest, ScanApStartInterference) {
  Init();

  env_->ScheduleNotification(std::bind(&SimInterface::StartScan, &client_ifc_, kFirstScanId, false),
                             zx::msec(10));
  env_->ScheduleNotification(std::bind(&ScanAndApStartTest::StartAp, this), zx::msec(200));

  static constexpr zx::duration kTestDuration = zx::sec(100);
  env_->Run(kTestDuration);

  // Scan should have been cancelled by AP start operation
  auto result = client_ifc_.ScanResultCode(kFirstScanId);
  EXPECT_NE(result, std::nullopt);
  EXPECT_EQ(*result, WLAN_SCAN_RESULT_CANCELED_BY_DRIVER_OR_FIRMWARE);

  // Make sure the AP iface started successfully.
  EXPECT_EQ(softap_ifc_.stats_.start_confirmations.size(), 1U);
  EXPECT_EQ(softap_ifc_.stats_.start_confirmations.back().result_code, WLAN_START_RESULT_SUCCESS);
}

TEST_F(ScanAndApStartTest, ScanAbortFailure) {
  Init();

  // Return an error on scan abort request from firmware.
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjCmd(BRCMF_C_SCAN, ZX_ERR_IO_REFUSED, BCME_OK,
                                     client_ifc_.iface_id_);

  env_->ScheduleNotification(std::bind(&SimInterface::StartScan, &client_ifc_, kFirstScanId, false),
                             zx::msec(10));
  env_->ScheduleNotification(std::bind(&ScanAndApStartTest::StartAp, this), zx::msec(200));

  static constexpr zx::duration kFirstRunDuration = zx::sec(50);
  env_->Run(kFirstRunDuration);

  // The first scan should be done because the abort is failed
  auto first_result = client_ifc_.ScanResultCode(kFirstScanId);
  EXPECT_NE(first_result, std::nullopt);
  EXPECT_EQ(*first_result, WLAN_SCAN_RESULT_SUCCESS);

  // Make sure the AP iface started successfully.
  EXPECT_EQ(softap_ifc_.stats_.start_confirmations.size(), 1U);
  EXPECT_EQ(softap_ifc_.stats_.start_confirmations.back().result_code, WLAN_START_RESULT_SUCCESS);

  env_->ScheduleNotification(
      std::bind(&SimInterface::StartScan, &client_ifc_, kSecondScanId, false), zx::msec(10));

  // Run the test for another 50 seconds.
  static constexpr zx::duration kSecondRunDuration = zx::sec(50);
  env_->Run(kSecondRunDuration);

  // The second scan should also be successfully done without being blocked by the remaining
  // BRCMF_SCAN_STATUS_ABORT bit.
  auto second_result = client_ifc_.ScanResultCode(kSecondScanId);
  EXPECT_NE(second_result, std::nullopt);
  EXPECT_EQ(*second_result, WLAN_SCAN_RESULT_SUCCESS);
}

// This test verifies that when a scan request from SME is canceled by the driver because of an AP
// start request is ongoing, SME will receive a SHOULD_WAIT status code for scan result.
TEST_F(ScanAndApStartTest, ScanWhileApStart) {
  Init();

  // To simulate the situation where scan is blocked by AP start process, inject an error to
  // SET_SSID command, so that if the scan comes inside the 1 second AP start timeout limit, it will
  // be rejected by the driver.
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjCmd(BRCMF_C_SET_SSID, ZX_OK, BCME_OK, softap_ifc_.iface_id_);

  env_->ScheduleNotification(std::bind(&ScanAndApStartTest::StartAp, this), zx::msec(10));
  env_->ScheduleNotification(std::bind(&SimInterface::StartScan, &client_ifc_, kFirstScanId, false),
                             zx::msec(300));

  static constexpr zx::duration kTestDuration = zx::sec(50);
  env_->Run(kTestDuration);

  // The first scan should be done because the abort is failed
  auto first_result = client_ifc_.ScanResultCode(kFirstScanId);
  EXPECT_NE(first_result, std::nullopt);
  EXPECT_EQ(*first_result, WLAN_SCAN_RESULT_SHOULD_WAIT);

  // The result of AP iface start should be NOT_SUPPORT when timeout happened.
  EXPECT_EQ(softap_ifc_.stats_.start_confirmations.size(), 1U);
  EXPECT_EQ(softap_ifc_.stats_.start_confirmations.back().result_code,
            WLAN_START_RESULT_NOT_SUPPORTED);
}

}  // namespace wlan::brcmfmac
