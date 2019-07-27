// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include "as370.h"

namespace board_as370 {

zx_status_t As370::LightInit() {
  // setup LED reset pin
  auto status = gpio_impl_.SetAltFunction(4, 0);  // 0 - GPIO mode
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GPIO SetAltFunction failed: %d\n", __func__, status);
    return status;
  }

  // Initialize LED device
  status = gpio_impl_.Write(4, 1);  // Initialize device
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GPIO Write failed: %d\n", __func__, status);
    return status;
  }

  // Composite binding rules for TI LED driver.
  static const zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };

  static const zx_bind_inst_t i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0x0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x29),
  };

  static const device_component_part_t i2c_component[] = {
      {countof(root_match), root_match},
      {countof(i2c_match), i2c_match},
  };

  static const device_component_t components[] = {
      {countof(i2c_component), i2c_component},
  };

  pbus_dev_t light_dev = {};
  light_dev.name = "lp5018-light";
  light_dev.vid = PDEV_VID_TI;
  light_dev.pid = PDEV_PID_TI_LP5018;
  light_dev.did = PDEV_DID_TI_LED;

  status = pbus_.CompositeDeviceAdd(&light_dev, components, countof(components), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d\n", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_as370
