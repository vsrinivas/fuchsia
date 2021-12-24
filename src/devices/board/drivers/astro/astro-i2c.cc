// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro-gpios.h"
#include "astro.h"
#include "src/devices/lib/fidl-metadata/i2c.h"

namespace astro {
using i2c_channel_t = fidl_metadata::i2c::Channel;

static const pbus_mmio_t i2c_mmios[] = {
    {
        .base = S905D2_I2C_AO_0_BASE,
        .length = 0x20,
    },
    {
        .base = S905D2_I2C2_BASE,
        .length = 0x20,
    },
    {
        .base = S905D2_I2C3_BASE,
        .length = 0x20,
    },
};

static const pbus_irq_t i2c_irqs[] = {
    {
        .irq = S905D2_I2C_AO_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_I2C2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S905D2_I2C3_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const i2c_channel_t i2c_channels[] = {
    // Backlight I2C
    {
        .bus_id = ASTRO_I2C_3,
        .address = I2C_BACKLIGHT_ADDR,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Focaltech touch screen
    {
        .bus_id = ASTRO_I2C_2, .address = I2C_FOCALTECH_TOUCH_ADDR, .vid = 0, .pid = 0, .did = 0,
        // binds as composite device
    },
    // Goodix touch screen
    {
        .bus_id = ASTRO_I2C_2, .address = I2C_GOODIX_TOUCH_ADDR, .vid = 0, .pid = 0, .did = 0,
        // binds as composite device
    },
    // Light sensor
    {
        .bus_id = ASTRO_I2C_A0_0, .address = I2C_AMBIENTLIGHT_ADDR, .vid = 0, .pid = 0, .did = 0,
        // binds as composite device
    },
    // Audio output
    {
        .bus_id = ASTRO_I2C_3, .address = I2C_AUDIO_CODEC_ADDR, .vid = 0, .pid = 0, .did = 0,
        // binds as composite device
    },
};

static pbus_dev_t i2c_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "i2c";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_I2C;
  dev.mmio_list = i2c_mmios;
  dev.mmio_count = std::size(i2c_mmios);
  dev.irq_list = i2c_irqs;
  dev.irq_count = std::size(i2c_irqs);
  return dev;
}();

zx_status_t Astro::I2cInit() {
  // setup pinmux for our I2C busses

  // i2c_ao_0
  gpio_impl_.SetAltFunction(GPIO_SOC_SENSORS_I2C_SDA, 1);
  gpio_impl_.SetDriveStrength(GPIO_SOC_SENSORS_I2C_SDA, 4000, nullptr);

  gpio_impl_.SetAltFunction(GPIO_SOC_SENSORS_I2C_SCL, 1);
  gpio_impl_.SetDriveStrength(GPIO_SOC_SENSORS_I2C_SCL, 4000, nullptr);

  // i2c2
  gpio_impl_.SetAltFunction(GPIO_SOC_TOUCH_I2C_SDA, 3);
  gpio_impl_.SetDriveStrength(GPIO_SOC_TOUCH_I2C_SDA, 4000, nullptr);
  gpio_impl_.SetAltFunction(GPIO_SOC_TOUCH_I2C_SCL, 3);
  gpio_impl_.SetDriveStrength(GPIO_SOC_TOUCH_I2C_SCL, 4000, nullptr);

  // i2c3
  gpio_impl_.SetAltFunction(GPIO_SOC_AV_I2C_SDA, 2);
  gpio_impl_.SetDriveStrength(GPIO_SOC_AV_I2C_SDA, 3000, nullptr);

  gpio_impl_.SetAltFunction(GPIO_SOC_AV_I2C_SCL, 2);
  gpio_impl_.SetDriveStrength(GPIO_SOC_AV_I2C_SCL, 3000, nullptr);

  auto i2c_status = fidl_metadata::i2c::I2CChannelsToFidl(i2c_channels);
  if (i2c_status.is_error()) {
    zxlogf(ERROR, "%s: failed to fidl encode i2c channels: %d", __func__, i2c_status.error_value());
    return i2c_status.error_value();
  }

  auto& data = i2c_status.value();

  const pbus_metadata_t i2c_metadata = {
      .type = DEVICE_METADATA_I2C_CHANNELS,
      .data_buffer = data.data(),
      .data_size = data.size(),
  };

  i2c_dev.metadata_list = &i2c_metadata;
  i2c_dev.metadata_count = 1;

  zx_status_t status = pbus_.DeviceAdd(&i2c_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace astro
