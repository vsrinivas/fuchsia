// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/lights.h>

#include "pinecrest.h"
#include "src/devices/board/drivers/pinecrest/pinecrest-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace board_pinecrest {
namespace fpbus = fuchsia_hardware_platform_bus;

zx_status_t Pinecrest::LightInit() {
  // setup LED/Touch reset pin
  auto status = gpio_impl_.SetAltFunction(4, 0);  // 0 - GPIO mode
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GPIO SetAltFunction failed: %d", __func__, status);
    return status;
  }

  // Reset LED/Touch device
  // Note: GPIO is shared between LED and Touch. Hence reset is done only here.
  gpio_impl_.Write(4, 1);
  gpio_impl_.Write(4, 0);
  gpio_impl_.Write(4, 1);

  constexpr LightsConfig kConfigs[] = {
      {.brightness = true, .rgb = true, .init_on = false, .group_id = 1},
      {.brightness = true, .rgb = true, .init_on = false, .group_id = 0},
      {.brightness = true, .rgb = true, .init_on = false, .group_id = 0},
      {.brightness = true, .rgb = true, .init_on = false, .group_id = 0},
      {.brightness = true, .rgb = true, .init_on = false, .group_id = 0},
      {.brightness = true, .rgb = true, .init_on = false, .group_id = 1},
  };
  using LightName = char[ZX_MAX_NAME_LEN];
  constexpr LightName kLightGroupNames[] = {"GROUP_OF_4", "GROUP_OF_2"};
  std::vector<fpbus::Metadata> light_metadata{
      {{
          .type = DEVICE_METADATA_LIGHTS,
          .data =
              std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&kConfigs),
                                   reinterpret_cast<const uint8_t*>(&kConfigs) + sizeof(kConfigs)),
      }},
      {{
          .type = DEVICE_METADATA_LIGHTS_GROUP_NAME,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&kLightGroupNames),
              reinterpret_cast<const uint8_t*>(&kLightGroupNames) + sizeof(kLightGroupNames)),
      }},
  };

  // Composite binding rules for TI LED driver.

  static const zx_bind_inst_t i2c_match[] = {
      BI_ABORT_IF(NE, BIND_FIDL_PROTOCOL, ZX_FIDL_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0x0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x29),
  };

  static const device_fragment_part_t i2c_fragment[] = {
      {std::size(i2c_match), i2c_match},
  };

  static const device_fragment_t fragments[] = {
      {"i2c", std::size(i2c_fragment), i2c_fragment},
  };

  fpbus::Node light_dev;
  light_dev.name() = "lp5018-light";
  light_dev.vid() = PDEV_VID_TI;
  light_dev.pid() = PDEV_PID_TI_LP5018;
  light_dev.did() = PDEV_DID_TI_LED;
  light_dev.metadata() = light_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('LIGH');
  auto result = pbus_.buffer(arena)->AddCompositeImplicitPbusFragment(
      fidl::ToWire(fidl_arena, light_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, fragments, std::size(fragments)), {});
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Light(light_dev) request failed: %s",
           __func__, result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Light(light_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace board_pinecrest
