// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_

#include <vector>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
}
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-nvm.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim.h"

namespace wlan::testing {

class SimMvm : public ::wlan::simulation::StationIfc {
 public:
  explicit SimMvm(::wlan::simulation::Environment* env) : env_(env) { env->AddStation(this); }
  ~SimMvm() {
    if (env_ != nullptr) {
      env_->RemoveStation(this);
    }
  }

  zx_status_t SendCmd(struct iwl_host_cmd* cmd);

  // StationIfc operations
  void Rx(void* pkt) override {}
  void RxBeacon(const wlan_channel_t& channel, const wlan_ssid_t& ssid,
                const common::MacAddr& bssid) override {}
  void RxAssocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                  const common::MacAddr& bssid) override {}
  void RxAssocResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, uint16_t status) override {}
  void RxDisassocReq(const wlan_channel_t& channel, const common::MacAddr& src,
                     const common::MacAddr& bssid, uint16_t reason) override {}
  void RxProbeReq(const wlan_channel_t& channel, const common::MacAddr& src) override {}
  void RxProbeResp(const wlan_channel_t& channel, const common::MacAddr& src,
                   const common::MacAddr& dst, const wlan_ssid_t& ssid) override {}
  void ReceiveNotification(void* payload) override {}

 private:
  // The buffer size should be determined by the max response size.
  // This number is for the response of NVM_ACCESS_CMD read command.
  static constexpr size_t kNvmAccessCmdSize = 16 + 2048;

  ::wlan::simulation::Environment* env_;
  SimNvm nvm_;

  // Used by SendCmd() to store the response from the simulated firmware functions.
  //
  // Since this buffer is used only when CMD_WANT_SKB is set, which means it is protected
  // by STATUS_SYNC_HCMD_ACTIVE in trans->status when CMD_ASYNC is de-assserted in cmd->flags.
  //
  uint8_t resp_buf_[kNvmAccessCmdSize];
};

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_
