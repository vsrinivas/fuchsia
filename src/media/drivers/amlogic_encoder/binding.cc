// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/driver/binding.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <memory>

#include "src/media/drivers/amlogic_encoder/device_ctx.h"

namespace amlogic_encoder {

zx_status_t amlogic_encoder_bind(void* ctx, zx_device_t* parent) {
  auto device_ctx = DeviceCtx::Create(parent);

  if (!device_ctx) {
    zxlogf(ERROR, "Failed to create device");
    return ZX_ERR_NO_MEMORY;
  }

  auto status = device_ctx->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to bind device %d", status);
    return status;
  }

  // Let the device context live forever.
  device_ctx.release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops{};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = amlogic_encoder_bind;
  return ops;
}();

}  // namespace amlogic_encoder

// clang-format off
ZIRCON_DRIVER_BEGIN(amlogic_video_enc, amlogic_encoder::driver_ops,
                    /*vendor_name=*/"zircon", /*version=*/"0.1",
                    /*bind_count=*/4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_VIDEO_ENC),
ZIRCON_DRIVER_END(amlogic_video_enc);
// clang-format on
