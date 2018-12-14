// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

// This is the implementation of the C bindings to the zircon DDK interface, for the Realtek rtl88xx
// driver.

#include <ddk/binding.h>
#include <ddk/driver.h>

#include "wlan_phy.h"

static zx_driver_ops_t rtl88xx_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = &rtl88xx_bind_wlan_phy,
};

ZIRCON_DRIVER_BEGIN(rtl88xx, rtl88xx_driver_ops, "zircon", "0.1", 8)
    BI_GOTO_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI, 0),
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x10ec),  // Realtek PCI VID
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x0000),
    BI_ABORT(),
    BI_LABEL(0),
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_OLD),
    BI_ABORT_IF(NE, BIND_USB_VID, 0x0bda),  // Realtek USB VID
    BI_MATCH_IF(EQ, BIND_USB_PID, 0xc820),  // UM821C04_3V3 test board
ZIRCON_DRIVER_END(rtl88xx)
