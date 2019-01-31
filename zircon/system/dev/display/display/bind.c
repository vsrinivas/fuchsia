// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>

#include "controller.h"

static zx_driver_ops_t display_controller_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = display_controller_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(display_controller, display_controller_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL),
ZIRCON_DRIVER_END(display_controller)
