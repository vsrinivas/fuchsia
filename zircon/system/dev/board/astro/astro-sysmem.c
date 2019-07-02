// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "astro.h"
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <zircon/device/sysmem.h>

static const pbus_bti_t sysmem_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SYSMEM,
    },
};

static const sysmem_metadata_t sysmem_metadata = {
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    .protected_memory_size = 16 * 1024 * 1024,
    // Support h.264 5.1, which has a max DPB size of 70,778,880 bytes (with NV12), and add some
    // extra size for additional pictures for buffering and several framebuffers (1024*608*4 bytes
    // each).
    .contiguous_memory_size = 100 * 1024 * 1024,
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

zx_status_t astro_sysmem_init(aml_bus_t* bus) {
    zx_status_t status;

    if ((status = pbus_protocol_device_add(&bus->pbus, ZX_PROTOCOL_SYSMEM, &sysmem_dev)) != ZX_OK) {
        zxlogf(ERROR, "astro_sysmem_init: pbus_protocol_device_add() failed for sysmem: %d\n", status);
        return status;
    }

    return ZX_OK;
}
