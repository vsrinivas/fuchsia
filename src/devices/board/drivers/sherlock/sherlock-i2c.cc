// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/i2c.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddktl/protocol/gpioimpl.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

static const pbus_mmio_t i2c_mmios[] = {
    {
        .base = T931_I2C_AOBUS_BASE,
        .length = 0x20,
    },
    {
        .base = T931_I2C2_BASE,
        .length = 0x20,
    },
    {
        .base = T931_I2C3_BASE,
        .length = 0x20,
    },
};

static const pbus_irq_t i2c_irqs[] = {
    {
        .irq = T931_I2C_AO_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = T931_I2C2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = T931_I2C3_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static i2c_channel_t i2c_channels[] = {
    // Backlight I2C
    {
        .bus_id = SHERLOCK_I2C_3,
        .address = 0x2C,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Touch screen I2C
    {
        .bus_id = SHERLOCK_I2C_2,
        .address = 0x38,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Tweeter left
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x6c,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Tweeter right
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x6d,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Woofer
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x6f,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // IMX227 Camera Sensor
    {
        .bus_id = SHERLOCK_I2C_3,
        .address = 0x36,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Light Sensor
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x39,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
};

static const pbus_metadata_t i2c_metadata[] = {
    {
        .type = DEVICE_METADATA_I2C_CHANNELS,
        .data_buffer = &i2c_channels,
        .data_size = sizeof(i2c_channels),
    },
};

static pbus_dev_t i2c_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "gpio";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_I2C;
  dev.mmio_list = i2c_mmios;
  dev.mmio_count = countof(i2c_mmios);
  dev.irq_list = i2c_irqs;
  dev.irq_count = countof(i2c_irqs);
  dev.metadata_list = i2c_metadata;
  dev.metadata_count = countof(i2c_metadata);
  return dev;
}();

zx_status_t Sherlock::I2cInit() {
  pdev_board_info_t info;
  zx_status_t status = pbus_.GetBoardInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetBoardInfo failed %d", __FILE__, status);
    return status;
  }

  // setup pinmux for our I2C busses
  // i2c_ao_0
  gpio_impl_.SetAltFunction(T931_GPIOAO(2), 1);
  gpio_impl_.SetAltFunction(T931_GPIOAO(3), 1);
  if (info.pid == PDEV_PID_LUIS) {
    gpio_impl_.SetDriveStrength(T931_GPIOAO(2), 3000, nullptr);
    gpio_impl_.SetDriveStrength(T931_GPIOAO(3), 3000, nullptr);
  }
  // i2c2
  gpio_impl_.SetAltFunction(T931_GPIOZ(14), 3);
  gpio_impl_.SetAltFunction(T931_GPIOZ(15), 3);
  // i2c3
  gpio_impl_.SetAltFunction(T931_GPIOA(14), 2);
  gpio_impl_.SetAltFunction(T931_GPIOA(15), 2);

  // Camera sensor for Luis
  if (info.pid == PDEV_PID_LUIS) {
    i2c_channels[5].address = 0x1a;
  }

  status = pbus_.DeviceAdd(&i2c_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
