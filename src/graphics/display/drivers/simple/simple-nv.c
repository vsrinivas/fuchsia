// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <hw/pci.h>

#include "simple-display.h"

#define NV_GFX_VID (0x10de)

static zx_status_t nv_disp_bind(void* ctx, zx_device_t* dev) {
  // framebuffer bar seems to be 1
  return bind_simple_pci_display_bootloader(dev, "nv", 1u);
}

static zx_driver_ops_t nv_disp_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = nv_disp_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(nv_disp, nv_disp_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, NV_GFX_VID),
    BI_MATCH_IF(EQ, BIND_PCI_CLASS, PCI_CLASS_DISPLAY), // display controller
ZIRCON_DRIVER_END(nv_disp)
