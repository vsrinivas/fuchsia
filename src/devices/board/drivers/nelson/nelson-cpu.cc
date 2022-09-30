// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include "nelson.h"

namespace nelson {
namespace fpbus = fuchsia_hardware_platform_bus;

static const fpbus::Node cpu_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "nelson-cpu";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_S905D3;
  dev.did() = PDEV_DID_AMLOGIC_CPU;
  return dev;
}();

zx_status_t Nelson::CpuInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('CPU_');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, cpu_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Cpu(cpu_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Cpu(cpu_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace nelson
