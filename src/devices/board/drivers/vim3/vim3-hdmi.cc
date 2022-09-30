// Copyright 2021 The Fuchsia Authors. All rights reserved.
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

#include "vim3.h"

namespace vim3 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> hdmi_mmios{
    {{
        // HDMITX
        .base = A311D_HDMITX_BASE,
        .length = A311D_HDMITX_LENGTH,
    }},
};

static const fpbus::Node hdmi_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-hdmi";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A311D;
  dev.did() = PDEV_DID_AMLOGIC_HDMI;
  dev.mmio() = hdmi_mmios;
  return dev;
}();

zx_status_t Vim3::HdmiInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('HDMI');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, hdmi_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Hdmi(hdmi_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Hdmi(hdmi_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace vim3
