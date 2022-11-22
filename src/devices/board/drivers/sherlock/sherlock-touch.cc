// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/focaltech/focaltech.h>
#include <limits.h>
#include <unistd.h>

#include <bind/fuchsia/amlogic/platform/t931/cpp/bind.h>
#include <bind/fuchsia/cpp/bind.h>
#include <bind/fuchsia/gpio/cpp/bind.h>
#include <bind/fuchsia/i2c/cpp/bind.h>
#include <fbl/algorithm.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

namespace sherlock {

const uint32_t kI2cAddressValues[] = {bind_fuchsia_i2c::BIND_I2C_ADDRESS_FOCALTECH_TOUCH,
                                      bind_fuchsia_i2c::BIND_I2C_ADDRESS_TI_INA231_SPEAKERS};

const ddk::NodeGroupBindRule kI2cRules[] = {
    ddk::MakeAcceptBindRule(bind_fuchsia::FIDL_PROTOCOL,
                            bind_fuchsia_i2c::BIND_FIDL_PROTOCOL_DEVICE),
    ddk::MakeAcceptBindRule(bind_fuchsia::I2C_BUS_ID, static_cast<uint32_t>(SHERLOCK_I2C_2)),
    ddk::MakeAcceptBindRuleList(bind_fuchsia::I2C_ADDRESS, kI2cAddressValues),
};

const device_bind_prop_t kI2cProperties[] = {
    ddk::MakeProperty(bind_fuchsia::FIDL_PROTOCOL, bind_fuchsia_i2c::BIND_FIDL_PROTOCOL_DEVICE),
    ddk::MakeProperty(bind_fuchsia::I2C_ADDRESS,
                      bind_fuchsia_i2c::BIND_I2C_ADDRESS_FOCALTECH_TOUCH),
};

const ddk::NodeGroupBindRule kInterruptRules[] = {
    ddk::MakeAcceptBindRule(bind_fuchsia::PROTOCOL, bind_fuchsia_gpio::BIND_PROTOCOL_DEVICE),
    ddk::MakeAcceptBindRule(bind_fuchsia::GPIO_PIN,
                            bind_fuchsia_amlogic_platform_t931::GPIOZ_PIN_ID_PIN_1),
};

const device_bind_prop_t kInterruptProperties[] = {
    ddk::MakeProperty(bind_fuchsia::PROTOCOL, bind_fuchsia_gpio::BIND_PROTOCOL_DEVICE),
    ddk::MakeProperty(bind_fuchsia_gpio::FUNCTION, bind_fuchsia_gpio::FUNCTION_TOUCH_INTERRUPT)};

const ddk::NodeGroupBindRule kResetRules[] = {
    ddk::MakeAcceptBindRule(bind_fuchsia::PROTOCOL, bind_fuchsia_gpio::BIND_PROTOCOL_DEVICE),
    ddk::MakeAcceptBindRule(bind_fuchsia::GPIO_PIN,
                            bind_fuchsia_amlogic_platform_t931::GPIOZ_PIN_ID_PIN_9),
};

const device_bind_prop_t kResetProperties[] = {
    ddk::MakeProperty(bind_fuchsia::PROTOCOL, bind_fuchsia_gpio::BIND_PROTOCOL_DEVICE),
    ddk::MakeProperty(bind_fuchsia_gpio::FUNCTION, bind_fuchsia_gpio::FUNCTION_TOUCH_RESET),
};

zx_status_t Sherlock::TouchInit() {
  static const FocaltechMetadata device_info = {
      .device_id = FOCALTECH_DEVICE_FT5726,
      .needs_firmware = true,
      .display_vendor = GetDisplayVendor(),
      .ddic_version = GetDdicVersion(),
  };
  static const device_metadata_t ft5726_touch_metadata[] = {
      {.type = DEVICE_METADATA_PRIVATE, .data = &device_info, .length = sizeof(device_info)},
  };

  auto status = DdkAddNodeGroup("ft5726_touch",
                                ddk::NodeGroupDesc(kI2cRules, kI2cProperties)
                                    .AddNodeRepresentation(kInterruptRules, kInterruptProperties)
                                    .AddNodeRepresentation(kResetRules, kResetProperties)
                                    .set_metadata(ft5726_touch_metadata)
                                    .set_spawn_colocated(false));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddNodeGroup failed: %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
