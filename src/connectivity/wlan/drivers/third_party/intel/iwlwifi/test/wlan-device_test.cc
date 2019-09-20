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
  WlanDeviceTest() {}
  ~WlanDeviceTest() {}
};

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
