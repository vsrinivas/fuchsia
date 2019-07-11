// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Simulated firmware for iwlwifi.
//
// This class actually simulates a transport layer ops (just like a PCI-e bus).
//

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_TRANS_SIM_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_TRANS_SIM_H_

// This file must be included before other headers in order to provide
// correct definition.
// clang-format off
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fuchsia_porting.h"
// clang-format on

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"

namespace wlan {
namespace testing {

class TransportSim : public SimulatedFirmware {
 public:
  TransportSim(SimulatedEnvironment* env);
  ~TransportSim() {}
};

}  // namespace testing
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_TRANS_SIM_H_
