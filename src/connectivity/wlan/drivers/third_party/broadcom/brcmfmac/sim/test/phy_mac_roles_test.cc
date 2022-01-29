// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/wlan_interface.h"

namespace wlan::brcmfmac {

using namespace testing;

class PhyMacRolesTest : public SimTest {
 public:
  PhyMacRolesTest() = default;
  void Init();
};

void PhyMacRolesTest::Init() { ASSERT_EQ(SimTest::Init(), ZX_OK); }

TEST_F(PhyMacRolesTest, VerifyMacRoles) {
  Init();
  wlan_mac_role_t supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES] = {};
  uint8_t supported_mac_roles_count = 0;

  zx_status_t status;
  status = device_->WlanphyImplGetSupportedMacRoles(supported_mac_roles_list,
                                                    &supported_mac_roles_count);
  ASSERT_EQ(status, ZX_OK);

  EXPECT_EQ(supported_mac_roles_count, 2);
  EXPECT_EQ(supported_mac_roles_list[0], WLAN_MAC_ROLE_CLIENT);
  EXPECT_EQ(supported_mac_roles_list[1], WLAN_MAC_ROLE_AP);
}

}  // namespace wlan::brcmfmac
