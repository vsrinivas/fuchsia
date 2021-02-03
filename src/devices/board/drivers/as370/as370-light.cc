// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/lights.h>
#include <ddk/platform-defs.h>

#include "as370.h"
#include "src/devices/board/drivers/as370/as370-bind.h"

namespace board_as370 {

zx_status_t As370::LightInit() {
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
  static const pbus_metadata_t light_metadata[] = {
      {
          .type = DEVICE_METADATA_LIGHTS,
          .data_buffer = &kConfigs,
          .data_size = sizeof(kConfigs),
      },
      {
          .type = DEVICE_METADATA_LIGHTS_GROUP_NAME,
          .data_buffer = &kLightGroupNames,
          .data_size = sizeof(kLightGroupNames),
      },
  };

  // Composite binding rules for TI LED driver.
  static const zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };

  static const zx_bind_inst_t i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0x0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x29),
  };

  static const device_fragment_part_t i2c_fragment[] = {
      {countof(root_match), root_match},
      {countof(i2c_match), i2c_match},
  };

  static const device_fragment_t fragments[] = {
      {"i2c", countof(i2c_fragment), i2c_fragment},
  };

  pbus_dev_t light_dev = {};
  light_dev.name = "lp5018-light";
  light_dev.vid = PDEV_VID_TI;
  light_dev.pid = PDEV_PID_TI_LP5018;
  light_dev.did = PDEV_DID_TI_LED;
  light_dev.metadata_list = light_metadata;
  light_dev.metadata_count = countof(light_metadata);

  status = pbus_.CompositeDeviceAdd(&light_dev, reinterpret_cast<uint64_t>(fragments),
                                    countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_as370
