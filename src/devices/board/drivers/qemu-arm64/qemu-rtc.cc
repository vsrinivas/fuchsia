// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include "qemu-bus.h"
#include "qemu-virt.h"

namespace board_qemu_arm64 {
namespace fpbus = fuchsia_hardware_platform_bus;

zx_status_t QemuArm64::RtcInit() {
  static const std::vector<fpbus::Mmio> kPl031Mmios{
      {{
          .base = RTC_BASE_PHYS,
          .length = RTC_SIZE,
      }},
  };
  fpbus::Node pl031_dev;
  pl031_dev.name() = "pl031";
  pl031_dev.vid() = PDEV_VID_GENERIC;
  pl031_dev.pid() = PDEV_PID_GENERIC;
  pl031_dev.did() = PDEV_DID_RTC_PL031;
  pl031_dev.mmio() = kPl031Mmios;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('RTC_');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, pl031_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Rtc(pl031_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Rtc(pl031_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace board_qemu_arm64
