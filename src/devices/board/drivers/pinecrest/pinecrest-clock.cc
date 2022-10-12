// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/clock.h>
#include <soc/as370/as370-clk.h>
#include <soc/as370/as370-gpio.h>
#include <soc/as370/as370-hw.h>

#include "pinecrest.h"

namespace board_pinecrest {
namespace fpbus = fuchsia_hardware_platform_bus;

zx_status_t Pinecrest::ClockInit() {
  static const std::vector<fpbus::Mmio> clock_mmios{
      {{
          .base = as370::kGlobalBase,
          .length = as370::kGlobalSize,
      }},
      {{
          .base = as370::kAudioGlobalBase,
          .length = as370::kAudioGlobalSize,
      }},
      {{
          .base = as370::kCpuBase,
          .length = as370::kCpuSize,
      }},
  };
  static const clock_id_t clock_ids[] = {
      {as370::As370Clk::kClkAvpll0},
      {as370::As370Clk::kClkAvpll1},
      {as370::As370Clk::kClkCpu},
  };
  static const std::vector<fpbus::Metadata> clock_metadata{
      {{
          .type = DEVICE_METADATA_CLOCK_IDS,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&clock_ids),
              reinterpret_cast<const uint8_t*>(&clock_ids) + sizeof(clock_ids)),
      }},
  };

  fpbus::Node dev;
  dev.name() = "pinecrest-clock";
  dev.vid() = PDEV_VID_SYNAPTICS;
  dev.did() = PDEV_DID_AS370_CLOCK;
  dev.mmio() = clock_mmios;
  dev.metadata() = clock_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('CLOC');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Clock(dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Clock(dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace board_pinecrest
