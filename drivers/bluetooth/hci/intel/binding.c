// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/types.h>

extern zx_status_t btintel_bind(void* ctx, zx_device_t* device);

static zx_driver_ops_t btintel_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = btintel_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(btintel, btintel_driver_ops, "fuchsia", "0.1", 8)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_BT_HCI_TRANSPORT),
    BI_ABORT_IF(NE, BIND_USB_VID, 0x8087), // Intel Corp.
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x07dc), // Intel 7260
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x0a2a), // Intel 7265
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x0aa7), // Sandy Peak (3168)
    // Devices below use the "secure" method
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x0025), // Thunder Peak (9160/9260)
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x0a2b), // Snowfield Peak (8260)
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x0aaa), // Jefferson Peak (9460/9560)
ZIRCON_DRIVER_END(btintel)
