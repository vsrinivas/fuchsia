// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>

extern zx_status_t dwc3_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t dwc3_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = dwc3_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(dwc3, dwc3_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_DWC3),
ZIRCON_DRIVER_END(dwc3)
// clang-format on
