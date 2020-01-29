// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>

extern zx_status_t amlogic_video_init(void** out_ctx);
extern zx_status_t amlogic_video_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t amlogic_video_driver_ops = {
    .version = DRIVER_OPS_VERSION, .init = amlogic_video_init, .bind = amlogic_video_bind,
    // .release is not critical for this driver because dedicated devhost
    // process
};

// clang-format off
ZIRCON_DRIVER_BEGIN(amlogic_video, amlogic_video_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_VIDEO),
ZIRCON_DRIVER_END(amlogic_video);
// clang-format on
