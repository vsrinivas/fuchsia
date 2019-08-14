// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
}

namespace wlan {
namespace testing {

class SimMvm : public SimulatedFirmware {
 public:
  SimMvm(SimulatedEnvironment* env) : SimulatedFirmware(env) {}
  ~SimMvm() {}

  zx_status_t SendCmd(struct iwl_host_cmd* cmd) { return ZX_OK; }
};

}  // namespace testing
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_
