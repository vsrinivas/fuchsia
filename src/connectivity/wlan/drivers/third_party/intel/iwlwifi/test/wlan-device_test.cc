// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To test PHY and MAC device callback functions.

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/wlan-device.h"
}

#include <stdio.h>
#include <zircon/syscalls.h>

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
  wlanmac_ifc_t ifc;
  zx_handle_t sme_channel;

  // Test input null pointers
  ASSERT_EQ(wlanmac_ops.start(nullptr, &ifc, &sme_channel, nullptr), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, nullptr, &sme_channel, nullptr), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, &ifc, nullptr, nullptr), ZX_ERR_INVALID_ARGS);
}

TEST_F(WlanDeviceTest, MacStartSmeChannel) {
  wlanmac_ifc_t ifc;
  zx_handle_t sme_channel;

  // The normal case. A channel will be transferred to MLME.
  constexpr zx_handle_t from_devmgr = 73939133;  // An arbitrary value not ZX_HANDLE_INVALID.
  mvmvif_sta_.sme_channel = from_devmgr;
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, &ifc, &sme_channel, nullptr), ZX_OK);
  ASSERT_EQ(sme_channel, from_devmgr);                    // The channel handle is returned.
  ASSERT_EQ(mvmvif_sta_.sme_channel, ZX_HANDLE_INVALID);  // Driver no longer holds the ownership.

  // Since the driver no longer owns the handle, the start should fail.
  ASSERT_EQ(wlanmac_ops.start(&mvmvif_sta_, &ifc, &sme_channel, nullptr), ZX_ERR_ALREADY_BOUND);
}

TEST_F(WlanDeviceTest, MacRelease) {
  // Allocate an instance so that we can free that in mac_release().
  struct iwl_mvm_vif* mvmvif =
      reinterpret_cast<struct iwl_mvm_vif*>(calloc(1, sizeof(struct iwl_mvm_vif)));

  // Create a channel. Let this test case holds one end while driver holds the other end.
  char dummy[1];
  zx_handle_t case_end;
  ASSERT_EQ(zx_channel_create(0 /* option */, &case_end, &mvmvif->sme_channel), ZX_OK);
  ASSERT_EQ(zx_channel_write(case_end, 0 /* option */, dummy, sizeof(dummy), nullptr, 0), ZX_OK);

  // Call release and the sme channel should be closed so that we will get a peer-close error while
  // trying to write any data to it.
  device_mac_ops.release(mvmvif);
  ASSERT_EQ(zx_channel_write(case_end, 0 /* option */, dummy, sizeof(dummy), nullptr, 0),
            ZX_ERR_PEER_CLOSED);
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
