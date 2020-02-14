// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <ddk/driver.h>

#include "driver_utils.h"
#include "gpu.h"

static zx_status_t virtio_gpu_bind(void* ctx, zx_device_t* bus_device) {
  const char* flag = getenv("driver.virtio-gpu.disable");
  // If gpu disabled:
  if (flag != nullptr && (!strcmp(flag, "1") || !strcmp(flag, "true") || !strcmp(flag, "on"))) {
    zxlogf(INFO, "driver.virtio-gpu.disabled=1, not binding to the GPU\n");
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

ZIRCON_DRIVER_BEGIN(gpu_block, gpu_block_driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, VIRTIO_PCI_VENDOR_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_GPU), ZIRCON_DRIVER_END(gpu_block)
