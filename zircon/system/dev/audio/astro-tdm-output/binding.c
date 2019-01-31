// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>

extern zx_status_t audio_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t aml_tdm_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = audio_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_tdm, aml_tdm_driver_ops, "aml-tdm-out", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_TDM),
ZIRCON_DRIVER_END(aml_tdm)
    // clang-format on
