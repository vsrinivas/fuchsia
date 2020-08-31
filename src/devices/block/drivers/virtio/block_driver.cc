// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/virtio/driver_utils.h>
#include <zircon/types.h>

#include <ddk/driver.h>

#include "block.h"

static const zx_driver_ops_t virtio_block_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = CreateAndBind<virtio::BlockDevice>;
  return ops;
}();

ZIRCON_DRIVER_BEGIN(virtio_block, virtio_block_driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, VIRTIO_PCI_VENDOR_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_BLOCK),
    BI_MATCH_IF(EQ, BIND_PCI_DID, VIRTIO_DEV_TYPE_T_BLOCK), ZIRCON_DRIVER_END(virtio_block)
