// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/driver.h>
#include <ddk/binding.h>

#include "broker.h"

static zx_driver_ops_t nand_broker_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = nand_broker_bind,
};

ZIRCON_DRIVER_BEGIN(nand-broker, nand_broker_ops, "zircon", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_NAND)
ZIRCON_DRIVER_END(nand-broker)
