// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <stdlib.h>
#include <string.h>

extern zx_status_t rtl8211f_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t rtl8211f_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = rtl8211f_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(rtl8211f, rtl8211f_driver_ops, "rtl8211-phy", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_REALTEK),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_RTL8211F),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ETH_PHY),
ZIRCON_DRIVER_END(rtl8211f)
