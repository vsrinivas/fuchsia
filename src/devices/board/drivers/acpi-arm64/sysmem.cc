// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/sysmem/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <lib/zx/result.h>

#include "acpi-arm64.h"

namespace acpi_arm64 {
namespace fpbus = fuchsia_hardware_platform_bus;

constexpr size_t kBtiSysmem = 0;

zx::result<> AcpiArm64::SysmemInit() {
  static const std::vector<fpbus::Bti> kSysmemBtis{
      {{
          .iommu_index = 0,
          .bti_id = kBtiSysmem,
      }},
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

  static const std::vector<fpbus::Metadata> kSysmemMetadataList{
      {{
          .type = SYSMEM_METADATA_TYPE,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&kSysmemMetadata),
              reinterpret_cast<const uint8_t*>(&kSysmemMetadata) + sizeof(kSysmemMetadata)),
      }},
  };

  fpbus::Node sysmem_dev;
  sysmem_dev.name() = "sysmem";
  sysmem_dev.vid() = PDEV_VID_GENERIC;
  sysmem_dev.pid() = PDEV_PID_GENERIC;
  sysmem_dev.did() = PDEV_DID_SYSMEM;
  sysmem_dev.bti() = kSysmemBtis;
  sysmem_dev.metadata() = kSysmemMetadataList;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('ACPI');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, sysmem_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd AcpiArm64(sysmem_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return zx::error(result.status());
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd AcpiArm64(sysmem_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return zx::error(result->error_value());
  }

  return zx::ok();
}

}  // namespace acpi_arm64
