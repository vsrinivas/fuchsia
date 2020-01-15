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
  struct iwl_mvm_vif mvmvif = {
      .mvm = mvm_,
      .mac_role = WLAN_INFO_MAC_ROLE_CLIENT,
  };
  ASSERT_OK(iwl_mvm_mac_ctxt_init(&mvmvif));
}

TEST_F(MacContextTest, AddModifyRemove) {
  struct iwl_mvm_vif mvmvif = {
      .mvm = mvm_,
      .mac_role = WLAN_INFO_MAC_ROLE_CLIENT,
  };
  ASSERT_OK(iwl_mvm_mac_ctxt_init(&mvmvif));

  ASSERT_OK(iwl_mvm_mac_ctxt_add(&mvmvif));
  // Already existing
  ASSERT_EQ(ZX_ERR_IO, iwl_mvm_mac_ctxt_add(&mvmvif));

  // Expect success for modify and remove
  ASSERT_OK(iwl_mvm_mac_ctxt_changed(&mvmvif, false, nullptr));
  ASSERT_OK(iwl_mvm_mac_ctxt_remove(&mvmvif));

  // Removed so expect error
  ASSERT_EQ(ZX_ERR_IO, iwl_mvm_mac_ctxt_changed(&mvmvif, false, nullptr));
  ASSERT_EQ(ZX_ERR_IO, iwl_mvm_mac_ctxt_remove(&mvmvif));
}

}  // namespace
}  // namespace wlan::testing
