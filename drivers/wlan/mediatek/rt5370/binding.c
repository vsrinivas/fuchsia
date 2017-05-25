// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <magenta/types.h>

extern mx_status_t rt5370_bind(void* ctx, mx_device_t* device, void** cookie);

static mx_driver_ops_t rt5370_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = rt5370_bind,
};

MAGENTA_DRIVER_BEGIN(rt5370, rt5370_driver_ops, "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, 0x148f),
    BI_MATCH_IF(EQ, BIND_USB_PID, 0x5370),
MAGENTA_DRIVER_END(rt5370)
