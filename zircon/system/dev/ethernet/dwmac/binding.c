// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <stdlib.h>
#include <string.h>

extern zx_status_t dwmac_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t dwmac_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = dwmac_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(dwmac, dwmac_driver_ops, "designware_mac", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_DESIGNWARE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ETH_MAC),
ZIRCON_DRIVER_END(dwmac)
