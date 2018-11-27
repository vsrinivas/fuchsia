// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <zircon/types.h>

#include "sysdev.h"

static zx_driver_ops_t test_sysdev_ops = {
    .version = DRIVER_OPS_VERSION,
    .create = test_sysdev_create,
};

ZIRCON_DRIVER_BEGIN(test_sysdev, test_sysdev_ops, "zircon", "0.1", 1)
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(test_sysdev)
