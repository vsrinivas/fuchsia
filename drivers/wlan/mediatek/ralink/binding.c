// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/types.h>

extern zx_status_t ralink_bind(void* ctx, zx_device_t* device);

static zx_driver_ops_t ralink_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ralink_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(ralink, ralink_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, 0x148f),
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x5370),  // RT5370
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x5572),  // RT5572
ZIRCON_DRIVER_END(ralink)
    // clang-format on
