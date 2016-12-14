// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// implemented in virtio_driver.cpp
extern mx_status_t virtio_bind(mx_driver_t* driver, mx_device_t* device);

mx_driver_t _driver_virtio = {
    .name = "virtio",
    .ops = {
        .bind = virtio_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_virtio, "virtio", "magenta", "0.1", 5)
BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI)
,
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x1af4),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1001), // Block device (transitional)
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1050), // GPU device
    //BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1000), // Network device (transitional)
    BI_ABORT(),
    MAGENTA_DRIVER_END(_driver_virtio)
