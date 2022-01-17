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
};

void PhyQueryTest::Init() { ASSERT_EQ(SimTest::Init(), ZX_OK); }

void PhyQueryTest::PhyQuery(wlanphy_impl_info_t* info) {
  zx_status_t status;
  status = device_->WlanphyImplQuery(info);
  ASSERT_EQ(status, ZX_OK);
}

TEST_F(PhyQueryTest, VerifyQueryData) {
  Init();
  wlanphy_impl_info_t info;
  PhyQuery(&info);
  EXPECT_THAT(info.supported_mac_roles, FieldsAre(true, true, false));
}

}  // namespace wlan::brcmfmac
