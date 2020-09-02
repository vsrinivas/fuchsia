// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Simulated firmware for iwlwifi.
//
// This class actually simulates a transport layer ops (just like a PCI-e bus).
//
// By the way, this class also holds a 'iwl_trans' instance, which contains 'op_mode' and 'mvm'
// after Init() is called.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_TRANS_SIM_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_TRANS_SIM_H_

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-mvm.h"

namespace wlan {
namespace testing {

// The struct to store the internal state of the simulated firmware.
struct trans_sim_priv {
  SimMvm* fw;

  // The pointer pointing back to a Test case for mock functions.  This must be initialized before
  // mock functions are called.
  void* test;
};

static inline struct trans_sim_priv* IWL_TRANS_GET_TRANS_SIM(struct iwl_trans* trans) {
  return (struct trans_sim_priv*)trans->trans_specific;
}

class TransportSim : public SimMvm {
 public:
  explicit TransportSim(::wlan::simulation::Environment* env) : SimMvm(env), iwl_trans_(nullptr) {}
  ~TransportSim() { Release(); }

  // This function must be called before starting using other functions.
  zx_status_t Init();

  void Release();

  // Note that the user cannot take the ownership of trans. This class still holds it.
  struct iwl_trans* iwl_trans() {
    return iwl_trans_;
  }

 private:
  struct iwl_trans* iwl_trans_;
};

}  // namespace testing
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_TRANS_SIM_H_
