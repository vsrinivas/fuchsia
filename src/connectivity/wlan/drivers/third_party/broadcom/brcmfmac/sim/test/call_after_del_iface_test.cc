// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/hardware/wlan/fullmac/c/banjo.h>
#include <zircon/errors.h>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

// Verify that a firmware scan result indication after the interface is stopped does
// not cause a failure.
TEST_F(SimTest, ScanResultAfterIfaceStop) {
  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;

  ASSERT_EQ(StartInterface(WLAN_MAC_ROLE_CLIENT, &client_ifc), ZX_OK);

  client_ifc.StartScan(0, true);
  client_ifc.StopInterface();
  // The scan result will arrive after the iface is torn down.
  env_->Run(zx::sec(1));  // This should be a no-op, not a crash.

  EXPECT_EQ(DeleteInterface(&client_ifc), ZX_OK);
}

// Verify that calling WlanphyImplDestroyIface() twice will not cause a crash when the first call
// failed.
TEST_F(SimTest, DeleteIfaceTwice) {
  ASSERT_EQ(Init(), ZX_OK);

  SimInterface softap_ifc;
  ASSERT_EQ(StartInterface(WLAN_MAC_ROLE_AP, &softap_ifc), ZX_OK);

  // Inject firmware error to "interface_remove" iovar.
  brcmf_simdev* sim = device_->GetSim();
  sim->sim_fw->err_inj_.AddErrInjIovar("interface_remove", ZX_OK, BCME_ERROR, softap_ifc.iface_id_);

  EXPECT_EQ(DeleteInterface(&softap_ifc), ZX_ERR_IO_REFUSED);

  // Cancel the injected error.
  sim->sim_fw->err_inj_.DelErrInjIovar("interface_remove");

  EXPECT_EQ(DeleteInterface(&softap_ifc), ZX_OK);
}
}  // namespace wlan::brcmfmac
