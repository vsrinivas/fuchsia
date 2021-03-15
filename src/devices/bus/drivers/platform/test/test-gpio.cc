// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpio.h>

#include "test.h"

namespace board_test {

namespace {

static const gpio_pin_t gpio_pins[] = {
    {1},
    {3},
    {5},
};

static const pbus_metadata_t gpio_metadata[] = {{
    .type = DEVICE_METADATA_GPIO_PINS,
    .data_buffer = reinterpret_cast<const uint8_t*>(&gpio_pins),
    .data_size = sizeof(gpio_pins),
}};

}  // namespace

zx_status_t TestBoard::GpioInit() {
  pbus_dev_t gpio_dev = {};
  gpio_dev.name = "gpio";
  gpio_dev.vid = PDEV_VID_TEST;
  gpio_dev.pid = PDEV_PID_PBUS_TEST;
  gpio_dev.did = PDEV_DID_TEST_GPIO;
  gpio_dev.metadata_list = gpio_metadata;
  gpio_dev.metadata_count = countof(gpio_metadata);

  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_test
