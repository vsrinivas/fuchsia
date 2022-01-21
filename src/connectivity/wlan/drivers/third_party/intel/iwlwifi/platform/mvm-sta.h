// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MVM_STA_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MVM_STA_H_

#include <fuchsia/hardware/wlan/associnfo/cpp/banjo.h>
#include <fuchsia/hardware/wlan/softmac/cpp/banjo.h>
#include <netinet/if_ether.h>
#include <zircon/types.h>

#include <memory>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/sta.h"
}  // extern "C"

namespace wlan::iwlwifi {

// This class manages the lifetime of one instance of a iwl_mvm_sta.
class MvmSta {
 public:
  // Factory function for MvmSta instances.
  static zx_status_t Create(struct iwl_mvm_vif* iwl_mvm_vif, const uint8_t bssid[ETH_ALEN],
                            std::unique_ptr<MvmSta>* mvm_sta_out);
  ~MvmSta();

  // Get the current station state.
  enum iwl_sta_state GetState() const;

  // Effect a change to the given station state.
  zx_status_t ChangeState(enum iwl_sta_state state);

  // Accessors.
  struct iwl_mvm_sta* iwl_mvm_sta();
  const struct iwl_mvm_sta* iwl_mvm_sta() const;

 private:
  explicit MvmSta(struct iwl_mvm_vif* iwl_mvm_vif, std::unique_ptr<struct iwl_mvm_sta> iwl_mvm_sta);

  // Change the station state in each direction:
  // * Up is from NOTEXIST -> AUTHORIZED
  // * Down is from AUTHORIZED -> NOTEXIST
  zx_status_t ChangeStateUp();
  zx_status_t ChangeStateDown();

  struct iwl_mvm_vif* iwl_mvm_vif_ = nullptr;
  std::unique_ptr<struct iwl_mvm_sta> iwl_mvm_sta_;
  enum iwl_sta_state sta_state_ = iwl_sta_state::IWL_STA_NOTEXIST;
};

}  // namespace wlan::iwlwifi

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MVM_STA_H_
