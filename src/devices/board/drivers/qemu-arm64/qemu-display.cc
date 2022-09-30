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
#include "src/devices/board/drivers/qemu-arm64/qemu_bus_bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace board_qemu_arm64 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
static const device_fragment_part_t sysmem_fragment[] = {
    {std::size(sysmem_match), sysmem_match},
};
static const device_fragment_t fragments[] = {
    {"sysmem", std::size(sysmem_fragment), sysmem_fragment},
};

zx_status_t QemuArm64::DisplayInit() {
  fpbus::Node display_dev;
  display_dev.name() = "display";
  display_dev.vid() = PDEV_VID_GENERIC;
  display_dev.pid() = PDEV_PID_GENERIC;
  display_dev.did() = PDEV_DID_FAKE_DISPLAY;
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('DISP');
  auto result = pbus_.buffer(arena)->AddCompositeImplicitPbusFragment(
      fidl::ToWire(fidl_arena, display_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, fragments, std::size(fragments)), {});
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Display(display_dev) request failed: %s",
           __func__, result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Display(display_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace board_qemu_arm64
