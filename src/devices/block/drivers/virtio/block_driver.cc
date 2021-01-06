// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/virtio/driver_utils.h>
#include <zircon/types.h>

#include <ddk/driver.h>

#include "block.h"
#include "src/devices/block/drivers/virtio/virtio_block_bind.h"

static const zx_driver_ops_t virtio_block_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = CreateAndBind<virtio::BlockDevice>;
  return ops;
}();

ZIRCON_DRIVER(virtio_block, virtio_block_driver_ops, "zircon", "0.1");
