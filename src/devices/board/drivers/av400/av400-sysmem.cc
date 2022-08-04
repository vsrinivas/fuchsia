// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/sysmem/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include "av400.h"

namespace av400 {

static constexpr pbus_bti_t sysmem_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SYSMEM,
    },
};

static constexpr sysmem_metadata_t sysmem_metadata = {
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_A5,
};

static const pbus_metadata_t sysmem_metadata_list[] = {
    {
        .type = SYSMEM_METADATA_TYPE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&sysmem_metadata),
        .data_size = sizeof(sysmem_metadata),
    },
};

static constexpr pbus_dev_t sysmem_dev = [] {
  pbus_dev_t ret = {};
  ret.name = "sysmem";
  ret.vid = PDEV_VID_GENERIC;
  ret.pid = PDEV_PID_GENERIC;
  ret.did = PDEV_DID_SYSMEM;
  ret.bti_list = sysmem_btis;
  ret.bti_count = std::size(sysmem_btis);
  ret.metadata_list = sysmem_metadata_list;
  ret.metadata_count = std::size(sysmem_metadata_list);
  return ret;
}();

zx_status_t Av400::SysmemInit() {
  zx_status_t status = pbus_.DeviceAdd(&sysmem_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "DeviceAdd failed %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace av400
