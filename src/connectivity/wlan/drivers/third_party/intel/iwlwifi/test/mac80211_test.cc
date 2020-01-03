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
  };
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

  ASSERT_OK(iwl_mvm_mac_add_interface(&mvmvif, &vif));
  // Already existing
  ASSERT_EQ(ZX_ERR_IO, iwl_mvm_mac_add_interface(&mvmvif, &vif));

  // Check internal variables
  EXPECT_EQ(1, mvm_->vif_count);
  EXPECT_EQ(&mvmvif, vif.drv_priv);

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
      },
      {
          .mvm = mvm_,
      },
      {
          .mvm = mvm_,
      },
  };
  struct ieee80211_vif vif[] = {
      {
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
      },
      {
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
      },
      {
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
      },
  };

  size_t mvmvif_count = ARRAY_SIZE(mvmvif);
  for (size_t i = 0; i < mvmvif_count; ++i) {
    ASSERT_OK(iwl_mvm_mac_add_interface(&mvmvif[i], &vif[i]));

    // Check internal variables
    EXPECT_EQ(i + 1, mvm_->vif_count);
    EXPECT_EQ(&mvmvif[i], vif[i].drv_priv);
  }

  for (size_t i = 0; i < mvmvif_count; ++i) {
    // Expect success.
    ASSERT_OK(iwl_mvm_mac_remove_interface(&mvmvif[i]));

    // Check internal variables
    EXPECT_EQ(mvmvif_count - i - 1, mvm_->vif_count);
  }
}

}  // namespace
}  // namespace wlan::testing
