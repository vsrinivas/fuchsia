// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/driver.h>
#include <lib/virtio/driver_utils.h>
#include <zircon/types.h>

#include "console.h"
#include "src/devices/serial/drivers/virtio-console/virtio_console_bind.h"

static const zx_driver_ops_t virtio_console_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = virtio::CreateAndBind<virtio::ConsoleDevice>;
  return ops;
}();

ZIRCON_DRIVER(virtio_console, virtio_console_driver_ops, "zircon", "0.1");
