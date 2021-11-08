// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To simulate the TIME_EVENT_CMD in the testing code.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_TIME_EVENT_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_TIME_EVENT_H_

#include <zircon/types.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim.h"

namespace wlan::testing {

// The returned unique_id for the time event command. On the real firmware, this value is
// generated randomly. But a fake fixed value is good enough for the testing code.
constexpr uint16_t kFakeUniqueId = 0x5566;

zx_status_t HandleTimeEvent(struct iwl_host_cmd* cmd, SimMvmResponse* resp);

}  // namespace wlan::testing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_SIM_TIME_EVENT_H_
