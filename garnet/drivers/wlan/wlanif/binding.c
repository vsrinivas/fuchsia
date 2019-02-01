// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <zircon/types.h>

#include "driver.h"

static zx_driver_ops_t wlanif_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = wlanif_bind,
};

ZIRCON_DRIVER_BEGIN(wlan, wlanif_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_WLANIF_IMPL),
ZIRCON_DRIVER_END(wlan)
