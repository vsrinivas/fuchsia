// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/types.h>

extern zx_status_t wlantapctl_init(void** out_ctx);
extern zx_status_t wlantapctl_bind(void* ctx, zx_device_t* device);
extern void wlantapctl_release(void* ctx);

static zx_driver_ops_t wlantapctl_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = wlantapctl_init,
    .bind = wlantapctl_bind,
    .release = wlantapctl_release,
};

ZIRCON_DRIVER_BEGIN(wlantapctl, wlantapctl_driver_ops, "fuchsia", "0.1", 1)
BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT), ZIRCON_DRIVER_END(wlantapctl)
