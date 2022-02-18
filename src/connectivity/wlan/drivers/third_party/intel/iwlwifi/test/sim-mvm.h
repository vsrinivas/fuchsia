// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_

#include <memory>
#include <vector>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}  // extern "C"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-nvm.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim.h"

namespace wlan::testing {

class SimMvm {
 public:
  explicit SimMvm() : resp_buf_(kNvmAccessCmdSize) {}

  // Execute the command.
  //
  // 'notify_wait' will be updated to:
  //   true: tell the caller to notify the notification wait.
  //   false: no need to notify.
  //
  zx_status_t SendCmd(struct iwl_trans* trans, struct iwl_host_cmd* cmd, bool* notify_wait);

 private:
  // The buffer size should be determined by the max response size.
  // This number is for the response of NVM_ACCESS_CMD read command.
  static constexpr size_t kNvmAccessCmdSize = 16 + 2048;

  SimNvm nvm_;

  // Used by SendCmd() to store the response from the simulated firmware functions.
  //
  // Since this buffer is used only when CMD_WANT_SKB is set, which means it is protected
  // by STATUS_SYNC_HCMD_ACTIVE in trans->status when CMD_ASYNC is de-assserted in cmd->flags.
  std::vector<char> resp_buf_;
};

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_
