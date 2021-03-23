// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include "src/media/drivers/amlogic_decoder/amlogic_video_test_bind.h"

extern zx_status_t test_amlogic_video_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t amlogic_video_driver_ops = {
    .version = DRIVER_OPS_VERSION, .init = NULL, .bind = test_amlogic_video_bind,
    // .release is not critical for this driver because dedicated devhost
    // process
};

ZIRCON_DRIVER(amlogic_video_test, amlogic_video_driver_ops, "zircon", "0.1");
