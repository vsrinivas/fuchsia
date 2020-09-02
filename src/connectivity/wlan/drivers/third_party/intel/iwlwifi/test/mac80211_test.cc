// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used to test mvm/mac80211.c

#include <lib/mock-function/mock-function.h>
#include <stdio.h>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"
#include "zircon/system/ulib/fbl/include/fbl/auto_lock.h"

namespace wlan::testing {
namespace {

class Mac80211Test : public SingleApTest {
 public:
  Mac80211Test() { mvm_ = iwl_trans_get_mvm(sim_trans_.iwl_trans()); }
  ~Mac80211Test() {}

 protected:
  struct iwl_mvm* mvm_;
};

// Normal case: add an interface, then delete it.
TEST_F(Mac80211Test, AddThenRemove) {
  struct iwl_mvm_vif mvmvif = {
      .mvm = mvm_,
      .mac_role = WLAN_INFO_MAC_ROLE_CLIENT,
  };

  ASSERT_OK(iwl_mvm_mac_add_interface(&mvmvif));
  // Already existing
  ASSERT_EQ(ZX_ERR_IO, iwl_mvm_mac_add_interface(&mvmvif));

  // Check internal variables
  EXPECT_EQ(1, mvm_->vif_count);

  // Expect success.
  ASSERT_OK(iwl_mvm_mac_remove_interface(&mvmvif));

  // Removed so expect error
  ASSERT_EQ(ZX_ERR_IO, iwl_mvm_mac_remove_interface(&mvmvif));

  // Check internal variables
  EXPECT_EQ(0, mvm_->vif_count);
}

// Add multiple interfaces sequentially and expect we can remove them.
TEST_F(Mac80211Test, MultipleAddsRemoves) {
  struct iwl_mvm_vif mvmvif[] = {
      {
          .mvm = mvm_,
          .mac_role = WLAN_INFO_MAC_ROLE_CLIENT,
      },
      {
          .mvm = mvm_,
          .mac_role = WLAN_INFO_MAC_ROLE_CLIENT,
      },
      {
          .mvm = mvm_,
          .mac_role = WLAN_INFO_MAC_ROLE_CLIENT,
      },
  };

  size_t mvmvif_count = ARRAY_SIZE(mvmvif);
  for (size_t i = 0; i < mvmvif_count; ++i) {
    ASSERT_OK(iwl_mvm_mac_add_interface(&mvmvif[i]));

    // Check internal variables
    EXPECT_EQ(i + 1, mvm_->vif_count);
  }

  for (size_t i = 0; i < mvmvif_count; ++i) {
    // Expect success.
    ASSERT_OK(iwl_mvm_mac_remove_interface(&mvmvif[i]));

    // Check internal variables
    EXPECT_EQ(mvmvif_count - i - 1, mvm_->vif_count);
  }
}

TEST_F(Mac80211Test, ChanCtxSingle) {
  wlan_channel_t chandef = {
      // any arbitrary values
      .primary = 6,
  };
  uint16_t phy_ctxt_id;
  ASSERT_EQ(ZX_OK, iwl_mvm_add_chanctx(mvm_, &chandef, &phy_ctxt_id));
  ASSERT_EQ(0, phy_ctxt_id);
  struct iwl_mvm_phy_ctxt* phy_ctxt = &mvm_->phy_ctxts[phy_ctxt_id];
  ASSERT_NE(0, phy_ctxt->ref);
  EXPECT_EQ(6, phy_ctxt->chandef.primary);

  wlan_channel_t new_def = {
      .primary = 3,
  };
  iwl_mvm_change_chanctx(mvm_, phy_ctxt_id, &new_def);
  EXPECT_EQ(3, phy_ctxt->chandef.primary);

  iwl_mvm_remove_chanctx(mvm_, phy_ctxt_id);
  EXPECT_EQ(0, phy_ctxt->ref);
}

TEST_F(Mac80211Test, ChanCtxMultiple) {
  wlan_channel_t chandef = {
      // any arbitrary values
      .primary = 44,
  };
  uint16_t phy_ctxt_id_0;
  uint16_t phy_ctxt_id_1;
  uint16_t phy_ctxt_id_2;

  ASSERT_EQ(ZX_OK, iwl_mvm_add_chanctx(mvm_, &chandef, &phy_ctxt_id_0));
  ASSERT_EQ(0, phy_ctxt_id_0);

  ASSERT_EQ(ZX_OK, iwl_mvm_add_chanctx(mvm_, &chandef, &phy_ctxt_id_1));
  ASSERT_EQ(1, phy_ctxt_id_1);

  iwl_mvm_remove_chanctx(mvm_, phy_ctxt_id_0);

  ASSERT_EQ(ZX_OK, iwl_mvm_add_chanctx(mvm_, &chandef, &phy_ctxt_id_2));
  ASSERT_EQ(0, phy_ctxt_id_2);

  ASSERT_EQ(ZX_OK, iwl_mvm_remove_chanctx(mvm_, phy_ctxt_id_2));
  ASSERT_EQ(ZX_OK, iwl_mvm_remove_chanctx(mvm_, phy_ctxt_id_1));
  ASSERT_EQ(ZX_ERR_BAD_STATE, iwl_mvm_remove_chanctx(mvm_, phy_ctxt_id_0));  // removed above
}

// Test the normal usage.
//
TEST_F(Mac80211Test, MvmSlotNormalCase) {
  fbl::AutoLock auto_lock(&mvm_->mutex);
  int index;
  struct iwl_mvm_vif mvmvif = {};  // an instance to bind
  zx_status_t ret;

  // Fill up all mvmvif slots and expect okay
  for (size_t i = 0; i < MAX_NUM_MVMVIF; i++) {
    ret = iwl_mvm_find_free_mvmvif_slot(mvm_, &index);
    ASSERT_EQ(ret, ZX_OK);
    ASSERT_EQ(i, index);

    // First bind is okay
    ret = iwl_mvm_bind_mvmvif(mvm_, index, &mvmvif);
    ASSERT_EQ(ret, ZX_OK);

    // A second bind is prohibited.
    ret = iwl_mvm_bind_mvmvif(mvm_, index, &mvmvif);
    ASSERT_EQ(ret, ZX_ERR_ALREADY_EXISTS);
  }

  // One more is not accepted.
  ret = iwl_mvm_find_free_mvmvif_slot(mvm_, &index);
  ASSERT_EQ(ret, ZX_ERR_NO_RESOURCES);
}

// Bind / unbind test.
//
TEST_F(Mac80211Test, MvmSlotBindUnbind) {
  fbl::AutoLock auto_lock(&mvm_->mutex);
  int index;
  struct iwl_mvm_vif mvmvif = {};  // an instance to bind
  zx_status_t ret;

  // re-consider the test case if the slot number is changed.
  ASSERT_EQ(MAX_NUM_MVMVIF, 4);

  // First occupy the index 0, 1, 3.
  ret = iwl_mvm_bind_mvmvif(mvm_, 0, &mvmvif);
  ASSERT_EQ(ret, ZX_OK);
  ret = iwl_mvm_bind_mvmvif(mvm_, 1, &mvmvif);
  ASSERT_EQ(ret, ZX_OK);
  ret = iwl_mvm_bind_mvmvif(mvm_, 3, &mvmvif);
  ASSERT_EQ(ret, ZX_OK);

  // Expect the free one is 2.
  ret = iwl_mvm_find_free_mvmvif_slot(mvm_, &index);
  ASSERT_EQ(ret, ZX_OK);
  ASSERT_EQ(index, 2);
  ret = iwl_mvm_bind_mvmvif(mvm_, 2, &mvmvif);
  ASSERT_EQ(ret, ZX_OK);

  // No more space.
  ret = iwl_mvm_find_free_mvmvif_slot(mvm_, &index);
  ASSERT_EQ(ret, ZX_ERR_NO_RESOURCES);

  // Release the slot 1
  iwl_mvm_unbind_mvmvif(mvm_, 1);

  // The available slot should be slot 1
  ret = iwl_mvm_find_free_mvmvif_slot(mvm_, &index);
  ASSERT_EQ(ret, ZX_OK);
  ASSERT_EQ(index, 1);
}

}  // namespace
}  // namespace wlan::testing
