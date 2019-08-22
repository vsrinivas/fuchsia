// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-sta-ifc.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
}

namespace wlan {
namespace testing {

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
  void ReceiveNotification(void* payload) override {}

 private:
  ::wlan::simulation::Environment* env_;
};

}  // namespace testing
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_
