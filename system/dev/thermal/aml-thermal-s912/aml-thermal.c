// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>

extern zx_status_t aml_thermal_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t aml_thermal_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_thermal_bind,
};

ZIRCON_DRIVER_BEGIN(aml_thermal, aml_thermal_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SCPI),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S912),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_THERMAL),
ZIRCON_DRIVER_END(aml_thermal)
