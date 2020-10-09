// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ddk/device.h>
#include <ddk/driver.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/bind.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwlwifi-bind.h"

/*
 * Currently the Intel wlan driver only supports PCIe. When we add support for other forms of
 * interfaces, we can mux all of that through this file using build time config options.
 */
static zx_status_t pci_bus_bind(void* ctx, zx_device_t* parent) {
  return iwl_pci_bind(ctx, parent);
}

static constexpr zx_driver_ops_t pci_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = pci_bus_bind;
  return ops;
}();

ZIRCON_DRIVER(iwlwifi_pci, pci_driver_ops, "zircon", "0.1");
