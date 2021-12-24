// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <limits.h>

#include <soc/aml-a311d/a311d-gpio.h>
#include <soc/aml-a311d/a311d-hw.h>

#include "src/devices/lib/fidl-metadata/i2c.h"
#include "vim3.h"

namespace vim3 {
using i2c_channel_t = fidl_metadata::i2c::Channel;

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
    // USB PD
    {
        .bus_id = 0,
        .address = 0x22,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // TCA6408 (U17) IO expander (used for various lcd/cam signals and LEDs)
    {
        .bus_id = 0,
        .address = 0x20,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // SDIO test rig on the J4 GPIO header. May or may not be connected.
    {
        .bus_id = 1,
        .address = 0x32,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
#if 0  // placeholder until driver implemented and vid/pid/did assigned
       // bus_ids and addresses are correct
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

zx_status_t Vim3::I2cInit() {
  pbus_dev_t i2c_dev = {};
  i2c_dev.name = "i2c";
  i2c_dev.vid = PDEV_VID_AMLOGIC;
  i2c_dev.pid = PDEV_PID_GENERIC;
  i2c_dev.did = PDEV_DID_AMLOGIC_I2C;
  i2c_dev.mmio_list = i2c_mmios;
  i2c_dev.mmio_count = std::size(i2c_mmios);
  i2c_dev.irq_list = i2c_irqs;
  i2c_dev.irq_count = std::size(i2c_irqs);

  auto i2c_status = fidl_metadata::i2c::I2CChannelsToFidl(i2c_channels);
  if (i2c_status.is_error()) {
    zxlogf(ERROR, "I2cInit: Failed to fidl encode i2c channels: %d", i2c_status.error_value());
    return i2c_status.error_value();
  }

  auto& data = i2c_status.value();

  pbus_metadata_t i2c_metadata = {
      .type = DEVICE_METADATA_I2C_CHANNELS,
      .data_buffer = data.data(),
      .data_size = data.size(),
  };
  i2c_dev.metadata_list = &i2c_metadata;
  i2c_dev.metadata_count = 1;

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
