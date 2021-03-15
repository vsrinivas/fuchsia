// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/platform-defs.h>
#include <limits.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/i2c.h>
#include <soc/aml-a311d/a311d-gpio.h>
#include <soc/aml-a311d/a311d-hw.h>

#include "vim3.h"

namespace vim3 {

// Only the AO and EE_M3 i2c busses are used on VIM3

static const pbus_mmio_t i2c_mmios[] = {
    {
        .base = A311D_I2C_AOBUS_BASE,
        .length = A311D_I2C_AOBUS_LENGTH,
    },
    {
        .base = A311D_EE_I2C_M3_BASE,
        .length = A311D_I2C_AOBUS_LENGTH,
    },
};

static const pbus_irq_t i2c_irqs[] = {
    {
        .irq = A311D_I2C_AO_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = A311D_I2C_M3_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const i2c_channel_t i2c_channels[] = {
    // RTC
    {
        .bus_id = 0,
        .address = 0x51,
        .vid = PDEV_VID_NXP,
        .pid = PDEV_PID_GENERIC,
        .did = PDEV_DID_PCF8563_RTC,
    },
    // STM8s microcontroller
    {
        .bus_id = 0,
        .address = 0x18,
        .vid = PDEV_VID_KHADAS,
        .pid = PDEV_PID_VIM3,
        .did = PDEV_DID_VIM3_MCU,
    },
#if 0  // placeholder until driver implemented and vid/pid/did assigned
       // bus_ids and addresses are correct
    // TCA6408 (U17) IO expander (used for various lcd/cam signals and LEDs)
    {
        .bus_id = 0,
        .address = 0x20,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // KXTJ3 (U18) 3 axis accelerometer
    {
        .bus_id = 0,
        .address = 0x0E,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
#endif
};

static const pbus_metadata_t i2c_metadata[] = {{
    .type = DEVICE_METADATA_I2C_CHANNELS,
    .data_buffer = reinterpret_cast<const uint8_t*>(&i2c_channels),
    .data_size = sizeof(i2c_channels),
}};
zx_status_t Vim3::I2cInit() {
  pbus_dev_t i2c_dev = {};
  i2c_dev.name = "i2c";
  i2c_dev.vid = PDEV_VID_AMLOGIC;
  i2c_dev.pid = PDEV_PID_GENERIC;
  i2c_dev.did = PDEV_DID_AMLOGIC_I2C;
  i2c_dev.mmio_list = i2c_mmios;
  i2c_dev.mmio_count = countof(i2c_mmios);
  i2c_dev.irq_list = i2c_irqs;
  i2c_dev.irq_count = countof(i2c_irqs);
  i2c_dev.metadata_list = i2c_metadata;
  i2c_dev.metadata_count = countof(i2c_metadata);

  // AO
  gpio_impl_.SetAltFunction(A311D_GPIOAO(2), A311D_GPIOAO_2_M0_SCL_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOAO(3), A311D_GPIOAO_3_M0_SDA_FN);

  // EE - M3
  // Used on J13(pins 3,4), M.2 socket(pins 40,42), and J4(pins 22,23)
  gpio_impl_.SetAltFunction(A311D_GPIOA(15), A311D_GPIOA_15_I2C_EE_M3_SCL_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOA(14), A311D_GPIOA_14_I2C_EE_M3_SDA_FN);

  zx_status_t status = pbus_.DeviceAdd(&i2c_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "I2cInit: DeviceAdd failed: %d", status);
    return status;
  }

  return ZX_OK;
}
}  // namespace vim3
