// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include "simple-display.h"

// simple framebuffer device to match against an AMD Kaveri R7 device already
// initialized from EFI
#define AMD_GFX_VID (0x1002)
#define AMD_KAVERI_R7_DID (0x130f)
#define AMD_GFX_RAVEN (0x15dd)
#define AMD_POLARIS_22_DID (0x694c)
#define AMD_R7_240_340_DID (0x6613)
#define AMD_GFX_STONEY_RIDGE_DID (0x98e4)

static zx_status_t kaveri_disp_bind(void* ctx, zx_device_t* dev) {
  // framebuffer bar seems to be 0
  return bind_simple_pci_display_bootloader(dev, "amd", 0u);
}

static zx_driver_ops_t kaveri_disp_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = kaveri_disp_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(kaveri_disp, kaveri_disp_driver_ops, "zircon", "0.1", 7)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, AMD_GFX_VID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, AMD_KAVERI_R7_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, AMD_GFX_RAVEN),
    BI_MATCH_IF(EQ, BIND_PCI_DID, AMD_POLARIS_22_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, AMD_R7_240_340_DID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, AMD_GFX_STONEY_RIDGE_DID),
ZIRCON_DRIVER_END(kaveri_disp)
