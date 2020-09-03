// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used to test mvm/sta.c

#include <lib/mock-function/mock-function.h>
#include <stdio.h>

#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/sta.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/single-ap-test.h"

namespace wlan::testing {
namespace {

class StaTest : public SingleApTest {
 public:
  StaTest() {
    mvm_ = iwl_trans_get_mvm(sim_trans_.iwl_trans());

    // Set up pointer back to this test so that the following wrapper can call its mock funciton.
    struct trans_sim_priv* priv = IWL_TRANS_GET_TRANS_SIM(mvm_->trans);
    priv->test = this;
  }
  ~StaTest() {}

  // Expected fields.
  mock_function::MockFunction<zx_status_t,  // return value
                              uint32_t,     // cmd_id
                              size_t,       // cmd_size
                              uint8_t,      // add_modify
                              __le32,       // mac_id_n_color
                              uint8_t,      // sta_id
                              uint8_t,      // modify_mask
                              __le32,       // station_flags
                              __le32,       // station_flags_msk
                              __le16        // assoc_id
                              >
      mock_send_cmd_;

  static zx_status_t send_cmd_wrapper(struct iwl_trans* trans, struct iwl_host_cmd* host_cmd) {
    struct trans_sim_priv* priv = IWL_TRANS_GET_TRANS_SIM(trans);
    StaTest* test = reinterpret_cast<StaTest*>(priv->test);
    ZX_ASSERT(test);

    auto sta_cmd = reinterpret_cast<const struct iwl_mvm_add_sta_cmd*>(host_cmd->data[0]);
    return test->mock_send_cmd_.Call(host_cmd->id, host_cmd->len[0], sta_cmd->add_modify,
                                     sta_cmd->mac_id_n_color, sta_cmd->sta_id, sta_cmd->modify_mask,
                                     sta_cmd->station_flags, sta_cmd->station_flags_msk,
                                     sta_cmd->assoc_id);
  }

  void bind_mock_function() {
    org_send_cmd_ = sim_trans_.iwl_trans()->ops->send_cmd;
    sim_trans_.iwl_trans()->ops->send_cmd = send_cmd_wrapper;
  }

  void unbind_mock_function() { sim_trans_.iwl_trans()->ops->send_cmd = org_send_cmd_; }

 protected:
  struct iwl_mvm* mvm_;  // pointing to the mvm instance associated with this transportation.
  zx_status_t (*org_send_cmd_)(struct iwl_trans* trans, struct iwl_host_cmd* cmd);  // for backup
};

TEST_F(StaTest, DisableTx) {
  // mock function after the testing environment had been set.
  bind_mock_function();

  struct iwl_mvm_sta mvmsta = {};

  mock_send_cmd_.ExpectCall(ZX_OK,                                  // return value
                            WIDE_ID(LONG_GROUP, ADD_STA),           // cmd_id
                            sizeof(struct iwl_mvm_add_sta_cmd_v7),  // cmd_size
                            STA_MODE_MODIFY,                        // add_modify
                            0,                                      // mac_id_n_color
                            mvmsta.sta_id,                          // sta_id
                            0,                                      // modify_mask
                            cpu_to_le32(STA_FLG_DISABLE_TX),        // station_flags
                            cpu_to_le32(STA_FLG_DISABLE_TX),        // station_flags_msk
                            0);                                     // assoc_id

  iwl_mvm_sta_modify_disable_tx(mvm_, &mvmsta, true);

  unbind_mock_function();
}

TEST_F(StaTest, EnableTx) {
  // mock function after the testing environment had been set.
  bind_mock_function();

  struct iwl_mvm_sta mvmsta = {};

  mock_send_cmd_.ExpectCall(ZX_OK,                                  // return value
                            WIDE_ID(LONG_GROUP, ADD_STA),           // cmd_id
                            sizeof(struct iwl_mvm_add_sta_cmd_v7),  // cmd_size
                            STA_MODE_MODIFY,                        // add_modify
                            0,                                      // mac_id_n_color
                            mvmsta.sta_id,                          // sta_id
                            0,                                      // modify_mask
                            cpu_to_le32(0),                         // station_flags
                            cpu_to_le32(STA_FLG_DISABLE_TX),        // station_flags_msk
                            0);                                     // assoc_id

  iwl_mvm_sta_modify_disable_tx(mvm_, &mvmsta, false);

  unbind_mock_function();
}

}  // namespace
}  // namespace wlan::testing
