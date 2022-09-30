// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

namespace astro {
namespace fpbus = fuchsia_hardware_platform_bus;

namespace {

static const std::vector<fpbus::Mmio> dsi_mmios{
    {{
        // DSI Host Controller
        .base = S905D2_MIPI_DSI_BASE,
        .length = S905D2_MIPI_DSI_LENGTH,
    }},
};

static const fpbus::Node dsi_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "dw-dsi";
  dev.vid() = PDEV_VID_GENERIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_DW_DSI;
  dev.mmio() = dsi_mmios;
  return dev;
}();

}  // namespace

zx_status_t Astro::DsiInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('DSI_');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, dsi_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Dsi(dsi_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Dsi(dsi_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace astro
