// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/sysmem/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <lib/zx/status.h>

#include "acpi-arm64.h"

namespace acpi_arm64 {

constexpr size_t kBtiSysmem = 0;

zx::status<> AcpiArm64::SysmemInit() {
  constexpr pbus_bti_t kSysmemBtis[] = {
      {
          .iommu_index = 0,
          .bti_id = kBtiSysmem,
      },
  };

  constexpr sysmem_metadata_t kSysmemMetadata = {
      .vid = PDEV_VID_QEMU,
      .pid = PDEV_PID_QEMU,
      // no protected pool
      .protected_memory_size = 0,
      // -5 means 5% of physical RAM
      // we allocate a small amount of contiguous RAM to keep the sysmem tests from flaking,
      // see https://fxbug.dev/67703.
      .contiguous_memory_size = -5,
  };

  const pbus_metadata_t kSysmemMetadataList[] = {{
      .type = SYSMEM_METADATA_TYPE,
      .data_buffer = reinterpret_cast<const uint8_t*>(&kSysmemMetadata),
      .data_size = sizeof(kSysmemMetadata),
  }};

  pbus_dev_t sysmem_dev = {};
  sysmem_dev.name = "sysmem";
  sysmem_dev.vid = PDEV_VID_GENERIC;
  sysmem_dev.pid = PDEV_PID_GENERIC;
  sysmem_dev.did = PDEV_DID_SYSMEM;
  sysmem_dev.bti_list = kSysmemBtis;
  sysmem_dev.bti_count = std::size(kSysmemBtis);
  sysmem_dev.metadata_list = kSysmemMetadataList;
  sysmem_dev.metadata_count = std::size(kSysmemMetadataList);

  zx_status_t status = pbus_.DeviceAdd(&sysmem_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d", __func__, status);
    return zx::error(status);
  }

  return zx::ok();
}

}  // namespace acpi_arm64
