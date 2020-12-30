// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/virtio/driver_utils.h>
#include <zircon/types.h>

#include "gpu.h"
#include "src/graphics/drivers/virtio/virtio_gpu_bind.h"

static zx_status_t virtio_gpu_bind(void* ctx, zx_device_t* bus_device) {
  const char* flag = getenv("driver.virtio-gpu.disable");
  // If gpu disabled:
  if (flag != nullptr && (!strcmp(flag, "1") || !strcmp(flag, "true") || !strcmp(flag, "on"))) {
    zxlogf(INFO, "driver.virtio-gpu.disabled=1, not binding to the GPU");
    return ZX_ERR_NOT_FOUND;
  }
  return CreateAndBind<virtio::GpuDevice>(ctx, bus_device);
}

static const zx_driver_ops_t gpu_block_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = virtio_gpu_bind;
  return ops;
}();

ZIRCON_DRIVER(gpu_block, gpu_block_driver_ops, "zircon", "0.1");
