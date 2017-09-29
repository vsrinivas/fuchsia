// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

extern zx_status_t simple_init(void** out_ctx);

zx_driver_ops_t _driver_ops_ddk_toy = {
    .version = DRIVER_OPS_VERSION,
    .init = simple_init,
};

ZIRCON_DRIVER_BEGIN(_driver_ddk_toy, _driver_ops_ddk_toy, "ddk-toy", "0.1.0", 0)
ZIRCON_DRIVER_END(_driver_ddk_toy)
