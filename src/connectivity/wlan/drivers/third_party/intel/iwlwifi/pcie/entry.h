// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PCIE_ENTRY_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PCIE_ENTRY_H_

#include <stdint.h>
#include <zircon/types.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"

// PCIE driver entry points.
zx_status_t iwl_pci_find_device_id(uint16_t device_id, uint16_t subsystem_device_id,
                                   const struct iwl_pci_device_id** out_id);
zx_status_t iwl_pci_probe(struct iwl_pci_dev* pdev, const struct iwl_pci_device_id* ent);
void iwl_pci_remove(struct iwl_pci_dev* pdev);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PCIE_ENTRY_H_
