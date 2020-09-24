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

  static void VerifyMacRoleBitfield(wlan_info_mac_role_t mac_role_bitfield);
  static void VerifyPhyTypeBitfield(wlan_info_phy_type_t phy_type_bitfield);
  static void VerifyDriverFeatureBitfield(wlan_info_driver_feature_t driver_feature_bitfield);
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
void PhyQueryTest::VerifyMacRoleBitfield(wlan_info_mac_role_t mac_role_bitfield) {
  EXPECT_NE(0U, mac_role_bitfield & WLAN_INFO_MAC_ROLE_CLIENT);
  EXPECT_NE(0U, mac_role_bitfield & WLAN_INFO_MAC_ROLE_AP);
}

// static
void PhyQueryTest::VerifyPhyTypeBitfield(wlan_info_phy_type_t phy_type_bitfield) {
  EXPECT_NE(0U, phy_type_bitfield & WLAN_INFO_PHY_TYPE_DSSS);
  EXPECT_NE(0U, phy_type_bitfield & WLAN_INFO_PHY_TYPE_CCK);
  EXPECT_NE(0U, phy_type_bitfield & WLAN_INFO_PHY_TYPE_ERP);
  EXPECT_NE(0U, phy_type_bitfield & WLAN_INFO_PHY_TYPE_OFDM);
  EXPECT_NE(0U, phy_type_bitfield & WLAN_INFO_PHY_TYPE_HT);
  EXPECT_NE(0U, phy_type_bitfield & WLAN_INFO_PHY_TYPE_VHT);
}

// static
void PhyQueryTest::VerifyDriverFeatureBitfield(wlan_info_driver_feature_t driver_feature_bitfield) {
  // Available driver features
  EXPECT_NE(0U, driver_feature_bitfield & WLAN_INFO_DRIVER_FEATURE_DFS);
  EXPECT_NE(0U, driver_feature_bitfield & WLAN_INFO_DRIVER_FEATURE_SCAN_OFFLOAD);

  // Not available driver features
  EXPECT_EQ(0U, driver_feature_bitfield & WLAN_INFO_DRIVER_FEATURE_RATE_SELECTION);
  EXPECT_EQ(0U, driver_feature_bitfield & WLAN_INFO_DRIVER_FEATURE_SYNTH);
  EXPECT_EQ(0U, driver_feature_bitfield & WLAN_INFO_DRIVER_FEATURE_TX_STATUS_REPORT);
  EXPECT_EQ(0U, driver_feature_bitfield & WLAN_INFO_DRIVER_FEATURE_PROBE_RESP_OFFLOAD);
}

// static
void PhyQueryTest::VerifyHardwareCapabilityBitfield(
    wlan_info_hardware_capability_t hardware_capability_bitfield) {
  EXPECT_NE(0U, hardware_capability_bitfield & WLAN_INFO_HARDWARE_CAPABILITY_SHORT_PREAMBLE);
  EXPECT_NE(0U, hardware_capability_bitfield & WLAN_INFO_HARDWARE_CAPABILITY_SPECTRUM_MGMT);
  EXPECT_NE(0U, hardware_capability_bitfield & WLAN_INFO_HARDWARE_CAPABILITY_SHORT_SLOT_TIME);
  EXPECT_NE(0U, hardware_capability_bitfield & WLAN_INFO_HARDWARE_CAPABILITY_RADIO_MSMT);
  EXPECT_NE(0U,
            hardware_capability_bitfield & WLAN_INFO_HARDWARE_CAPABILITY_SIMULTANEOUS_CLIENT_AP);
}

TEST_F(PhyQueryTest, VerifyQueryData) {
  Init();
  wlanphy_impl_info_t impl_info;
  PhyQuery(&impl_info);
  wlan_info_t wlan_info = impl_info.wlan_info;

  // The Broadcom driver does not set MAC address in PHY query. See fxbug.dev/53991
  EXPECT_THAT(wlan_info.mac_addr, Each(0U));

  VerifyMacRoleBitfield(wlan_info.mac_role);
  VerifyPhyTypeBitfield(wlan_info.supported_phys);
  VerifyDriverFeatureBitfield(wlan_info.driver_features);
  VerifyHardwareCapabilityBitfield(wlan_info.caps);
}

}  // namespace wlan::brcmfmac
