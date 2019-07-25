// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_

// This file must be included before other headers in order to provide
// correct definition. TODO(WLAN-1215): to fix this issue.
// clang-format off
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fuchsia_porting.h"
// clang-format on

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"

namespace wlan {
namespace testing {

class SimMvm : public SimulatedFirmware {
 public:
  SimMvm(SimulatedEnvironment* env) : SimulatedFirmware(env) {}
  ~SimMvm() {}
};

}  // namespace testing
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MVM_H_
