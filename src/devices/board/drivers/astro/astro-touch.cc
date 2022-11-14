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
#include <lib/focaltech/focaltech.h>
#include <limits.h>
#include <unistd.h>

#include <bind/fuchsia/amlogic/platform/s905d2/cpp/bind.h>
#include <bind/fuchsia/cpp/bind.h>
#include <bind/fuchsia/gpio/cpp/bind.h>
#include <bind/fuchsia/i2c/cpp/bind.h>
#include <fbl/algorithm.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro-gpios.h"
#include "astro.h"
#include "src/devices/board/drivers/astro/gt92xx-touch-bind.h"

namespace astro {
namespace fpbus = fuchsia_hardware_platform_bus;

static const FocaltechMetadata device_info = {
    .device_id = FOCALTECH_DEVICE_FT3X27,
    .needs_firmware = false,
};
static const device_metadata_t ft3x27_touch_metadata[] = {
    {.type = DEVICE_METADATA_PRIVATE, .data = &device_info, .length = sizeof(device_info)},
};

const ddk::DeviceGroupBindRule ft3x27_i2c_bind_rules[] = {
    ddk::MakeAcceptBindRule(bind_fuchsia::FIDL_PROTOCOL,
                            bind_fuchsia_i2c::BIND_FIDL_PROTOCOL_DEVICE),
    ddk::MakeAcceptBindRule(bind_fuchsia::I2C_BUS_ID, bind_fuchsia_i2c::BIND_I2C_BUS_ID_ASTRO_2),
    ddk::MakeAcceptBindRule(bind_fuchsia::I2C_ADDRESS,
                            bind_fuchsia_i2c::BIND_I2C_ADDRESS_FOCALTECH_TOUCH),
};

const device_bind_prop_t ft3x27_i2c_bind_properties[] = {
    ddk::MakeProperty(bind_fuchsia::FIDL_PROTOCOL, bind_fuchsia_i2c::BIND_FIDL_PROTOCOL_DEVICE),
};

const ddk::DeviceGroupBindRule gpio_interrupt_bind_rules[] = {
    ddk::MakeAcceptBindRule(bind_fuchsia::PROTOCOL, bind_fuchsia_gpio::BIND_PROTOCOL_DEVICE),
    ddk::MakeAcceptBindRule(bind_fuchsia::GPIO_PIN,
                            bind_fuchsia_amlogic_platform_s905d2::GPIOZ_PIN_ID_PIN_4),
};

const device_bind_prop_t gpio_interrupt_bind_properties[] = {
    ddk::MakeProperty(bind_fuchsia::PROTOCOL, bind_fuchsia_gpio::BIND_PROTOCOL_DEVICE),
    ddk::MakeProperty(bind_fuchsia_gpio::FUNCTION, bind_fuchsia_gpio::FUNCTION_TOUCH_INTERRUPT)};

const ddk::DeviceGroupBindRule gpio_reset_bind_rules[] = {
    ddk::MakeAcceptBindRule(bind_fuchsia::PROTOCOL, bind_fuchsia_gpio::BIND_PROTOCOL_DEVICE),
    ddk::MakeAcceptBindRule(bind_fuchsia::GPIO_PIN,
                            bind_fuchsia_amlogic_platform_s905d2::GPIOZ_PIN_ID_PIN_9),
};

const device_bind_prop_t gpio_reset_bind_properties[] = {
    ddk::MakeProperty(bind_fuchsia::PROTOCOL, bind_fuchsia_gpio::BIND_PROTOCOL_DEVICE),
    ddk::MakeProperty(bind_fuchsia_gpio::FUNCTION, bind_fuchsia_gpio::FUNCTION_TOUCH_RESET),
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

    const composite_device_desc_t comp_desc = {
        .props = props,
        .props_count = std::size(props),
        .fragments = gt92xx_touch_fragments,
        .fragments_count = std::size(gt92xx_touch_fragments),
        .primary_fragment = "i2c",
        .spawn_colocated = false,
        .metadata_list = nullptr,
        .metadata_count = 0,
    };

    zx_status_t status = DdkAddComposite("gt92xx-touch", &comp_desc);
    if (status != ZX_OK) {
      zxlogf(INFO, "astro_touch_init(gt92xx): composite_device_add failed: %d", status);
      return status;
    }
  } else {
    auto status = DdkAddDeviceGroup(
        "ft3x27_touch", ddk::DeviceGroupDesc(ft3x27_i2c_bind_rules, ft3x27_i2c_bind_properties)
                            .AddNode(gpio_interrupt_bind_rules, gpio_interrupt_bind_properties)
                            .AddNode(gpio_reset_bind_rules, gpio_reset_bind_properties)
                            .set_metadata(ft3x27_touch_metadata)
                            .set_spawn_colocated(false));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s(ft3x27): DdkAddDeviceGroup failed: %d", __func__, status);
      return status;
    }
  }

  return ZX_OK;
}

}  // namespace astro
