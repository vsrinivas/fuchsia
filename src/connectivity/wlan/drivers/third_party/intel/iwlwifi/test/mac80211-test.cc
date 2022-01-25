// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used to test mvm/mac80211.c

#include <lib/mock-function/mock-function.h>
#include <zircon/compiler.h>

#include <iterator>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/fake-ucode-capa-test.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/mock-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"

namespace wlan::testing {
namespace {

class ClientInterfaceHelper {
 public:
  ClientInterfaceHelper(SimTransport* sim_trans) : mvmvif_{}, ap_sta_{}, txqs_ {}
  {
    mvm_ = iwl_trans_get_mvm(sim_trans->iwl_trans());

    // First find a free slot for the interface.
    mtx_lock(&mvm_->mutex);
    EXPECT_EQ(ZX_OK, iwl_mvm_find_free_mvmvif_slot(mvm_, &mvmvif_idx_));
    EXPECT_EQ(ZX_OK, iwl_mvm_bind_mvmvif(mvm_, mvmvif_idx_, &mvmvif_));
    mtx_unlock(&mvm_->mutex);

    // Initialize the interface data and add it to the mvm.
    mvmvif_.mvm = mvm_;
    mvmvif_.mac_role = WLAN_INFO_MAC_ROLE_CLIENT;
    mvmvif_.bss_conf.beacon_int = 16;
    iwl_mvm_mac_add_interface(&mvmvif_);

    // Assign the phy_ctxt in the mvm to the interface.
    wlan_channel_t chandef = {
        // any arbitrary values
        .primary = 35,
    };
    uint16_t phy_ctxt_id;
    ASSERT_EQ(ZX_OK, iwl_mvm_add_chanctx(mvm_, &chandef, &phy_ctxt_id));
    mvmvif_.phy_ctxt = &mvm_->phy_ctxts[phy_ctxt_id];

    // Assign the AP sta info.
    ASSERT_EQ(IEEE80211_TIDS_MAX + 1, std::size(ap_sta_.txq));
    for (size_t i = 0; i < std::size(ap_sta_.txq); i++) {
      ap_sta_.txq[i] = &txqs_[i];
    }
    ASSERT_EQ(ZX_OK, iwl_mvm_mac_sta_state(&mvmvif_, &ap_sta_, IWL_STA_NOTEXIST, IWL_STA_NONE));

    // Set it to associated.
    mvmvif_.bss_conf.assoc = true;
  }

  ~ClientInterfaceHelper() {
    mtx_lock(&mvm_->mutex);
    iwl_mvm_unbind_mvmvif(mvm_, mvmvif_idx_);
    mtx_unlock(&mvm_->mutex);
  }

  struct iwl_mvm* mvm() {
    return mvm_;
  }
  struct iwl_mvm_vif* mvmvif() {
    return &mvmvif_;
  }

 private:
  struct iwl_mvm* mvm_;
  // for ClientInterfaceHelper().
  struct iwl_mvm_vif mvmvif_;
  int mvmvif_idx_;
  struct iwl_mvm_sta ap_sta_;
  struct iwl_mvm_txq txqs_[IEEE80211_TIDS_MAX + 1];
};

class Mac80211Test : public SingleApTest {
 public:
  Mac80211Test() : mvm_(iwl_trans_get_mvm(sim_trans_.iwl_trans())) {}
  ~Mac80211Test() {}

 protected:
  struct iwl_mvm* mvm_;
};

class Mac80211UcodeTest : public FakeUcodeCapaTest {
 public:
  Mac80211UcodeTest() : FakeUcodeCapaTest(0, BIT(IWL_UCODE_TLV_CAPA_LAR_SUPPORT)) {}
  ~Mac80211UcodeTest() {}
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

  size_t mvmvif_count = std::size(mvmvif);
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
TEST_F(Mac80211Test, MvmSlotNormalCase) __TA_NO_THREAD_SAFETY_ANALYSIS {
  int index;
  struct iwl_mvm_vif mvmvif = {};  // an instance to bind
  zx_status_t ret;

  // Fill up all mvmvif slots and expect okay
  mtx_lock(&mvm_->mutex);
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
  mtx_unlock(&mvm_->mutex);
}

// Bind / unbind test.
//
TEST_F(Mac80211Test, MvmSlotBindUnbind) __TA_NO_THREAD_SAFETY_ANALYSIS {
  int index;
  struct iwl_mvm_vif mvmvif = {};  // an instance to bind
  zx_status_t ret;

  // re-consider the test case if the slot number is changed.
  ASSERT_EQ(MAX_NUM_MVMVIF, 4);

  // First occupy the index 0, 1, 3.
  mtx_lock(&mvm_->mutex);
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
  mtx_unlock(&mvm_->mutex);
}

class McastFilterTestWithoutIface : public Mac80211Test, public MockTrans {
 public:
  McastFilterTestWithoutIface() : mvm_(iwl_trans_get_mvm(sim_trans_.iwl_trans())) {
    BIND_TEST(mvm_->trans);
  }
  ~McastFilterTestWithoutIface() { mock_send_cmd_.VerifyAndClear(); }

  // The values we expect.  We only test few arbitrary bytes in 'addr_list' .
  //
  mock_function::MockFunction<zx_status_t,
                              uint32_t,  // hcmd->id
                              uint8_t,   // mcast_cmd->port_id
                              uint8_t,   // mcast_cmd->count
                              uint8_t,   // mcast_cmd->bssid[0]
                              uint8_t,   // mcast_cmd->addr_list[0 * ETH_ALEN + 0]
                              uint8_t,   // mcast_cmd->addr_list[1 * ETH_ALEN + 5]
                              uint8_t    // mcast_cmd->addr_list[2 * ETH_ALEN + 2]
                              >
      mock_send_cmd_;

  static zx_status_t send_cmd_wrapper(struct iwl_trans* trans, struct iwl_host_cmd* hcmd) {
    auto mcast_cmd = reinterpret_cast<const struct iwl_mcast_filter_cmd*>(hcmd->data[0]);

    auto test = GET_TEST(McastFilterTestWithoutIface, trans);
    return test->mock_send_cmd_.Call(hcmd->id, mcast_cmd->port_id, mcast_cmd->count,
                                     mcast_cmd->bssid[0], mcast_cmd->addr_list[0 * ETH_ALEN + 0],
                                     mcast_cmd->addr_list[1 * ETH_ALEN + 5],
                                     mcast_cmd->addr_list[2 * ETH_ALEN + 2]);
  }

 protected:
  struct iwl_mvm* mvm_;
};

class McastFilterTest : public McastFilterTestWithoutIface {
 public:
  McastFilterTest() : helper_(ClientInterfaceHelper(&sim_trans_)) {}
  ~McastFilterTest() {}

 protected:
  ClientInterfaceHelper helper_;
};

TEST_F(McastFilterTest, McastFilterNormal) {
  // mock function after the testing environment had been set.
  bindSendCmd(send_cmd_wrapper);

  // Test if we can configure the mcast filter.
  mock_send_cmd_.ExpectCall(ZX_OK, WIDE_ID(LONG_GROUP, MCAST_FILTER_CMD),  // hcmd->id
                            0,                                             // mcast_cmd->port_id
                            3,                                             // mcast_cmd->count
                            0x00,                                          // mcast_cmd->bssid[0]
                            0x33,  // mcast_cmd->addr_list[0 * ETH_ALEN + 0]
                            0x02,  // mcast_cmd->addr_list[1 * ETH_ALEN + 5]
                            0x5e   // mcast_cmd->addr_list[2 * ETH_ALEN + 2]
  );
  iwl_mvm_configure_filter(mvm_);

  ASSERT_NE(mvm_->mcast_filter_cmd, nullptr);

  unbindSendCmd();
}

TEST_F(McastFilterTestWithoutIface, McastFilterNoActiveInterface) {
  // mock function after the testing environment had been set.
  bindSendCmd(send_cmd_wrapper);

  // We shall expect nothing will happen because ClientInterfaceHelper() is not called and
  // no interface is created.
  iwl_mvm_configure_filter(mvm_);

  unbindSendCmd();
}

TEST_F(McastFilterTest, McastFilterAp) {
  helper_.mvmvif()->mac_role = WLAN_INFO_MAC_ROLE_AP;  // overwrite to AP.

  // mock function after the testing environment had been set.
  bindSendCmd(send_cmd_wrapper);

  // We shall expect nothing will happen because the interface is for AP.
  iwl_mvm_configure_filter(mvm_);

  unbindSendCmd();
}

class RegulatoryTest : public Mac80211UcodeTest {
 public:
  RegulatoryTest() {}
  ~RegulatoryTest() {}
};

TEST_F(RegulatoryTest, RegulatoryTestNormal) {
  ClientInterfaceHelper helper = ClientInterfaceHelper(&sim_trans_);

  bool changed;
  wlanphy_country_t country;
  auto mvm = helper.mvm();
  mtx_lock(&mvm->mutex);
  EXPECT_EQ(ZX_OK, iwl_mvm_get_regdomain(mvm, "TW", MCC_SOURCE_WIFI, &changed, &country));
  mtx_unlock(&mvm->mutex);

  EXPECT_EQ(true, changed);
  EXPECT_EQ(0x54, country.alpha2[0]);
  EXPECT_EQ(0x57, country.alpha2[1]);
  EXPECT_EQ(true, mvm->lar_regdom_set);
  EXPECT_EQ(MCC_SOURCE_WIFI, mvm->mcc_src);
}

}  // namespace
}  // namespace wlan::testing
