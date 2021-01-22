// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/sysmem/c/banjo.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::SysmemInit() {
  static const pbus_bti_t sysmem_btis[] = {
      {
          .iommu_index = 0,
          .bti_id = BTI_SYSMEM,
      },
  };

  static const sysmem_metadata_t sysmem_metadata = {
      .vid = PDEV_VID_MEDIATEK,
      .pid = PDEV_PID_MEDIATEK_8167S_REF,
      .protected_memory_size = 0,
      .contiguous_memory_size = 0,
  };

  static const pbus_metadata_t sysmem_metadata_list[] = {{
      .type = SYSMEM_METADATA,
      .data_buffer = &sysmem_metadata,
      .data_size = sizeof(sysmem_metadata),
  }};

  static const pbus_dev_t sysmem_dev = [] {
    pbus_dev_t ret = {};
    ret.name = "sysmem";
    ret.vid = PDEV_VID_GENERIC;
    ret.pid = PDEV_PID_GENERIC;
    ret.did = PDEV_DID_SYSMEM;
    ret.bti_list = sysmem_btis;
    ret.bti_count = countof(sysmem_btis);
    ret.metadata_list = sysmem_metadata_list;
    ret.metadata_count = countof(sysmem_metadata_list);
    return ret;
  }();

  zx_status_t status = pbus_.DeviceAdd(&sysmem_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_mt8167
