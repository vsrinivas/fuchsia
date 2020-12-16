// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <hw/pci.h>

#include "simple-display.h"
#include "src/graphics/display/drivers/simple/simple-vmware-bind.h"

static zx_status_t vmware_disp_bind(void* ctx, zx_device_t* dev) {
  // framebuffer bar seems to be 1
  return bind_simple_pci_display_bootloader(dev, "vmware", 1u);
}

static zx_driver_ops_t vmware_disp_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = vmware_disp_bind,
};

ZIRCON_DRIVER(vmware_disp, vmware_disp_driver_ops, "zircon", "0.1");
