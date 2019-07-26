// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include <fbl/algorithm.h>
#include <lib/focaltech/focaltech.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <limits.h>
#include <unistd.h>

#include "astro.h"
#include "astro-gpios.h"

namespace astro {

static const uint32_t device_id = FOCALTECH_DEVICE_FT3X27;
static const pbus_metadata_t ft3x27_touch_metadata[] = {
    {.type = DEVICE_METADATA_PRIVATE, .data_buffer = &device_id, .data_size = sizeof(device_id)},
};

const pbus_dev_t ft3x27_touch_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "ft3x27-touch";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_ASTRO;
  dev.did = PDEV_DID_FOCALTOUCH;
  dev.metadata_list = ft3x27_touch_metadata;
  dev.metadata_count = fbl::count_of(ft3x27_touch_metadata);
  return dev;
}();

// Composite binding rules for focaltech touch driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
const zx_bind_inst_t ft_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, ASTRO_I2C_2),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, I2C_FOCALTECH_TOUCH_ADDR),
};
const zx_bind_inst_t goodix_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, ASTRO_I2C_2),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, I2C_GOODIX_TOUCH_ADDR),
};
static const zx_bind_inst_t gpio_int_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_TOUCH_INTERRUPT),
};
static const zx_bind_inst_t gpio_reset_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_TOUCH_RESET),
};
static const device_component_part_t ft_i2c_component[] = {
    {countof(root_match), root_match},
    {countof(ft_i2c_match), ft_i2c_match},
};
static const device_component_part_t goodix_i2c_component[] = {
    {countof(root_match), root_match},
    {countof(goodix_i2c_match), goodix_i2c_match},
};
static const device_component_part_t gpio_int_component[] = {
    {countof(root_match), root_match},
    {countof(gpio_int_match), gpio_int_match},
};
static const device_component_part_t gpio_reset_component[] = {
    {countof(root_match), root_match},
    {countof(gpio_reset_match), gpio_reset_match},
};
static const device_component_t ft_components[] = {
    {countof(ft_i2c_component), ft_i2c_component},
    {countof(gpio_int_component), gpio_int_component},
    {countof(gpio_reset_component), gpio_reset_component},
};
static const device_component_t goodix_components[] = {
    {countof(goodix_i2c_component), goodix_i2c_component},
    {countof(gpio_int_component), gpio_int_component},
    {countof(gpio_reset_component), gpio_reset_component},
};

zx_status_t Astro::TouchInit() {
  // Check the display ID pin to determine which driver device to add
  gpio_impl_.SetAltFunction(S905D2_GPIOH(5), 0);
  gpio_impl_.ConfigIn(S905D2_GPIOH(5), GPIO_NO_PULL);
  uint8_t gpio_state;
  /* Two variants of display are supported, one with BOE display panel and
        ft3x27 touch controller, the other with INX panel and Goodix touch
        controller.  This GPIO input is used to identify each.
        Logic 0 for BOE/ft3x27 combination
        Logic 1 for Innolux/Goodix combination
  */
  gpio_impl_.Read(S905D2_GPIOH(5), &gpio_state);

  if (gpio_state) {
    const zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GOOGLE},
        {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_ASTRO},
        {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_ASTRO_GOODIXTOUCH},
    };
    zx_status_t status = DdkAddComposite("gt92xx-touch", props, countof(props), goodix_components,
                                         countof(goodix_components), UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(INFO, "astro_touch_init(gt92xx): composite_device_add failed: %d\n", status);
      return status;
    }
  } else {
    // platform device protocol is only needed to provide metadata to the
    // driver.
    // TODO(voydanoff) remove pdev after we have a better way to provide
    // metadata to composite devices.
    zx_status_t status = pbus_.CompositeDeviceAdd(&ft3x27_touch_dev, ft_components,
                                                  countof(ft_components), UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s(ft3x27): CompositeDeviceAdd failed: %d\n", __func__, status);
      return status;
    }
  }

  return ZX_OK;
}

}  // namespace astro
