// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <cstdint>

#include "astro.h"
#include "src/devices/board/drivers/astro/astro-securemem-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace astro {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Bti> astro_secure_mem_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_AML_SECURE_MEM,
    }},
};

static const fpbus::Node secure_mem_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-secure-mem";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_S905D2;
  dev.did() = PDEV_DID_AMLOGIC_SECURE_MEM;
  dev.bti() = astro_secure_mem_btis;
  return dev;
}();

zx_status_t Astro::SecureMemInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('SECU');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, secure_mem_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, aml_secure_mem_fragments,
                                               std::size(aml_secure_mem_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite SecureMem(secure_mem_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite SecureMem(secure_mem_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace astro
