// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

// Arbitrary values used to uniquely identify scan requests (and results)
constexpr uint64_t kFirstScanId = 0x4f4a;
constexpr uint64_t kSecondScanId =  0x414a;

namespace wlan::brcmfmac {

// This test simply starts a scan while another is in progress. We expect that the second scan will
// be rejected and the first scan will complete successfully.
TEST_F(SimTest, ScanWhileScanning) {
  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;
  StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc);

  SCHEDULE_CALL(zx::msec(10), &SimInterface::StartScan, &client_ifc, kFirstScanId, false);
  SCHEDULE_CALL(zx::msec(100), &SimInterface::StartScan, &client_ifc, kSecondScanId, false);

  env_->Run();

  // Verify that first scan completed successfully
  auto first_result = client_ifc.ScanResultCode(kFirstScanId);
  EXPECT_NE(first_result, std::nullopt);
  EXPECT_EQ(*first_result, WLAN_SCAN_RESULT_SUCCESS);

  // Verify that second scan completed, but was not successful
  auto second_result = client_ifc.ScanResultCode(kSecondScanId);
  EXPECT_NE(second_result, std::nullopt);
  EXPECT_NE(*second_result, WLAN_SCAN_RESULT_SUCCESS);
}

}  // namespace wlan::brcmfmac
