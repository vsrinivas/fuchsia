// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test.h"

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/platform-defs.h>

#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"
#include "test-resources.h"

namespace board_test {
namespace fpbus = fuchsia_hardware_platform_bus;

zx_status_t TestBoard::TestInit() {
  fpbus::Node test_dev;
  test_dev.name() = "test-parent";
  test_dev.vid() = PDEV_VID_TEST;
  test_dev.pid() = PDEV_PID_PBUS_TEST;
  test_dev.did() = PDEV_DID_TEST_PARENT;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('TEST');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, test_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: DeviceAdd Test request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: DeviceAdd Test failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace board_test
