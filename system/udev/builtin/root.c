// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is defined by our rules.mk so the dmctl, null, etc
// devices have _driver_xyz symbols.  We need to undefine it
// for us, so we have a __magenta_driver__ symbol to allow us
// to be loaded as a wrapper driver around these "builtin"
// drivers that we contain.
#undef MAGENTA_BUILTIN_DRIVERS

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

extern mx_driver_t _driver_null;
extern mx_driver_t _driver_zero;

mx_status_t root_bind(void* ctx, mx_device_t* parent, void** cookie) {
    _driver_null.ops->bind(ctx, parent, cookie);
    _driver_zero.ops->bind(ctx, parent, cookie);
    return NO_ERROR;
}

static mx_driver_ops_t root_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = root_bind,
};

MAGENTA_DRIVER_BEGIN(root_drivers, root_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_ROOT),
MAGENTA_DRIVER_END(root_drivers)