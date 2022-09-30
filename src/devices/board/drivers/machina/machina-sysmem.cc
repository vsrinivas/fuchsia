// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/sysmem/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include "machina.h"

namespace machina {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Bti> sysmem_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_SYSMEM,
    }},
};

static const sysmem_metadata_t sysmem_metadata = {
    .vid = PDEV_VID_GOOGLE,
    .pid = PDEV_PID_MACHINA,
    .protected_memory_size = 0,
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
  fpbus::Node ret;
  ret.name() = "sysmem";
  ret.vid() = PDEV_VID_GENERIC;
  ret.pid() = PDEV_PID_GENERIC;
  ret.did() = PDEV_DID_SYSMEM;
  ret.bti() = sysmem_btis;
  ret.metadata() = sysmem_metadata_list;
  return ret;
}();

zx_status_t machina_sysmem_init(machina_board_t* bus) {
  fidl::Arena<> fidl_arena;
  auto result =
      bus->client.buffer(fdf::Arena('SYSM'))->NodeAdd(fidl::ToWire(fidl_arena, sysmem_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd request failed: %s", __func__, result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd failed: %s", __func__, zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace machina
