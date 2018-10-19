// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/pci.h>

static zx_status_t pci_drv_bind(void* ctx, zx_device_t* parent) {
    zxlogf(INFO, "userspace pci bound to pciroot.\n");
    return ZX_OK;
}

static zx_driver_ops_t kpci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = pci_drv_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(pci, kpci_driver_ops, "zircon", "0.1", 5)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_PCIROOT),
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_KPCI),
ZIRCON_DRIVER_END(pci)
