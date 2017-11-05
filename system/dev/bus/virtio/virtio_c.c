// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// implemented in virtio_driver.cpp
extern zx_status_t virtio_bind(void* ctx, zx_device_t* device);

static zx_driver_ops_t virtio_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = virtio_bind,
};

ZIRCON_DRIVER_BEGIN(virtio, virtio_driver_ops, "zircon", "0.1", 12)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x1af4),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1000), // Network device (transitional)
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1001), // Block device (transitional)
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1042), // Block device
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1003), // Console device (transitional)
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1043), // Console device
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1005), // RNG device (transitional)
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1044), // RNG device
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1050), // GPU device
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1052), // Input device
    BI_ABORT(),
ZIRCON_DRIVER_END(virtio)
