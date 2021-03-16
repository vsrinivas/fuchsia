// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/driver.h>
#include <lib/virtio/driver_utils.h>
#include <zircon/types.h>

#include "input.h"
#include "src/ui/input/drivers/virtio/virtio_input_bind.h"

static const zx_driver_ops_t virtio_input_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = virtio::CreateAndBind<virtio::InputDevice>;
  return ops;
}();

ZIRCON_DRIVER(virtio_input, virtio_input_driver_ops, "zircon", "0.1");
