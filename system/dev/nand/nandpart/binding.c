
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/nand.h>

extern zx_status_t nandpart_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t nandpart_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = nandpart_bind,
};

ZIRCON_DRIVER_BEGIN(nandpart, nandpart_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_NAND),
    BI_MATCH_IF(EQ, BIND_NAND_CLASS, NAND_CLASS_PARTMAP),
ZIRCON_DRIVER_END(nandpart)
