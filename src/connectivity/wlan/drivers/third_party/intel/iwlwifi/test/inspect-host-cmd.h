// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_INSPECT_HOST_CMD_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_INSPECT_HOST_CMD_H_

#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"

// Util functions to dump host command info/data for debug.
void inspect_host_cmd(const char* func_name, int line_no, struct iwl_host_cmd* cmd);

// |cmd| should be 'struct iwl_host_cmd*'.
//
// Disable by default. Please manually enable it when needed.
//
#define INSPECT_HOST_CMD(cmd)                     \
  do {                                            \
    if (false) {                                  \
      inspect_host_cmd(__func__, __LINE__, cmd);  \
    }                                             \
  } while (0)

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_TEST_INSPECT_HOST_CMD_H_
