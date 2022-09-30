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
#include <zircon/compiler.h>

#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson.h"
#include "src/devices/board/drivers/nelson/nelson_backlight_bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"
#include "src/ui/backlight/drivers/ti-lp8556/ti-lp8556Metadata.h"

namespace nelson {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> backlight_mmios{
    {{
        .base = S905D3_GPIO_AO_BASE,
        .length = S905D3_GPIO_AO_LENGTH,
    }},
};

static const std::vector<fpbus::BootMetadata> backlight_boot_metadata{
    {{
        .zbi_type = DEVICE_METADATA_BOARD_PRIVATE,
        .zbi_extra = 0,
    }},
};

constexpr double kMaxBrightnessInNits = 250.0;

zx_status_t Nelson::BacklightInit() {
  TiLp8556Metadata device_metadata = {
      .allow_set_current_scale = false,
      .registers =
          {
              // Registers
              0x01, 0x85,  // Device Control
                           // EPROM
              0xa2, 0x30,  // CFG2
              0xa3, 0x32,  // CFG3
              0xa5, 0x54,  // CFG5
              0xa7, 0xf4,  // CFG7
              0xa9, 0x60,  // CFG9
              0xae, 0x09,  // CFGE
          },
      .register_count = 14,
  };

  std::vector<fpbus::Metadata> backlight_metadata{
      {{
          .type = DEVICE_METADATA_BACKLIGHT_MAX_BRIGHTNESS_NITS,
          .data = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&kMaxBrightnessInNits),
                                       reinterpret_cast<const uint8_t*>(&kMaxBrightnessInNits) +
                                           sizeof(kMaxBrightnessInNits)),
      }},
      {{
          .type = DEVICE_METADATA_PRIVATE,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&device_metadata),
              reinterpret_cast<const uint8_t*>(&device_metadata) + sizeof(device_metadata)),
      }},
  };

  fpbus::Node backlight_dev;
  backlight_dev.name() = "backlight";
  backlight_dev.vid() = PDEV_VID_TI;
  backlight_dev.pid() = PDEV_PID_TI_LP8556;
  backlight_dev.did() = PDEV_DID_TI_BACKLIGHT;
  backlight_dev.mmio() = backlight_mmios;
  backlight_dev.metadata() = backlight_metadata;
  backlight_dev.boot_metadata() = backlight_boot_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('BACK');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, backlight_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, backlight_fragments,
                                               std::size(backlight_fragments)),
      "i2c");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Backlight(backlight_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Backlight(backlight_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace nelson
