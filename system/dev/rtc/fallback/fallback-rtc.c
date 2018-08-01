// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>

extern zx_status_t fallback_rtc_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t fallback_rtc_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = fallback_rtc_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(fallback_rtc, fallback_rtc_ops, "fallback_rtc", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_RTC_FALLBACK),
ZIRCON_DRIVER_END(fallback_rtc)
// clang-format on
