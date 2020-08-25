// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/wlanif.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-fake-ap/sim-fake-ap.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/status_code.h"

namespace wlan::brcmfmac {

const common::MacAddr kDefaultBssid({0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc});

class MfgTest : public SimTest {
 public:
  // How many devices have been registered by the fake devhost
  uint32_t DeviceCount();

 protected:
  SimInterface client_ifc_;
  SimInterface softap_ifc_;
};

uint32_t MfgTest::DeviceCount() { return (dev_mgr_->DeviceCount()); }

// Check to make sure only one IF can be active at anytime with MFG FW.
TEST_F(MfgTest, BasicTest) {
  Init();
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);

  // SoftAP If creation should fail as Client IF has already been created.
  ASSERT_NE(StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_, std::nullopt, kDefaultBssid),
            ZX_OK);

  // Now delete the Client IF and SoftAP creation should pass
  DeleteInterface(client_ifc_);
  EXPECT_EQ(DeviceCount(), 1U);
  ASSERT_EQ(StartInterface(WLAN_INFO_MAC_ROLE_AP, &softap_ifc_, std::nullopt, kDefaultBssid),
            ZX_OK);
  // Now that SoftAP IF is created, Client IF creation should fail
  ASSERT_NE(StartInterface(WLAN_INFO_MAC_ROLE_CLIENT, &client_ifc_), ZX_OK);
  DeleteInterface(softap_ifc_);
}

}  // namespace wlan::brcmfmac
