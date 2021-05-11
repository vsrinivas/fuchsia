// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PCIE_FUCHSIA_PCI_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PCIE_FUCHSIA_PCI_H_

// This file contains PCI bus code that operates as a compatibility layer between the Linux and
// Fuchsia PCI bus driver models.

#include <fuchsia/hardware/pci/c/banjo.h>
#include <lib/async/dispatcher.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fuchsia_device.h"

struct iwl_pci_device_id {
  uint16_t device_id;
  uint16_t subsystem_device_id;
  const struct iwl_cfg* config;
};

// Fuchsia-specific PCI bus interface.
struct iwl_pci_dev {
  struct device dev;
  pci_protocol_t proto;
  struct iwl_trans* drvdata;
};

static inline void iwl_pci_set_drvdata(struct iwl_pci_dev* dev, struct iwl_trans* drvdata) {
  dev->drvdata = drvdata;
}

static inline struct iwl_trans* iwl_pci_get_drvdata(struct iwl_pci_dev* dev) {
  return dev->drvdata;
}

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PCIE_FUCHSIA_PCI_H_
