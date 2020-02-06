// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_ctx.h"

#include <zircon/assert.h>

#include <ddk/debug.h>

std::pair<zx_status_t, std::unique_ptr<DeviceCtx>> DeviceCtx::Bind(DriverCtx* driver_ctx,
                                                                   zx_device_t* parent) {
  zxlogf(ERROR, "amlogic-encoder bind not yet implemented.");
  return {ZX_ERR_NOT_SUPPORTED, nullptr};
}

DeviceCtx::DeviceCtx(DriverCtx* driver_ctx) : driver_ctx_(driver_ctx) {
  ZX_DEBUG_ASSERT(driver_ctx_);
}
