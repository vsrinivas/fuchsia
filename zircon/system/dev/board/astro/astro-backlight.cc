// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

namespace astro {

constexpr pbus_mmio_t backlight_mmios[] = {
    {
        .base = S905D2_GPIO_A0_BASE,
        .length = S905D2_GPIO_AO_LENGTH,
    },
};

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};

constexpr zx_bind_inst_t i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, ASTRO_I2C_3),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, I2C_BACKLIGHT_ADDR),
};

constexpr device_component_part_t i2c_component[] = {
    {countof(root_match), root_match},
    {countof(i2c_match), i2c_match},
};

constexpr device_component_t components[] = {
    {countof(i2c_component), i2c_component},
};

constexpr pbus_dev_t backlight_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "backlight";
  dev.vid = PDEV_VID_TI;
  dev.pid = PDEV_PID_TI_LP8556;
  dev.did = PDEV_DID_TI_BACKLIGHT;
  dev.mmio_list = backlight_mmios;
  dev.mmio_count = countof(backlight_mmios);
  return dev;
}();

zx_status_t Astro::BacklightInit() {
  auto status = pbus_.CompositeDeviceAdd(&backlight_dev, components, countof(components), 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s CompositeDeviceAdd failed %d\n", __FUNCTION__, status);
  }
  return status;
}

}  // namespace astro
