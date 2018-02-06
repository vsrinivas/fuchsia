// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <stdlib.h>
#include <string.h>
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>

extern zx_status_t gauss_tdm_bind(void* ctx, zx_device_t* parent);
extern void gauss_tdm_release(void*);

static zx_driver_ops_t gauss_tdm_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = gauss_tdm_bind,
    .release = gauss_tdm_release,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(gauss_tdm, gauss_tdm_driver_ops, "gauss-tdm", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GAUSS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GAUSS_AUDIO_OUT),
ZIRCON_DRIVER_END(gauss_tdm)
// clang-format on
