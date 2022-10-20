// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a5/a5-hw.h>

#include "buckeye.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace buckeye {
namespace fpbus = fuchsia_hardware_platform_bus;

zx_status_t Buckeye::RtcInit() {
  static const std::vector<fpbus::Mmio> rtc_mmios{
      {{
          .base = A5_RTC_BASE,
          .length = A5_RTC_LENGTH,
      }},
  };

  static const std::vector<fpbus::Irq> rtc_irqs{
      {{
          .irq = A5_RTC_IRQ,
          .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
      }},
  };

  fpbus::Node amlrtc_dev;
  amlrtc_dev.name() = "amlrtc";
  amlrtc_dev.vid() = PDEV_VID_AMLOGIC;
  amlrtc_dev.pid() = PDEV_PID_AMLOGIC_A5;
  amlrtc_dev.did() = PDEV_DID_AMLOGIC_RTC;
  amlrtc_dev.mmio() = rtc_mmios;
  amlrtc_dev.irq() = rtc_irqs;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('RTC_');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, amlrtc_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Rtc(amlrtc_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Rtc(amlrtc_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace buckeye
