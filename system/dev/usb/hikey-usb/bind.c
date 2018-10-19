// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>

extern zx_status_t hikey_usb_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t hikey_usb_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hikey_usb_bind,
};

ZIRCON_DRIVER_BEGIN(hikey_usb, hikey_usb_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_96BOARDS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_HIKEY960),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_HIKEY960_USB),
ZIRCON_DRIVER_END(hikey_usb)
