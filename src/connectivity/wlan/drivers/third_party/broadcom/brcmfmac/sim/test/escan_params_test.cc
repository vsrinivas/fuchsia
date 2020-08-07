// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

constexpr uint64_t kScanTxnId = 0x4a65616e6e65;
const wlanif_scan_req kDefaultScanReq = {
    .txn_id = kScanTxnId,
    .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
    .scan_type = WLAN_SCAN_TYPE_ACTIVE,
    .num_channels = WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS,
    .min_channel_time = SimInterface::kDefaultActiveScanDwellTimeMs,
    .max_channel_time = SimInterface::kDefaultActiveScanDwellTimeMs,
    .num_ssids = WLAN_SCAN_MAX_SSIDS};

// For this test, we don't want to use the default scan handlers provided by SimInterface
class EscanArgsIfc : public SimInterface {
 public:
  void OnScanResult(const wlanif_scan_result_t* result) override {}
  void OnScanEnd(const wlanif_scan_end_t* end) override;
  bool ScanCompleted() { return scan_completed_; }
  wlan_scan_result_t ScanResult() { return scan_result_; }

 private:
  bool scan_completed_ = false;
  wlan_scan_result_t scan_result_;
};

void EscanArgsIfc::OnScanEnd(const wlanif_scan_end_t* end) {
  EXPECT_EQ(end->txn_id, kScanTxnId);
  scan_completed_ = true;
  scan_result_ = end->code;
}

class EscanArgsTest : public SimTest {
 public:
  void Init();
  void RunScanTest(const wlanif_scan_req& req);

 protected:
  EscanArgsIfc client_ifc_;
};

void EscanArgsTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);
}

void EscanArgsTest::RunScanTest(const wlanif_scan_req& req) {
  client_ifc_.if_impl_ops_->start_scan(client_ifc_.if_impl_ctx_, &req);
  env_->Run();
  ASSERT_TRUE(client_ifc_.ScanCompleted());
}

// Verify that the driver is able to process a request that uses the maximum channels and ssids
// allowed.
TEST_F(EscanArgsTest, MaxArgs) {
  Init();
  RunScanTest(kDefaultScanReq);
  EXPECT_EQ(client_ifc_.ScanResult(), WLAN_SCAN_RESULT_SUCCESS);
}

// Verify that invalid scan params result in a failed scan result
TEST_F(EscanArgsTest, BadScanArgs) {
  Init();
  wlanif_scan_req req;

  // More than the allowed number of channels
  req = kDefaultScanReq;
  req.num_channels = WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS + 1;
  RunScanTest(req);
  EXPECT_NE(client_ifc_.ScanResult(), WLAN_SCAN_RESULT_SUCCESS);

  // More than the allowed number of SSIDs
  req = kDefaultScanReq;
  req.num_ssids = WLAN_SCAN_MAX_SSIDS + 1;
  RunScanTest(req);
  EXPECT_NE(client_ifc_.ScanResult(), WLAN_SCAN_RESULT_SUCCESS);

  // Dwell time of zero
  req = kDefaultScanReq;
  req.min_channel_time = 0;
  req.max_channel_time = 0;
  RunScanTest(req);
  EXPECT_NE(client_ifc_.ScanResult(), WLAN_SCAN_RESULT_SUCCESS);

  // min dwell time > max dwell time
  req = kDefaultScanReq;
  req.min_channel_time = req.max_channel_time + 1;
  RunScanTest(req);
  EXPECT_NE(client_ifc_.ScanResult(), WLAN_SCAN_RESULT_SUCCESS);
}

}  // namespace wlan::brcmfmac
