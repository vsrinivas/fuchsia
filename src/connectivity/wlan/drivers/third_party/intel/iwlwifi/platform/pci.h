// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_PCI_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_PCI_H_

// This file contains PCI bus code that operates as a compatibility layer between the Linux and
// Fuchsia PCI bus driver models.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

static inline void iwl_pci_set_drvdata(struct iwl_pci_dev* dev, struct iwl_trans* drvdata) {
  dev->drvdata = drvdata;
}

static inline struct iwl_trans* iwl_pci_get_drvdata(struct iwl_pci_dev* dev) {
  return dev->drvdata;
}

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_PCI_H_
