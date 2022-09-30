// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To test PHY device callback functions.

#include <lib/sync/cpp/completion.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>

#include <iterator>

#include <zxtest/zxtest.h>

#include "fidl/fuchsia.wlan.wlanphyimpl/cpp/wire_types.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/banjo/common.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/mvm-mlme.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/wlanphy-impl-device.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/fake-ucode-test.h"

namespace wlan::testing {
namespace {

constexpr size_t kListenInterval = 100;
constexpr zx_handle_t kDummyMlmeChannel = 73939133;  // An arbitrary value not ZX_HANDLE_INVALID

class WlanphyImplDeviceTest : public FakeUcodeTest {
 public:
  WlanphyImplDeviceTest()
      : FakeUcodeTest({IWL_UCODE_TLV_CAPA_LAR_SUPPORT}, {IWL_UCODE_TLV_API_WIFI_MCC_UPDATE}),
        mvmvif_sta_{
            .mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans()),
            .mac_role = WLAN_MAC_ROLE_CLIENT,
            .bss_conf =
                {
                    .beacon_int = kListenInterval,
                },
        } {
    device_ = sim_trans_.sim_device();

    // Create the FIDL endpoints, bind the client end to the test class, and the server end to
    // wlan::iwlwifi::WlanphyImplDevice class.
    auto endpoints = fdf::CreateEndpoints<fuchsia_wlan_wlanphyimpl::WlanphyImpl>();
    ASSERT_FALSE(endpoints.is_error());

    // Create a dispatcher to wait on the runtime channel.
    auto dispatcher = fdf::Dispatcher::Create(0, "iwlwifi-test",
                                              [&](fdf_dispatcher_t*) { completion_.Signal(); });

    ASSERT_FALSE(dispatcher.is_error());

    client_dispatcher_ = *std::move(dispatcher);

    client_ = fdf::WireSharedClient<fuchsia_wlan_wlanphyimpl::WlanphyImpl>(
        std::move(endpoints->client), client_dispatcher_.get());

    device_->DdkServiceConnect(
        fidl::DiscoverableProtocolName<fuchsia_wlan_wlanphyimpl::WlanphyImpl>,
        endpoints->server.TakeHandle());

    // Create test arena.
    constexpr uint32_t kTag = 'TEST';

    test_arena_ = fdf::Arena(kTag);
  }

  ~WlanphyImplDeviceTest() {
    client_dispatcher_.ShutdownAsync();
    completion_.Wait();
  }

  zx_status_t CreateIface(fuchsia_wlan_common::WlanMacRole role, uint16_t* iface_id_out) {
    auto ch = ::zx::channel(kDummyMlmeChannel);
    static fidl::Arena arena;
    auto builder = fuchsia_wlan_wlanphyimpl::wire::WlanphyImplCreateIfaceRequest::Builder(arena);
    builder.role(role);
    builder.mlme_channel(std::move(ch));

    auto result = client_.sync().buffer(test_arena_)->CreateIface(builder.Build());
    EXPECT_TRUE(result.ok());
    if (result->is_error()) {
      // Returns an invalid iface id.
      *iface_id_out = MAX_NUM_MVMVIF;
      return result->error_value();
    }

    *iface_id_out = result->value()->iface_id();
    return ZX_OK;
  }

  zx_status_t DestroyIface(uint16_t iface_id) {
    static fidl::Arena arena;
    auto builder = fuchsia_wlan_wlanphyimpl::wire::WlanphyImplDestroyIfaceRequest::Builder(arena);
    builder.iface_id(iface_id);

    auto result = client_.sync().buffer(test_arena_)->DestroyIface(builder.Build());
    EXPECT_TRUE(result.ok());
    if (result->is_error()) {
      return result->error_value();
    }

    return ZX_OK;
  }

 protected:
  struct iwl_mvm_vif mvmvif_sta_;  // The mvm_vif settings for station role.
  wlan::iwlwifi::WlanphyImplDevice* device_;

  fdf::WireSharedClient<fuchsia_wlan_wlanphyimpl::WlanphyImpl> client_;
  fdf::Dispatcher client_dispatcher_;
  fdf::Arena test_arena_;
  libsync::Completion completion_;
};

/////////////////////////////////////       PHY       //////////////////////////////////////////////

TEST_F(WlanphyImplDeviceTest, PhyGetSupportedMacRoles) {
  auto result = client_.sync().buffer(test_arena_)->GetSupportedMacRoles();
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->is_error());
  EXPECT_EQ(result->value()->supported_mac_roles.count(), 1);
  EXPECT_EQ(result->value()->supported_mac_roles.data()[0],
            fuchsia_wlan_common::WlanMacRole::kClient);
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

TEST_F(WlanphyImplDeviceTest, CreateIfaceNegativeTest) {
  static fidl::Arena arena;

  // Both role and channel not populated.
  {
    auto builder = fuchsia_wlan_wlanphyimpl::wire::WlanphyImplCreateIfaceRequest::Builder(arena);
    auto result = client_.sync().buffer(test_arena_)->CreateIface(builder.Build());
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, result->error_value());
  }

  // Role is set, but not channel.
  {
    auto builder = fuchsia_wlan_wlanphyimpl::wire::WlanphyImplCreateIfaceRequest::Builder(arena);
    builder.role(fuchsia_wlan_common::wire::WlanMacRole::kClient);
    auto result = client_.sync().buffer(test_arena_)->CreateIface(builder.Build());
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result->is_error());
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, result->error_value());
  }

  // Channel is set, but not the role.
  {
    auto ch = ::zx::channel(kDummyMlmeChannel);
    auto builder = fuchsia_wlan_wlanphyimpl::wire::WlanphyImplCreateIfaceRequest::Builder(arena);
    builder.mlme_channel(std::move(ch));
    auto result = client_.sync().buffer(test_arena_)->CreateIface(builder.Build());
    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(result->is_error());
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, result->error_value());
  }
}

TEST_F(WlanphyImplDeviceTest, DestroyIfaceNegativeTest) {
  static fidl::Arena arena;

  // ifaceid not populated.
  auto builder = fuchsia_wlan_wlanphyimpl::wire::WlanphyImplDestroyIfaceRequest::Builder(arena);
  auto result = client_.sync().buffer(test_arena_)->DestroyIface(builder.Build());
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, result->error_value());
}

TEST_F(WlanphyImplDeviceTest, PhyCreateDestroySingleInterface) {
  uint16_t iface_id;

  // Test invalid inputs
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, DestroyIface(MAX_NUM_MVMVIF));
  EXPECT_EQ(ZX_ERR_NOT_FOUND, DestroyIface(0));  // hasn't been added yet.

  // To verify the internal state of MVM driver.
  struct iwl_mvm* mvm = iwl_trans_get_mvm(sim_trans_.iwl_trans());

  // Add interface
  EXPECT_EQ(ZX_OK, CreateIface(fuchsia_wlan_common::wire::WlanMacRole::kClient, &iface_id));
  ASSERT_EQ(iface_id, 0);  // the first interface should have id 0.
  struct iwl_mvm_vif* mvmvif = mvm->mvmvif[0];
  ASSERT_NE(mvmvif, nullptr);
  ASSERT_EQ(mvmvif->mac_role,
            static_cast<wlan_mac_role_t>(fuchsia_wlan_common::wire::WlanMacRole::kClient));
  // Count includes phy device in addition to the newly created mac device.
  ASSERT_EQ(fake_parent_->descendant_count(), 2);
  device_->parent()->GetLatestChild()->InitOp();

  // Remove interface
  EXPECT_EQ(ZX_OK, DestroyIface(0));
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[0], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 1);

  mock_ddk::ReleaseFlaggedDevices(device_->zxdev());
}  // namespace

TEST_F(WlanphyImplDeviceTest, PhyCreateDestroyMultipleInterfaces) {
  struct iwl_trans* iwl_trans = sim_trans_.iwl_trans();
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);  // To verify the internal state of MVM
  uint16_t iface_id;

  // Add 1st interface
  EXPECT_EQ(ZX_OK, CreateIface(fuchsia_wlan_common::wire::WlanMacRole::kClient, &iface_id));
  ASSERT_EQ(iface_id, 0);
  ASSERT_NE(mvm->mvmvif[0], nullptr);
  ASSERT_EQ(mvm->mvmvif[0]->mac_role, WLAN_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 2);
  device_->parent()->GetLatestChild()->InitOp();

  // Add 2nd interface
  EXPECT_EQ(ZX_OK, CreateIface(fuchsia_wlan_common::wire::WlanMacRole::kClient, &iface_id));
  ASSERT_EQ(iface_id, 1);
  ASSERT_NE(mvm->mvmvif[1], nullptr);
  ASSERT_EQ(mvm->mvmvif[1]->mac_role, WLAN_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 3);
  device_->parent()->GetLatestChild()->InitOp();

  // Add 3rd interface
  EXPECT_EQ(ZX_OK, CreateIface(fuchsia_wlan_common::wire::WlanMacRole::kClient, &iface_id));
  ASSERT_EQ(iface_id, 2);
  ASSERT_NE(mvm->mvmvif[2], nullptr);
  ASSERT_EQ(mvm->mvmvif[2]->mac_role, WLAN_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 4);
  device_->parent()->GetLatestChild()->InitOp();

  // Remove the 2nd interface
  EXPECT_EQ(ZX_OK, DestroyIface(1));
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[1], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 3);

  // Add a new interface and it should be the 2nd one.
  EXPECT_EQ(ZX_OK, CreateIface(fuchsia_wlan_common::wire::WlanMacRole::kClient, &iface_id));
  ASSERT_EQ(iface_id, 1);
  ASSERT_NE(mvm->mvmvif[1], nullptr);
  ASSERT_EQ(mvm->mvmvif[1]->mac_role, WLAN_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 4);
  device_->parent()->GetLatestChild()->InitOp();

  // Add 4th interface
  EXPECT_EQ(ZX_OK, CreateIface(fuchsia_wlan_common::wire::WlanMacRole::kClient, &iface_id));
  ASSERT_EQ(iface_id, 3);
  ASSERT_NE(mvm->mvmvif[3], nullptr);
  ASSERT_EQ(mvm->mvmvif[3]->mac_role, WLAN_MAC_ROLE_CLIENT);
  ASSERT_EQ(fake_parent_->descendant_count(), 5);
  device_->parent()->GetLatestChild()->InitOp();

  // Add 5th interface and it should fail
  EXPECT_EQ(ZX_ERR_NO_RESOURCES,
            CreateIface(fuchsia_wlan_common::wire::WlanMacRole::kClient, &iface_id));
  ASSERT_EQ(fake_parent_->descendant_count(), 5);

  // Remove the 2nd interface
  EXPECT_EQ(ZX_OK, DestroyIface(1));
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[1], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 4);

  // Remove the 3rd interface
  EXPECT_EQ(ZX_OK, DestroyIface(2));
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[2], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 3);

  // Remove the 4th interface
  EXPECT_EQ(ZX_OK, DestroyIface(3));
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[3], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 2);

  // Remove the 1st interface
  EXPECT_EQ(ZX_OK, DestroyIface(0));
  mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  ASSERT_EQ(mvm->mvmvif[0], nullptr);
  ASSERT_EQ(fake_parent_->descendant_count(), 1);

  // Remove the 1st interface again and it should fail.
  EXPECT_EQ(ZX_ERR_NOT_FOUND, DestroyIface(0));
  ASSERT_EQ(fake_parent_->descendant_count(), 1);
}

TEST_F(WlanphyImplDeviceTest, GetCountry) {
  auto result = client_.sync().buffer(test_arena_)->GetCountry();
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->is_error());
  auto& country = result->value()->country;
  EXPECT_EQ('Z', country.alpha2().data()[0]);
  EXPECT_EQ('Z', country.alpha2().data()[1]);
}

TEST_F(WlanphyImplDeviceTest, SetCountry) {
  auto country = fuchsia_wlan_wlanphyimpl::wire::WlanphyCountry::WithAlpha2({'U', 'S'});
  auto result = client_.sync().buffer(test_arena_)->SetCountry(country);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, result->error_value());
}

TEST_F(WlanphyImplDeviceTest, ClearCountry) {
  auto result = client_.sync().buffer(test_arena_)->ClearCountry();
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, result->error_value());
}

TEST_F(WlanphyImplDeviceTest, SetPsMode) {
  static fidl::Arena arena;
  auto builder = fuchsia_wlan_wlanphyimpl::wire::WlanphyImplSetPsModeRequest::Builder(arena);
  auto result = client_.sync().buffer(test_arena_)->SetPsMode(builder.Build());
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, result->error_value());
}

TEST_F(WlanphyImplDeviceTest, GetPsMode) {
  auto result = client_.sync().buffer(test_arena_)->GetPsMode();
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, result->error_value());
}

}  // namespace
}  // namespace wlan::testing
