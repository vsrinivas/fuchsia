// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/metadata.h>
#include <ddktl/metadata/light-sensor.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

namespace sherlock {

zx_status_t Sherlock::LightInit() {
  constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };
  constexpr zx_bind_inst_t gpio_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_LIGHT_INTERRUPT),
  };
  constexpr zx_bind_inst_t i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, SHERLOCK_I2C_A0_0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x39),
  };
  const device_component_part_t gpio_component[] = {
      {countof(root_match), root_match},
      {countof(gpio_match), gpio_match},
  };
  const device_component_part_t i2c_component[] = {
      {countof(root_match), root_match},
      {countof(i2c_match), i2c_match},
  };
  const device_component_t components[] = {
      {countof(i2c_component), i2c_component},
      {countof(gpio_component), gpio_component},
  };

  metadata::LightSensorParams params = {};
  // TODO(kpt): Insert the right parameters here.
  params.lux_constant_coefficient = 0;
  params.lux_linear_coefficient = .29f;
  params.integration_time_ms = 615;
  pbus_metadata_t metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data_buffer = &params,
          .data_size = sizeof(params),
      },
  };

  pbus_dev_t dev = {};
  dev.name = "SherlockLightSensor";
  dev.vid = PDEV_VID_AMS;
  dev.pid = PDEV_PID_AMS_TCS3400;
  dev.did = PDEV_DID_AMS_LIGHT;
  dev.metadata_list = metadata;
  dev.metadata_count = countof(metadata);
  auto status = pbus_.CompositeDeviceAdd(&dev, components, countof(components), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s CompositeDeviceAdd failed %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
