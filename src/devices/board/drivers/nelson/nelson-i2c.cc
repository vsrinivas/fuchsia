// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "nelson.h"
#include "src/devices/lib/fidl-metadata/i2c.h"

namespace nelson {
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

static const uint32_t i2c_clock_delays[] = {
    0,    // Ignore I2C AO
    131,  // Set I2C 2 (touch) to 400 kHz
    0,    // Ignore I2C 3
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
        .bus_id = NELSON_I2C_3,
        .address = I2C_BACKLIGHT_ADDR,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Focaltech touch screen
    {
        .bus_id = NELSON_I2C_2, .address = I2C_FOCALTECH_TOUCH_ADDR, .vid = 0, .pid = 0, .did = 0,
        // binds as composite device
    },
    // Goodix touch screen
    {
        .bus_id = NELSON_I2C_2, .address = I2C_GOODIX_TOUCH_ADDR, .vid = 0, .pid = 0, .did = 0,
        // binds as composite device
    },
    // Light sensor
    {
        .bus_id = NELSON_I2C_A0_0, .address = I2C_AMBIENTLIGHT_ADDR, .vid = 0, .pid = 0, .did = 0,
        // binds as composite device
    },
    // Audio output
    {
        .bus_id = NELSON_I2C_3, .address = I2C_AUDIO_CODEC_ADDR, .vid = 0, .pid = 0, .did = 0,
        // binds as composite device
    },
    // Audio output
    {
        .bus_id = NELSON_I2C_3, .address = I2C_AUDIO_CODEC_ADDR_P2, .vid = 0, .pid = 0, .did = 0,
        // binds as composite device
    },
    // Power sensors
    {
        .bus_id = NELSON_I2C_3,
        .address = I2C_TI_INA231_MLB_ADDR,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    {
        .bus_id = NELSON_I2C_3,
        .address = I2C_TI_INA231_SPEAKERS_ADDR,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    {
        .bus_id = NELSON_I2C_A0_0,
        .address = I2C_SHTV3_ADDR,
        .vid = PDEV_VID_SENSIRION,
        .pid = 0,
        .did = PDEV_DID_SENSIRION_SHTV3,
    },
};

static pbus_dev_t i2c_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "i2c";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_I2C;
  dev.mmio_list = i2c_mmios;
  dev.mmio_count = countof(i2c_mmios);
  dev.irq_list = i2c_irqs;
  dev.irq_count = countof(i2c_irqs);
  return dev;
}();

zx_status_t Nelson::I2cInit() {
  // setup pinmux for our I2C busses

  // i2c_ao_0
  gpio_impl_.SetAltFunction(S905D2_GPIOAO(2), 1);
  gpio_impl_.SetDriveStrength(S905D2_GPIOAO(2), 2500, nullptr);
  gpio_impl_.SetAltFunction(S905D2_GPIOAO(3), 1);
  gpio_impl_.SetDriveStrength(S905D2_GPIOAO(3), 2500, nullptr);
  // i2c2
  gpio_impl_.SetAltFunction(S905D2_GPIOZ(14), 3);
  gpio_impl_.SetDriveStrength(S905D2_GPIOZ(14), 3000, nullptr);
  gpio_impl_.SetAltFunction(S905D2_GPIOZ(15), 3);
  gpio_impl_.SetDriveStrength(S905D2_GPIOZ(15), 3000, nullptr);
  // i2c3
  gpio_impl_.SetAltFunction(S905D2_GPIOA(14), 2);
  gpio_impl_.SetDriveStrength(S905D2_GPIOA(14), 3000, nullptr);
  gpio_impl_.SetAltFunction(S905D2_GPIOA(15), 2);
  gpio_impl_.SetDriveStrength(S905D2_GPIOA(15), 3000, nullptr);

  auto i2c_status = fidl_metadata::i2c::I2CChannelsToFidl(i2c_channels);
  if (i2c_status.is_error()) {
    zxlogf(ERROR, "%s: failed to fidl encode i2c channels: %d", __func__, i2c_status.error_value());
    return i2c_status.error_value();
  }

  auto& data = i2c_status.value();
  pbus_metadata_t i2c_metadata[] = {
      {.type = DEVICE_METADATA_I2C_CHANNELS, .data_buffer = data.data(), .data_size = data.size()},
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data_buffer = reinterpret_cast<const uint8_t*>(&i2c_clock_delays),
          .data_size = sizeof(i2c_clock_delays),
      },
  };
  i2c_dev.metadata_list = i2c_metadata;
  i2c_dev.metadata_count = countof(i2c_metadata);

  zx_status_t status = pbus_.DeviceAdd(&i2c_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace nelson
