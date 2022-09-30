// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/sysmem/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include "vim3.h"

namespace vim3 {
namespace fpbus = fuchsia_hardware_platform_bus;
static const std::vector<fpbus::Bti> sysmem_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_SYSMEM,
    }},
};
static const sysmem_metadata_t sysmem_metadata = {
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_A311D,
    .protected_memory_size = 0,
    // Enough for 5 1920x1088 fullscreen images (2 for virtcon and 3 for scenic).
    .contiguous_memory_size = 50LL * 1024 * 1024,
};

static const std::vector<fpbus::Metadata> sysmem_metadata_list{
    {{
        .type = SYSMEM_METADATA_TYPE,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&sysmem_metadata),
            reinterpret_cast<const uint8_t*>(&sysmem_metadata) + sizeof(sysmem_metadata)),
    }},
};

static const fpbus::Node sysmem_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "sysmem";
  dev.vid() = PDEV_VID_GENERIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_SYSMEM;
  dev.bti() = sysmem_btis;
  dev.metadata() = sysmem_metadata_list;
  return dev;
}();

zx_status_t Vim3::SysmemInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('SYSM');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, sysmem_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Sysmem(sysmem_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Sysmem(sysmem_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}
}  // namespace vim3
