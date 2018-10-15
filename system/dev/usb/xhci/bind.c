// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>

extern zx_status_t usb_xhci_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t xhci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_xhci_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_xhci, xhci_driver_ops, "zircon", "0.1", 9)
    // PCI binding support
    BI_GOTO_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI, 0),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 0x0C),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 0x03),
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 0x30),

    // platform bus binding support
    BI_LABEL(0),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_XHCI),

    BI_ABORT(),
ZIRCON_DRIVER_END(usb_xhci)
