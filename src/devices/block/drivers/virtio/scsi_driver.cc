// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/virtio/driver_utils.h>
#include <zircon/types.h>

#include <ddk/driver.h>

#include "scsi.h"
#include "src/devices/block/drivers/virtio/virtio_scsi_bind.h"

static const zx_driver_ops_t virtio_scsi_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = virtio::CreateAndBind<virtio::ScsiDevice>;
  return ops;
}();

ZIRCON_DRIVER(virtio_scsi, virtio_scsi_driver_ops, "zircon", "0.1");
