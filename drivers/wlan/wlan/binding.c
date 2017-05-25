// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <magenta/types.h>

extern mx_status_t wlan_bind(void* ctx, mx_device_t* device, void** cookie);

static mx_driver_ops_t wlan_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = wlan_bind,
};

MAGENTA_DRIVER_BEGIN(wlan, wlan_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_WLANMAC),
MAGENTA_DRIVER_END(wlan)
