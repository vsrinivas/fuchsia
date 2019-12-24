// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used to test mvm/mac-ctxt.c

#include <lib/mock-function/mock-function.h>
#include <stdio.h>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"

namespace wlan::testing {
namespace {

class MacContextTest : public SingleApTest {
 public:
  MacContextTest() { mvm_ = iwl_trans_get_mvm(sim_trans_.iwl_trans()); }
  ~MacContextTest() {}

 protected:
  struct iwl_mvm* mvm_;
};

TEST_F(MacContextTest, Init) {
  struct iwl_mvm_vif mvmvif = {};
  struct ieee80211_vif vif = {
      .drv_priv = &mvmvif,
  };
  ASSERT_OK(iwl_mvm_mac_ctxt_init(mvm_, &vif));
}

TEST_F(MacContextTest, AddModifyRemove) {
  struct iwl_mvm_vif mvmvif = {};
  struct ieee80211_vif vif = {
      .type = WLAN_INFO_MAC_ROLE_CLIENT,
      .bss_conf =
          {
              .chandef =
                  {
                      .primary = 7,
                      .cbw = WLAN_CHANNEL_BANDWIDTH__20,
                      .secondary80 = 0,
                  },
          },
      .addr = {},
      .drv_priv = &mvmvif,
  };
  ASSERT_OK(iwl_mvm_mac_ctxt_init(mvm_, &vif));

  ASSERT_OK(iwl_mvm_mac_ctxt_add(mvm_, &vif));
  // Already existing
  ASSERT_EQ(ZX_ERR_IO, iwl_mvm_mac_ctxt_add(mvm_, &vif));

  // Expect success for modify and remove
  ASSERT_OK(iwl_mvm_mac_ctxt_changed(mvm_, &vif, false, nullptr));
  ASSERT_OK(iwl_mvm_mac_ctxt_remove(mvm_, &vif));

  // Removed so expect error
  ASSERT_EQ(ZX_ERR_IO, iwl_mvm_mac_ctxt_changed(mvm_, &vif, false, nullptr));
  ASSERT_EQ(ZX_ERR_IO, iwl_mvm_mac_ctxt_remove(mvm_, &vif));
}

}  // namespace
}  // namespace wlan::testing
