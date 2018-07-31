// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>

extern zx_status_t platform_bus_create(void* ctx, zx_device_t* parent, const char* name,
                                       const char* args, zx_handle_t rpc_channel);

static zx_driver_ops_t platform_bus_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .create = platform_bus_create,
};

ZIRCON_DRIVER_BEGIN(platform_bus, platform_bus_driver_ops, "zircon", "0.1", 1)
    // devmgr loads us directly, so we need no binding information here
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(platform_bus)
