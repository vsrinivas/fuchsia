// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/clock.h>

#include "test.h"

namespace board_test {

namespace {
static const clock_id_t clock_ids[] = {
    {1},
    {3},
    {5},
    {7},
};

static const std::vector<fuchsia_hardware_platform_bus::Metadata> clock_metadata{
    []() {
      fuchsia_hardware_platform_bus::Metadata ret;
      ret.type() = DEVICE_METADATA_CLOCK_IDS;
      ret.data() =
          std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&clock_ids),
                               reinterpret_cast<const uint8_t*>(&clock_ids) + sizeof(clock_ids));
      return ret;
    }(),
};
}  // namespace

zx_status_t TestBoard::ClockInit() {
  fuchsia_hardware_platform_bus::Node clock_dev = {};
  clock_dev.name() = "clock";
  clock_dev.vid() = PDEV_VID_TEST;
  clock_dev.pid() = PDEV_PID_PBUS_TEST;
  clock_dev.did() = PDEV_DID_TEST_CLOCK;
  clock_dev.metadata() = clock_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('TCLK');

  auto result = pbus_.buffer(arena)->ProtocolNodeAdd(ZX_PROTOCOL_CLOCK_IMPL,
                                                     fidl::ToWire(fidl_arena, clock_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: ProtocolNodeAdd request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: ProtocolNodeAdd failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace board_test
