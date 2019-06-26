// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include "simple-display.h"

#define INTEL_I915_VID (0x8086)

static zx_status_t intel_disp_bind(void* ctx, zx_device_t* dev) {
  return bind_simple_pci_display_bootloader(dev, "intel", 2u);
}

static zx_driver_ops_t intel_disp_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = intel_disp_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(intel_disp, intel_disp_driver_ops, "zircon", "*0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, INTEL_I915_VID),
    BI_MATCH_IF(EQ, BIND_PCI_CLASS, 0x3), // Display class
ZIRCON_DRIVER_END(intel_disp)
