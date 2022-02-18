// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To simulate the TIME_EVENT_CMD in the testing code.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_SCAN_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_SCAN_H_

#include <zircon/types.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim.h"

namespace wlan::testing {

zx_status_t HandleScanAbort(struct iwl_trans* trans, struct iwl_host_cmd* cmd);

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_MCC_UPDATE_H_
