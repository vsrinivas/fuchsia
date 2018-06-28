// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/nand.h>

extern zx_status_t skip_block_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t skip_block_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = skip_block_bind,
};

ZIRCON_DRIVER_BEGIN(skip_block, skip_block_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_NAND),
    BI_MATCH_IF(EQ, BIND_NAND_CLASS, NAND_CLASS_BBS),
ZIRCON_DRIVER_END(skip_block)
