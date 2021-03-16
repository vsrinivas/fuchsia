// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/sysmem/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include "machina.h"

static const pbus_bti_t sysmem_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SYSMEM,
    },
};

static const sysmem_metadata_t sysmem_metadata = {
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_MACHINA,
    .protected_memory_size = 0,
};

static const pbus_metadata_t sysmem_metadata_list[] = {{
    .type = SYSMEM_METADATA,
    .data_buffer = (uint8_t*)&sysmem_metadata,
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

zx_status_t machina_sysmem_init(machina_board_t* bus) {
  zx_status_t status;

  if ((status = pbus_device_add(&bus->pbus, &sysmem_dev)) != ZX_OK) {
    zxlogf(ERROR, "machina_sysmem_init: pbus_protocol_device_add() failed for sysmem: %d", status);
    return status;
  }

  return ZX_OK;
}
