// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan::brcmfmac {

void VerifySetAllmulti(SimDevice* device, SimInterface& ifc, bool enable) {
  EXPECT_EQ(ifc.SetMulticastPromisc(enable), ZX_OK);

  brcmf_simdev* sim = device->GetSim();
  struct brcmf_if* ifp = brcmf_get_ifp(sim->drvr, ifc.iface_id_);

  uint32_t allmulti_value;
  zx_status_t status = brcmf_fil_iovar_int_get(ifp, "allmulti", &allmulti_value, nullptr);
  EXPECT_EQ(status, ZX_OK);

  EXPECT_EQ(allmulti_value, (uint32_t)enable);
}

// Verify that "allmulti" is set when brcmf_if_set_multicast_promisc() is called for both ifaces.
TEST_F(SimTest, SetMulticastPromisc) {
  ASSERT_EQ(Init(), ZX_OK);

  SimInterface client_ifc;
  SimInterface ap_ifc;
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc), ZX_OK);
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_AP, &ap_ifc), ZX_OK);

  VerifySetAllmulti(device_, client_ifc, true);
  VerifySetAllmulti(device_, client_ifc, false);
  VerifySetAllmulti(device_, ap_ifc, true);
  VerifySetAllmulti(device_, ap_ifc, false);

  DeleteInterface(&client_ifc);
  DeleteInterface(&ap_ifc);
}

}  // namespace wlan::brcmfmac
