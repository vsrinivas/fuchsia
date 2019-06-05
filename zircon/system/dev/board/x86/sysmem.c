// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/sysmem.h>

// This value is passed to bti_create as a marker; it does not have a particular
// meaning to anything in the system.
#define SYSMEM_BTI_ID 0x12341234UL

static const pbus_bti_t sysmem_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = SYSMEM_BTI_ID,
    },
};

// On x86 not much is known about the display adapter or other hardware.
static const sysmem_metadata_t sysmem_metadata = {
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .protected_memory_size = 0,
    .contiguous_memory_size = 0,
};

static const pbus_metadata_t sysmem_metadata_list[] = {
    {
        .type = SYSMEM_METADATA,
        .data_buffer = &sysmem_metadata,
        .data_size = sizeof(sysmem_metadata),
    }};


static const pbus_dev_t sysmem_dev = {
    .name = "sysmem",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_SYSMEM,
    .bti_list = sysmem_btis,
    .bti_count = countof(sysmem_btis),
    .metadata_list = sysmem_metadata_list,
    .metadata_count = countof(sysmem_metadata_list),
};

zx_status_t publish_sysmem(pbus_protocol_t* pbus) {
    zx_status_t status;

    if ((status = pbus_protocol_device_add(pbus, ZX_PROTOCOL_SYSMEM, &sysmem_dev)) != ZX_OK) {
        zxlogf(ERROR, "publish_sysmem: pbus_protocol_device_add() failed for sysmem: %d\n", status);
        return status;
    }

    return ZX_OK;
}
