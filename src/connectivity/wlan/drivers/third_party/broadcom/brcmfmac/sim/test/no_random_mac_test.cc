// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

// Verify that we can do an active scan even if the firmware doesn't support randomized mac
// addresses.
TEST_F(SimTest, RandomMacNotSupported) {
  constexpr uint64_t kScanTxnId = 42;

  ASSERT_EQ(PreInit(), ZX_OK);

  // Force failure in the iovar used to set a random mac address
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("pfn_macaddr", ZX_ERR_IO);

  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc), ZX_OK);

  SCHEDULE_CALL(zx::sec(1), &SimInterface::StartScan, &client_ifc, kScanTxnId, true);
  env_->Run();

  auto scan_result = client_ifc.ScanResultCode(kScanTxnId);

  // Verify scan completed
  EXPECT_TRUE(scan_result);

  // Verify that scan was successful
  EXPECT_EQ(*scan_result, ZX_OK);
}

}  // namespace wlan::brcmfmac
