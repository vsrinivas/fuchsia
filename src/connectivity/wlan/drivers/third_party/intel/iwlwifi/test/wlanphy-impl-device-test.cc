// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To test PHY device callback functions.

#include <fuchsia/wlan/common/cpp/banjo.h>
#include <fuchsia/wlan/internal/cpp/banjo.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>

#include <iterator>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/mvm-mlme.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/wlanphy-impl-device.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"

namespace wlan::testing {
namespace {

constexpr size_t kListenInterval = 100;
constexpr zx_handle_t kDummyMlmeChannel = 73939133;  // An arbitrary value not ZX_HANDLE_INVALID

class WlanphyImplDeviceTest : public SingleApTest {
 public:
  WlanphyImplDeviceTest()
      : mvmvif_sta_{
            .mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans()),
            .mac_role = WLAN_MAC_ROLE_CLIENT,
            .bss_conf =
                {
                    .beacon_int = kListenInterval,
                },
        } {
    device_ = sim_trans_.sim_device();
  }
  ~WlanphyImplDeviceTest() {}

 protected:
  struct iwl_mvm_vif mvmvif_sta_;  // The mvm_vif settings for station role.
  wlan::iwlwifi::WlanphyImplDevice* device_;
};

/////////////////////////////////////       PHY       //////////////////////////////////////////////

TEST_F(WlanphyImplDeviceTest, PhyQuery) {
  wlanphy_impl_info_t info = {};

  // Test input null pointers
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, device_->WlanphyImplQuery(nullptr));

  // Normal case
  ASSERT_EQ(ZX_OK, device_->WlanphyImplQuery(&info));
  EXPECT_EQ(1U, info.supported_mac_roles_count);
  EXPECT_EQ(WLAN_MAC_ROLE_CLIENT, info.supported_mac_roles_list[0]);
  free(static_cast<void*>(const_cast<wlan_mac_role_t*>(info.supported_mac_roles_list)));
}

TEST_F(WlanphyImplDeviceTest, PhyPartialCreateCleanup) {
  wlanphy_impl_create_iface_req_t req = {
      .role = WLAN_MAC_ROLE_CLIENT,
      .mlme_channel = kDummyMlmeChannel,
  };
  uint16_t iface_id;
  struct iwl_trans* iwl_trans = sim_trans_.iwl_trans();

  // Test input null pointers
  ASSERT_OK(phy_create_iface(iwl_trans, &req, &iface_id));

  // Ensure mvmvif got created and indexed.
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);
  ASSERT_NOT_NULL(mvm->mvmvif[iface_id]);

  // Ensure partial create failure removes it from the index.
  phy_create_iface_undo(iwl_trans, iface_id);
  ASSERT_NULL(mvm->mvmvif[iface_id]);
}

TEST_F(WlanphyImplDeviceTest, PhyCreateDestroySingleInterface) {
  wlanphy_impl_create_iface_req_t req = {
      .role = WLAN_MAC_ROLE_CLIENT,
      .mlme_channel = kDummyMlmeChannel,
  };
  uint16_t iface_id;

  // Test input null pointers
  ASSERT_EQ(device_->WlanphyImplCreateIface(nullptr, &iface_id), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, nullptr), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(device_->WlanphyImplCreateIface(nullptr, nullptr), ZX_ERR_INVALID_ARGS);

  // Test invalid inputs
  ASSERT_EQ(device_->WlanphyImplDestroyIface(MAX_NUM_MVMVIF), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(device_->WlanphyImplDestroyIface(0), ZX_ERR_NOT_FOUND);  // hasn't been added yet.

  // To verify the internal state of MVM driver.
  struct iwl_mvm* mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans());

  // Add interface
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 0);  // the first interface should have id 0.
  struct iwl_mvm_vif* mvmvif = mvm->mvmvif[iface_id];
  ASSERT_NE(mvmvif, nullptr);
  ASSERT_EQ(mvmvif->mac_role, WLAN_MAC_ROLE_CLIENT);
  // Count includes phy device in addition to the newly created mac device.
  ASSERT_EQ(fake_parent_->descendant_count(), 2);
  device_->parent()->GetLatestChild()->InitOp();

  // Remove interface
  ASSERT_EQ(device_->WlanphyImplDestroyIface(0), ZX_OK);
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 1);
}

TEST_F(WlanphyImplDeviceTest, PhyCreateDestroyMultipleInterfaces) {
  wlanphy_impl_create_iface_req_t req = {
      .role = WLAN_MAC_ROLE_CLIENT,
      .mlme_channel = kDummyMlmeChannel,
  };
  uint16_t iface_id;
  struct iwl_trans* iwl_trans = sim_trans_.iwl_trans();
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);  // To verify the internal state of MVM driver

  // Add 1st interface
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 0);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 2);
  device_->parent()->GetLatestChild()->InitOp();

  // Add 2nd interface
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 1);
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 3);
  device_->parent()->GetLatestChild()->InitOp();

  // Add 3rd interface
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 2);
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 4);
  device_->parent()->GetLatestChild()->InitOp();

  // Remove the 2nd interface
  ASSERT_EQ(device_->WlanphyImplDestroyIface(1), ZX_OK);
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[1], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 3);

  // Add a new interface and it should be the 2nd one.
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 1);
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 4);
  device_->parent()->GetLatestChild()->InitOp();

  // Add 4th interface
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_OK);
  ASSERT_EQ(iface_id, 3);
  ASSERT_NE(mvm->mvmvif[iface_id], nullptr);
  ASSERT_EQ(mvm->mvmvif[iface_id]->mac_role, WLAN_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 5);
  device_->parent()->GetLatestChild()->InitOp();

  // Add 5th interface and it should fail
  ASSERT_EQ(device_->WlanphyImplCreateIface(&req, &iface_id), ZX_ERR_NO_RESOURCES);
  ASSERT_EQ(fake_parent_->descendant_count(), 5);

  // Remove the 2nd interface
  ASSERT_EQ(device_->WlanphyImplDestroyIface(1), ZX_OK);
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[1], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 4);

  // Remove the 3rd interface
  ASSERT_EQ(device_->WlanphyImplDestroyIface(2), ZX_OK);
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[2], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 3);

  // Remove the 4th interface
  ASSERT_EQ(device_->WlanphyImplDestroyIface(3), ZX_OK);
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[3], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 2);

  // Remove the 1st interface
  ASSERT_EQ(device_->WlanphyImplDestroyIface(0), ZX_OK);
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[0], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 1);

  // Remove the 1st interface again and it should fail.
  ASSERT_EQ(device_->WlanphyImplDestroyIface(0), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(fake_parent_->descendant_count(), 1);
}

}  // namespace
}  // namespace wlan::testing
