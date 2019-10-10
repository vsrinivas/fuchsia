// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>

#include "device_ctx.h"
#include "driver_ctx.h"

extern "C" {
zx_status_t amlogic_video_encoder_init(void** out_ctx);
zx_status_t amlogic_video_encoder_bind(void* ctx, zx_device_t* parent);
}

static constexpr zx_driver_ops_t amlogic_video_driver_ops = [] {
  zx_driver_ops_t ops{};
  ops.version = DRIVER_OPS_VERSION;
  ops.init = amlogic_video_encoder_init;
  ops.bind = amlogic_video_encoder_bind;
  return ops;
}();

zx_status_t amlogic_video_encoder_init(void** out_ctx) {
  *out_ctx = new DriverCtx();
  return ZX_OK;
}

zx_status_t amlogic_video_encoder_bind(void* ctx, zx_device_t* parent) {
  DriverCtx* driver_ctx = reinterpret_cast<DriverCtx*>(ctx);
  auto driver_init_status = driver_ctx->Init();
  if (driver_init_status != ZX_OK) {
    zxlogf(ERROR, "Failed to initialize driver: %d", driver_init_status);
    return driver_init_status;
  };

  auto [device_bind_status, device_ctx] = DeviceCtx::Bind(driver_ctx, parent);
  if (device_bind_status != ZX_OK) {
    zxlogf(ERROR, "Failed to bind device: %d", device_bind_status);
    return device_bind_status;
  }

  // Let the device context live forever.
  device_ctx.release();
  return ZX_OK;
}

// clang-format off
ZIRCON_DRIVER_BEGIN(amlogic_video, amlogic_video_driver_ops,
                    /*vendor_name=*/"zircon", /*version=*/"0.1",
                    /*bind_count=*/4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    // TODO(turnage): define/use PDEV_DID_AMLOGIC_VIDEO_ENCODER
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_VIDEO),
ZIRCON_DRIVER_END(amlogic_video)
    // clang-format on
