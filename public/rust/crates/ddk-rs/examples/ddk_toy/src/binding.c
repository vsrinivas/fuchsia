// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

extern zx_driver_ops_t DRIVER_OPS_DDK_TOY;

ZIRCON_DRIVER_BEGIN(_driver_ddk_toy, DRIVER_OPS_DDK_TOY, "ddk-toy", "0.1.0", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(_driver_ddk_toy)
