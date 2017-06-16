// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

mx_status_t null_bind(void* ctx, mx_device_t* parent, void** cookie);
mx_status_t zero_bind(void* ctx, mx_device_t* parent, void** cookie);

mx_status_t root_bind(void* ctx, mx_device_t* parent, void** cookie) {
    null_bind(ctx, parent, cookie);
    zero_bind(ctx, parent, cookie);
    return MX_OK;
}

static mx_driver_ops_t root_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = root_bind,
};

MAGENTA_DRIVER_BEGIN(root_drivers, root_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_ROOT),
MAGENTA_DRIVER_END(root_drivers)
