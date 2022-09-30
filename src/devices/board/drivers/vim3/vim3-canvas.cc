// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a311d/a311d-hw.h>

#include "vim3.h"

namespace vim3 {

namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> canvas_mmios{
    {{
        .base = A311D_DMC_BASE,
        .length = A311D_DMC_LENGTH,
    }},
};

static const std::vector<fpbus::Bti> canvas_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_CANVAS,
    }},
};

static const fpbus::Node canvas_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "canvas";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_AMLOGIC_CANVAS;
  dev.mmio() = canvas_mmios;
  dev.bti() = canvas_btis;
  return dev;
}();

zx_status_t Vim3::CanvasInit() {
  fdf::Arena arena('CANV');
  fidl::Arena fidl_arena;
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, canvas_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "Canvas(canvas_dev)Init: NodeAdd Canvas(canvas_dev) request failed: %s",
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "Canvas(canvas_dev)Init: NodeAdd Canvas(canvas_dev) failed: %s",
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace vim3
