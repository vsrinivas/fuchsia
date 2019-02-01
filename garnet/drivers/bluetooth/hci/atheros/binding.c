// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/types.h>

extern zx_status_t bt_atheros_bind(void* ctx, zx_device_t* device);

static zx_driver_ops_t bt_atheros_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = bt_atheros_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(bt_hci_atheros, bt_atheros_driver_ops, "fuchsia", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_BT_TRANSPORT),
    BI_ABORT_IF(NE, BIND_USB_VID, 0x0CF3), // Atheros Communications Inc.
    BI_MATCH_IF(EQ, BIND_USB_PID, 0xE300),
ZIRCON_DRIVER_END(bt_hci_atheros)
