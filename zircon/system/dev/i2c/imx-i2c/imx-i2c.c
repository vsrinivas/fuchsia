// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>

#include <zircon/types.h>

extern zx_status_t imx_i2c_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t imx_i2c_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = imx_i2c_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(imx_i2c, imx_i2c_driver_ops, "imx-i2c", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_NXP),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_IMX8MMEVK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_IMX_I2C),
ZIRCON_DRIVER_END(imx_i2c)
// clang-format on
