// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To test PHY and MAC device callback functions.

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/wlan-device.h"
}

#include <stdio.h>

#include "gtest/gtest.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"

namespace wlan::testing {
namespace {

class WlanDeviceTest : public SingleApTest {
 public:
  WlanDeviceTest()
      : mvmvif_sta_{
            .mac_role = WLAN_INFO_MAC_ROLE_CLIENT,
        } {}
  ~WlanDeviceTest() {}

 protected:
  struct iwl_mvm_vif mvmvif_sta_;  // The mvm_vif settings for station role.
};

/////////////////////////////////////       MAC       //////////////////////////////////////////////

TEST_F(WlanDeviceTest, MacQuery) {
  // Test input null pointers
  uint32_t options = 0;
  void* whatever = &options;
  ASSERT_EQ(wlanmac_ops.query(nullptr, options, nullptr), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(wlanmac_ops.query(whatever, options, nullptr), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(wlanmac_ops.query(nullptr, options, reinterpret_cast<wlanmac_info*>(whatever)),
            ZX_ERR_INVALID_ARGS);

  wlanmac_info_t info;
  ASSERT_EQ(wlanmac_ops.query(&mvmvif_sta_, options, &info), ZX_OK);
  ASSERT_EQ(info.ifc_info.mac_role, WLAN_INFO_MAC_ROLE_CLIENT);
}

TEST_F(WlanDeviceTest, MacStart) {
  // Test input null pointers
  ASSERT_EQ(wlanmac_ops.start(nullptr, nullptr, nullptr, nullptr), ZX_ERR_INVALID_ARGS);
}

/////////////////////////////////////       PHY       //////////////////////////////////////////////

TEST_F(WlanDeviceTest, PhyQuery) {
  // Test input null pointers
  ASSERT_EQ(wlanphy_ops.query(nullptr, nullptr), ZX_ERR_INVALID_ARGS);

  // 'ctx' null is okay for now because the code under test is still not using that.
  wlanphy_impl_info_t info;
  ASSERT_EQ(wlanphy_ops.query(nullptr, &info), ZX_OK);
  ASSERT_EQ(info.wlan_info.mac_role, WLAN_INFO_MAC_ROLE_CLIENT);
}

}  // namespace
}  // namespace wlan::testing
