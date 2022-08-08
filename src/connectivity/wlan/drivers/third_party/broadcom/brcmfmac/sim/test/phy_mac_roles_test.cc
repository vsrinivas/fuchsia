// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-device/device.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/wlan_interface.h"

namespace wlan::brcmfmac {

class PhyMacRolesTest : public SimTest {
 public:
  PhyMacRolesTest() = default;
  void Init();
};

void PhyMacRolesTest::Init() { ASSERT_EQ(SimTest::Init(), ZX_OK); }

TEST_F(PhyMacRolesTest, VerifyMacRoles) {
  Init();
  auto result = client_.sync().buffer(test_arena_)->GetSupportedMacRoles();
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->is_error());
  EXPECT_EQ(result->value()->supported_mac_roles.count(), 2u);
  EXPECT_EQ(result->value()->supported_mac_roles.data()[0],
            fuchsia_wlan_common::wire::WlanMacRole::kClient);
  EXPECT_EQ(result->value()->supported_mac_roles.data()[1],
            fuchsia_wlan_common::wire::WlanMacRole::kAp);
}

}  // namespace wlan::brcmfmac
