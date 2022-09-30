// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include "test.h"

namespace board_test {
namespace fpbus = fuchsia_hardware_platform_bus;

zx_status_t TestBoard::VregInit() {
  fpbus::Node vreg_dev;
  vreg_dev.name() = "vreg";
  vreg_dev.vid() = PDEV_VID_TEST;
  vreg_dev.pid() = PDEV_PID_PBUS_TEST;
  vreg_dev.did() = PDEV_DID_TEST_VREG;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('TREG');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, vreg_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: DeviceAdd Vreg request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: DeviceAdd Vreg failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace board_test
