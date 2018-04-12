// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdlib.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <zircon/types.h>

#include "ram-nand-ctl.h"

static zx_driver_ops_t ram_nand_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ram_nand_driver_bind,
};

ZIRCON_DRIVER_BEGIN(ram_nand, ram_nand_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(ram_nand)
