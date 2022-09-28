// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MVM_STA_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MVM_STA_H_

#include <fidl/fuchsia.wlan.softmac/cpp/driver/wire.h>
#include <fuchsia/hardware/wlan/associnfo/cpp/banjo.h>
#include <fuchsia/hardware/wlan/softmac/cpp/banjo.h>
#include <netinet/if_ether.h>
#include <zircon/types.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <type_traits>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}  // extern "C"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/scoped_utils.h"

struct ieee80211_key_conf;
struct iwl_mvm_sta;

namespace wlan::iwlwifi {

// This class manages the lifetime of one instance of a iwl_mvm_sta, including associated state such
// as encryption keys.
class MvmSta {
 public:
  // Factory function for MvmSta instances.
  static zx_status_t Create(struct iwl_mvm_vif* iwl_mvm_vif, const uint8_t bssid[ETH_ALEN],
                            std::unique_ptr<MvmSta>* mvm_sta_out);
  ~MvmSta();

  // Set one of the keys for this station, which may be the pairwise, group, etc. key.
  zx_status_t SetKey(const fuchsia_wlan_softmac::wire::WlanKeyConfig* key_config);

  // Get a key for this station, which may be used for TX.
  struct ieee80211_key_conf* GetKey(wlan_key_type_t key_type);
  const struct ieee80211_key_conf* GetKey(wlan_key_type_t key_type) const;

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
  std::array<unique_free_ptr<struct ieee80211_key_conf>, 5> ieee80211_key_confs_;
};

}  // namespace wlan::iwlwifi

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MVM_STA_H_
