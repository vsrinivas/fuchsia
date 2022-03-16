// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <bind/fuchsia/ams/platform/cpp/fidl.h>
#include <ddk/metadata/lights.h>
#include <ddktl/metadata/light-sensor.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d3/s905d3-pwm.h>

#include "nelson-gpios.h"
#include "nelson.h"
#include "src/devices/board/drivers/nelson/nelson_gpio_light_bind.h"
#include "src/devices/board/drivers/nelson/nelson_tcs3400_light_bind.h"

namespace nelson {

// Composite binding rules for focaltech touch driver.

using LightName = char[ZX_MAX_NAME_LEN];
constexpr LightName kLightNames[] = {"AMBER_LED"};
constexpr LightsConfig kConfigs[] = {
    {.brightness = true, .rgb = false, .init_on = true, .group_id = -1},
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
  pbus_dev_t result = {};
  result.name = "gpio-light";
  result.vid = PDEV_VID_AMLOGIC;
  result.pid = PDEV_PID_GENERIC;
  result.did = PDEV_DID_GPIO_LIGHT;
  result.metadata_list = light_metadata;
  result.metadata_count = std::size(light_metadata);
  return result;
}();

zx_status_t Nelson::LightInit() {
  metadata::LightSensorParams params = {};
  // TODO(kpt): Insert the right parameters here.
  params.integration_time_us = 711'680;
  params.gain = 16;
  params.polling_time_us = 100'000;
  device_metadata_t metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data = &params,
          .length = sizeof(params),
      },
  };
  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, bind::fuchsia::ams::platform::BIND_PLATFORM_DEV_VID_AMS},
      {BIND_PLATFORM_DEV_PID, 0, bind::fuchsia::ams::platform::BIND_PLATFORM_DEV_PID_TCS3400},
      {BIND_PLATFORM_DEV_DID, 0, bind::fuchsia::ams::platform::BIND_PLATFORM_DEV_DID_LIGHT},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = std::size(props),
      .fragments = tcs3400_light_fragments,
      .fragments_count = std::size(tcs3400_light_fragments),
      .primary_fragment = "i2c",
      .spawn_colocated = false,
      .metadata_list = metadata,
      .metadata_count = std::size(metadata),
  };

  zx_status_t status = DdkAddComposite("tcs3400-light", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s(tcs-3400): DdkAddComposite failed: %d", __func__, status);
    return status;
  }

  // Enable the Amber LED so it will be controlled by PWM.
  status = gpio_impl_.SetAltFunction(GPIO_AMBER_LED_PWM, 3);  // Set as PWM.
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Configure mute LED GPIO failed %d", __func__, status);
  }

  // GPIO must be set to default out otherwise could cause light to not work
  // on certain reboots.
  status = gpio_impl_.ConfigOut(GPIO_AMBER_LED_PWM, 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Configure mute LED GPIO on failed %d", __func__, status);
  }

  status = pbus_.AddComposite(&light_dev, reinterpret_cast<uint64_t>(gpio_light_fragments),
                              std::size(gpio_light_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace nelson
