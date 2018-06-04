// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>

extern zx_status_t optee_bind(void*, zx_device_t*);

static zx_driver_ops_t optee_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = optee_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(optee, optee_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_OPTEE),
ZIRCON_DRIVER_END(optee)
// clang-format on
