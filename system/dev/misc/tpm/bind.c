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

ZIRCON_DRIVER_BEGIN(tpm, tpm_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(tpm)
