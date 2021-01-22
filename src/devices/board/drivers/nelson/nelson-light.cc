// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bind/fuchsia/ams/platform/cpp/fidl.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/lights.h>
#include <ddk/platform-defs.h>
#include <ddktl/metadata/light-sensor.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d3/s905d3-pwm.h>

#include "nelson-gpios.h"
#include "nelson.h"

namespace nelson {

// Composite binding rules for focaltech touch driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};

const zx_bind_inst_t i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, NELSON_I2C_A0_0),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, I2C_AMBIENTLIGHT_ADDR),
};

static const zx_bind_inst_t gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_LIGHT_INTERRUPT),
};

static const device_fragment_part_t i2c_fragment[] = {
    {countof(root_match), root_match},
    {countof(i2c_match), i2c_match},
};

static const device_fragment_part_t gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(gpio_match), gpio_match},
};

static const device_fragment_t fragments[] = {
    {"i2c", countof(i2c_fragment), i2c_fragment},
    {"gpio", countof(gpio_fragment), gpio_fragment},
};

using LightName = char[ZX_MAX_NAME_LEN];
constexpr LightName kLightNames[] = {"AMBER_LED"};
constexpr LightsConfig kConfigs[] = {
    {.brightness = true, .rgb = false, .init_on = true, .group_id = -1},
};

static constexpr pbus_metadata_t light_metadata[] = {
    {
        .type = DEVICE_METADATA_NAME,
        .data_buffer = &kLightNames,
        .data_size = sizeof(kLightNames),
    },
    {
        .type = DEVICE_METADATA_LIGHTS,
        .data_buffer = &kConfigs,
        .data_size = sizeof(kConfigs),
    },
};

constexpr zx_bind_inst_t amber_led_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_AMBER_LED),
};
constexpr zx_bind_inst_t amber_led_pwm_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
    BI_MATCH_IF(EQ, BIND_PWM_ID, S905D3_PWM_AO_A),
};
constexpr device_fragment_part_t amber_led_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(amber_led_gpio_match), amber_led_gpio_match},
};
constexpr device_fragment_part_t amber_led_pwm_fragment[] = {
    {countof(root_match), root_match},
    {countof(amber_led_pwm_match), amber_led_pwm_match},
};
const device_fragment_t light_fragments[] = {
    {"gpio", countof(amber_led_gpio_fragment), amber_led_gpio_fragment},
    {"pwm", countof(amber_led_pwm_fragment), amber_led_pwm_fragment},
};

static const pbus_dev_t light_dev = []() {
  pbus_dev_t result = {};
  result.name = "gpio-light";
  result.vid = PDEV_VID_AMLOGIC;
  result.pid = PDEV_PID_GENERIC;
  result.did = PDEV_DID_GPIO_LIGHT;
  result.metadata_list = light_metadata;
  result.metadata_count = countof(light_metadata);
  return result;
}();

zx_status_t Nelson::LightInit() {
  metadata::LightSensorParams params = {};
  // TODO(kpt): Insert the right parameters here.
  params.integration_time_ms = 615;
  params.gain = 16;
  params.polling_time_ms = 100;
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
      .props_count = countof(props),
      .fragments = fragments,
      .fragments_count = countof(fragments),
      .coresident_device_index = UINT32_MAX,
      .metadata_list = metadata,
      .metadata_count = countof(metadata),
  };

  zx_status_t status = DdkAddComposite("tcs3400-light", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s(tcs-3400): DdkAddComposite failed: %d", __func__, status);
    return status;
  }

  // Enable the Amber LED so it will be controlled by PWM.
  status = gpio_impl_.SetAltFunction(GPIO_AMBER_LED, 3);  // Set as PWM.
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Configure mute LED GPIO failed %d", __func__, status);
  }

  // GPIO must be set to default out otherwise could cause light to not work
  // on certain reboots.
  status = gpio_impl_.ConfigOut(GPIO_AMBER_LED, 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Configure mute LED GPIO on failed %d", __func__, status);
  }

  status =
      pbus_.CompositeDeviceAdd(&light_dev, light_fragments, countof(light_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace nelson
