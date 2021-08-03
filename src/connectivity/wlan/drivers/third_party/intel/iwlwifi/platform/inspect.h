// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Util functions to dump info/data for debug.
//
// Manually define the IWL_INSPECT below to turn this on.
//
// These functions have the following ways to use it:
//
//   + IWL_INSPECT_XXX(): macro to print out the XXX. Function name and line number is expanded
//                        here.
//
//   + iwl_inspect_xxx(): function that actually does the print. Usually user doesn't call this
//                        directly. Use IWL_INSPECT_XXX instead.
//

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_INSPECT_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_INSPECT_H_

#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"

// Define this const to enable the feature.
#undef IWL_INSPECT

// ==== iwl_host_cmd =============================================================================
//
void iwl_inspect_host_cmd(const char* func_name, int line_no, struct iwl_host_cmd* cmd);

// |cmd| should be 'struct iwl_host_cmd*'.
#ifdef IWL_INSPECT
#define IWL_INSPECT_HOST_CMD(cmd)                  \
  do {                                             \
    iwl_inspect_host_cmd(__func__, __LINE__, cmd); \
  } while (0)
#else  // IWL_INSPECT
#define IWL_INSPECT_HOST_CMD(cmd) \
  do {                            \
  } while (0)
#endif  // IWL_INSPECT

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_INSPECT_H_
