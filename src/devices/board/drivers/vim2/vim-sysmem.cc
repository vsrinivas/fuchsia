// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/sysmem/c/banjo.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/debug.h>
#include <ddk/device.h>

#include "vim.h"

namespace vim {
static const pbus_bti_t sysmem_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SYSMEM,
    },
};
static const sysmem_metadata_t sysmem_metadata = {
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S912,
    .protected_memory_size = 0,
    .contiguous_memory_size = 0,
};

static const pbus_metadata_t sysmem_metadata_list[] = {{
    .type = SYSMEM_METADATA,
    .data_buffer = &sysmem_metadata,
    .data_size = sizeof(sysmem_metadata),
}};

static const pbus_dev_t sysmem_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "sysmem";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_SYSMEM;
  dev.bti_list = sysmem_btis;
  dev.bti_count = countof(sysmem_btis);
  dev.metadata_list = sysmem_metadata_list;
  dev.metadata_count = countof(sysmem_metadata_list);
  return dev;
}();

zx_status_t Vim::SysmemInit() {
  zx_status_t status;

  if ((status = pbus_.DeviceAdd(&sysmem_dev)) != ZX_OK) {
    zxlogf(ERROR, "SysmemInit: pbus_protocol_device_add() failed for sysmem: %d", status);
    return status;
  }

  return ZX_OK;
}
}  // namespace vim
