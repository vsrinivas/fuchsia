// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/hardware/wlan/fullmac/c/banjo.h>
#include <fuchsia/hardware/wlan/phyinfo/c/banjo.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <zircon/errors.h>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {
namespace {

constexpr zx::duration kSimulatedClockDuration = zx::sec(10);

}  // namespace

constexpr uint64_t kScanTxnId = 0x4a65616e6e65;
const uint8_t kDefaultChannelsList[11] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const wlan_fullmac_scan_req kDefaultScanReq = {
    .txn_id = kScanTxnId,
    .scan_type = WLAN_SCAN_TYPE_ACTIVE,
    .channels_list = kDefaultChannelsList,
    .channels_count = 11,
    .ssids_list = nullptr,
    .ssids_count = 0,
    .min_channel_time = SimInterface::kDefaultActiveScanDwellTimeMs,
    .max_channel_time = SimInterface::kDefaultActiveScanDwellTimeMs,
};

// For this test, we don't want to use the default scan handlers provided by SimInterface
class EscanArgsIfc : public SimInterface {
 public:
  void OnScanResult(const wlan_fullmac_scan_result_t* result) override {}
  void OnScanEnd(const wlan_fullmac_scan_end_t* end) override;
  bool ScanCompleted() { return scan_completed_; }
  wlan_scan_result_t ScanResult() { return scan_result_; }

 private:
  bool scan_completed_ = false;
  wlan_scan_result_t scan_result_;
};

void EscanArgsIfc::OnScanEnd(const wlan_fullmac_scan_end_t* end) {
  EXPECT_EQ(end->txn_id, kScanTxnId);
  scan_completed_ = true;
  scan_result_ = end->code;
}

class EscanArgsTest : public SimTest {
 public:
  void Init();
  void RunScanTest(const wlan_fullmac_scan_req& req);

 protected:
  EscanArgsIfc client_ifc_;
};

void EscanArgsTest::Init() {
  ASSERT_EQ(SimTest::Init(), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);
}

void EscanArgsTest::RunScanTest(const wlan_fullmac_scan_req& req) {
  client_ifc_.if_impl_ops_->start_scan(client_ifc_.if_impl_ctx_, &req);
  env_->Run(kSimulatedClockDuration);
  ASSERT_TRUE(client_ifc_.ScanCompleted());
}

// Verify that invalid scan params result in a failed scan result
TEST_F(EscanArgsTest, BadScanArgs) {
  Init();
  wlan_fullmac_scan_req req;

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

TEST_F(EscanArgsTest, EmptyChannelList) {
  Init();
  wlan_fullmac_scan_req req;
  req = kDefaultScanReq;
  req.channels_count = 0;
  RunScanTest(req);
  EXPECT_EQ(client_ifc_.ScanResult(), WLAN_SCAN_RESULT_INVALID_ARGS);
}

}  // namespace wlan::brcmfmac
