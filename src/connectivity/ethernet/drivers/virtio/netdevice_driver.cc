// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/driver.h>
#include <lib/virtio/driver_utils.h>

#include "netdevice.h"
#include "src/connectivity/ethernet/drivers/virtio/virtio_ethernet-bind.h"

static const zx_driver_ops_t virtio_ethernet_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = virtio::CreateAndBind<virtio::NetworkDevice>;
  return ops;
}();

ZIRCON_DRIVER(virtio_ethernet, virtio_ethernet_driver_ops, "zircon", "0.1");
