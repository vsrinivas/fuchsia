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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_TRANS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_TRANS_H_

#include <memory>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-mvm.h"

namespace async {

class Loop;

}  // namespace async

namespace wlan {
namespace iwlwifi {

class RcuManager;
class WlanphyImplDevice;

}  // namespace iwlwifi

namespace testing {

// The struct to store the internal state of the simulated firmware.
struct sim_trans_priv {
  SimMvm* fw;

  // The pointer pointing back to a Test case for mock functions.  This must be initialized before
  // mock functions are called.
  void* test;
};

static inline struct sim_trans_priv* IWL_TRANS_GET_SIM_TRANS(struct iwl_trans* trans) {
  return (struct sim_trans_priv*)trans->trans_specific;
}

class SimTransport : public SimMvm {
 public:
  explicit SimTransport(zx_device_t* parent);
  ~SimTransport();

  // This function must be called before starting using other functions.
  zx_status_t Init();

  // Member accessors.
  struct iwl_trans* iwl_trans();
  const struct iwl_trans* iwl_trans() const;
  ::wlan::iwlwifi::WlanphyImplDevice* sim_device();
  const ::wlan::iwlwifi::WlanphyImplDevice* sim_device() const;
  zx_device_t* fake_parent();

 private:
  std::unique_ptr<::async::Loop> task_loop_;
  std::unique_ptr<::async::Loop> irq_loop_;
  std::unique_ptr<::wlan::iwlwifi::RcuManager> rcu_manager_;
  struct device device_;
  struct iwl_trans* iwl_trans_;
  wlan::iwlwifi::WlanphyImplDevice* sim_device_;
};

}  // namespace testing
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_TRANS_H_
