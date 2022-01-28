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

class PhyQueryTest : public SimTest {
 public:
  PhyQueryTest() = default;
  void Init();
  void PhyQuery(wlanphy_impl_info_t* info);

  static void VerifyMacRoles(const mac_role_t* supported_mac_roles_list,
                             size_t supported_mac_roles_count);
  static void VerifyHardwareCapabilityBitfield(
      wlan_info_hardware_capability_t hardware_capability_bitfield);
};

void PhyQueryTest::Init() { ASSERT_EQ(SimTest::Init(), ZX_OK); }

void PhyQueryTest::PhyQuery(wlanphy_impl_info_t* info) {
  zx_status_t status;
  status = device_->WlanphyImplQuery(info);
  ASSERT_EQ(status, ZX_OK);
}

// static
void PhyQueryTest::VerifyMacRoles(const mac_role_t* supported_mac_roles_list,
                                  size_t supported_mac_roles_count) {
  EXPECT_EQ(2U, supported_mac_roles_count);
  EXPECT_EQ(MAC_ROLE_CLIENT, supported_mac_roles_list[0]);
  EXPECT_EQ(MAC_ROLE_AP, supported_mac_roles_list[1]);
}

TEST_F(PhyQueryTest, VerifyQueryData) {
  Init();
  wlanphy_impl_info_t info;
  PhyQuery(&info);

  VerifyMacRoles(info.supported_mac_roles_list, info.supported_mac_roles_count);
  free(static_cast<void*>(const_cast<mac_role_t*>(info.supported_mac_roles_list)));
}

}  // namespace wlan::brcmfmac
