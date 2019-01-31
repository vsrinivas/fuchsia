// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm.h"

#include <ddk/binding.h>
#include <ddk/driver.h>

static zx_driver_ops_t tpm_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = tpm_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(tpm, tpm_driver_ops, "zircon", "0.1", 3)
    // Handle I2C
    // TODO(teisenbe): Make this less hacky when we have a proper I2C protocol
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_ABORT_IF(NE, BIND_PCI_DID, 0x9d61),
    BI_MATCH_IF(EQ, BIND_I2C_ADDR, 0x0050),
ZIRCON_DRIVER_END(tpm);
// clang-format on
