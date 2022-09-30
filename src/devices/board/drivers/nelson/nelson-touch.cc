// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include "nelson.h"
#include "src/devices/board/drivers/nelson/gt6853_touch_bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace nelson {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::BootMetadata> touch_boot_metadata{
    {{
        .zbi_type = DEVICE_METADATA_BOARD_PRIVATE,
        .zbi_extra = 0,
    }},
};

zx_status_t Nelson::TouchInit() {
  const uint32_t display_id = GetDisplayId();
  zxlogf(INFO, "Board rev: %u", GetBoardRev());
  zxlogf(INFO, "Panel ID: 0b%d%d", display_id & 0b10 ? 1 : 0, display_id & 0b01 ? 1 : 0);

  fpbus::Node touch_dev;
  touch_dev.name() = "gt6853-touch";
  touch_dev.vid() = PDEV_VID_GOODIX;
  touch_dev.did() = PDEV_DID_GOODIX_GT6853;
  touch_dev.boot_metadata() = touch_boot_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('TOUC');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, touch_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, gt6853_touch_fragments,
                                               std::size(gt6853_touch_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Touch(touch_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Touch(touch_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace nelson
