// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/c/banjo.h>

#include <ddk/debug.h>
#include <ddk/platform-defs.h>

#include "x86.h"

namespace x86 {

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
    // no protected pool
    .protected_memory_size = 0,
    // -5 means 5% of physical RAM
    .contiguous_memory_size = -5,
};

static const pbus_metadata_t sysmem_metadata_list[] = {{
    .type = SYSMEM_METADATA,
    .data_buffer = reinterpret_cast<const uint8_t*>(&sysmem_metadata),
    .data_size = sizeof(sysmem_metadata),
}};

static const pbus_dev_t sysmem_dev = [] {
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

zx_status_t X86::SysmemInit() {
  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_SYSMEM, &sysmem_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAddFailed %d", __func__, status);
  }
  return status;
}

}  // namespace x86
