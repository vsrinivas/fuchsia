// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <limits.h>

#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> sherlock_canvas_mmios{
    {{
        .base = T931_DMC_BASE,
        .length = T931_DMC_LENGTH,
    }},
};

static const std::vector<fpbus::Bti> sherlock_canvas_btis{
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
  dev.mmio() = sherlock_canvas_mmios;
  dev.bti() = sherlock_canvas_btis;
  return dev;
}();

zx_status_t Sherlock::CanvasInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('CANV');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, canvas_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Canvas(canvas_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Canvas(canvas_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace sherlock
