// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/compiler.h>

#include <bind/fuchsia/ams/platform/cpp/bind.h>
#include <ddk/metadata/lights.h>
#include <ddktl/metadata/light-sensor.h>
#include <soc/aml-t931/t931-pwm.h>

#include "sherlock-gpios.h"
#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/sherlock-gpio-light-bind.h"
#include "src/devices/board/drivers/sherlock/sherlock-light-sensor-bind.h"

namespace sherlock {

zx_status_t Sherlock::LightInit() {
  metadata::LightSensorParams params = {};
  // TODO(kpt): Insert the right parameters here.
  params.integration_time_us = 711'680;
  params.gain = 16;
  params.polling_time_us = 100'000;
  device_metadata_t metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data = reinterpret_cast<uint8_t*>(&params),
          .length = sizeof(params),
      },
  };
  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, bind_fuchsia_ams_platform::BIND_PLATFORM_DEV_VID_AMS},
      {BIND_PLATFORM_DEV_PID, 0, bind_fuchsia_ams_platform::BIND_PLATFORM_DEV_PID_TCS3400},
      {BIND_PLATFORM_DEV_DID, 0, bind_fuchsia_ams_platform::BIND_PLATFORM_DEV_DID_LIGHT},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = std::size(props),
      .fragments = sherlock_light_sensor_fragments,
      .fragments_count = std::size(sherlock_light_sensor_fragments),
      .primary_fragment = "i2c",
      .spawn_colocated = false,
      .metadata_list = metadata,
      .metadata_count = std::size(metadata),
  };

  zx_status_t status = DdkAddComposite("SherlockLightSensor", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite failed: %d", __func__, status);
    return status;
  }

  // Lights
  // Instructions: include fragments in this order
  //     GPIO fragment
  //     BRIGHTNESS capable--include PWM fragment
  //     RGB capable--include RGB fragment
  //   Set GPIO alternative function here!
  using LightName = char[ZX_MAX_NAME_LEN];
  constexpr LightName kLightNames[] = {"AMBER_LED", "GREEN_LED"};
  constexpr LightsConfig kConfigs[] = {
      {.brightness = true, .rgb = false, .init_on = true, .group_id = -1},
      {.brightness = true, .rgb = false, .init_on = false, .group_id = -1},
  };
  static const pbus_metadata_t light_metadata[] = {
      {
          .type = DEVICE_METADATA_NAME,
          .data_buffer = reinterpret_cast<const uint8_t*>(&kLightNames),
          .data_size = sizeof(kLightNames),
      },
      {
          .type = DEVICE_METADATA_LIGHTS,
          .data_buffer = reinterpret_cast<const uint8_t*>(&kConfigs),
          .data_size = sizeof(kConfigs),
      },
  };

  static const pbus_dev_t light_dev = []() {
    pbus_dev_t dev = {};
    dev.name = "gpio-light";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_GPIO_LIGHT;
    dev.metadata_list = light_metadata;
    dev.metadata_count = std::size(light_metadata);
    return dev;
  }();

  // Enable the Amber LED so it will be controlled by PWM.
  status = gpio_impl_.SetAltFunction(GPIO_AMBER_LED, 3);  // Set as GPIO.
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Configure mute LED GPIO failed %d", __func__, status);
  }
  status = gpio_impl_.ConfigOut(GPIO_AMBER_LED, 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Configure mute LED GPIO on failed %d", __func__, status);
  }
  // Enable the Green LED so it will be controlled by PWM.
  status = gpio_impl_.SetAltFunction(GPIO_GREEN_LED, 4);  // Set as PWM.
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Configure mute LED GPIO failed %d", __func__, status);
  }
  status = gpio_impl_.ConfigOut(GPIO_GREEN_LED, 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Configure mute LED GPIO on failed %d", __func__, status);
  }

  status = pbus_.AddComposite(&light_dev, reinterpret_cast<uint64_t>(gpio_light_fragments),
                              std::size(gpio_light_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: AddComposite failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
