// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <ddk/driver.h>

#include "driver_utils.h"
#include "rng.h"

static const zx_driver_ops_t virtio_rng_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = CreateAndBind<virtio::RngDevice>;
  return ops;
}();

ZIRCON_DRIVER_BEGIN(virtio_rng, virtio_rng_driver_ops, "zircon", "0.1", 6)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, VIRTIO_PCI_VENDOR_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_ENTROPY),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_T_ENTROPY), ZIRCON_DRIVER_END(virtio_rng)
