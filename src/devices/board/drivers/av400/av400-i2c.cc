// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <limits.h>

#include <soc/aml-a5/a5-gpio.h>
#include <soc/aml-a5/a5-hw.h>

#include "av400.h"
#include "src/devices/lib/fidl-metadata/i2c.h"

namespace av400 {
using i2c_channel_t = fidl_metadata::i2c::Channel;

// Only the I2C_C and I2C_D busses are used on AV400

static const pbus_mmio_t i2c_mmios[] = {
    {
        .base = A5_I2C_C_BASE,
        .length = A5_I2C_LENGTH,
    },
    {
        .base = A5_I2C_D_BASE,
        .length = A5_I2C_LENGTH,
    },
};

static const pbus_irq_t i2c_irqs[] = {
    {
        .irq = A5_I2C_C_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = A5_I2C_D_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const i2c_channel_t i2c_channels[] = {
    // ESMT audio amplifier
    {
        .bus_id = 0,
        .address = 0x30,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // ESMT audio amplifier
    {
        .bus_id = 0,
        .address = 0x31,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // ESMT audio amplifier
    {
        .bus_id = 0,
        .address = 0x34,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // ESMT audio amplifier
    {
        .bus_id = 0,
        .address = 0x35,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // ESMT audio amplifier
    {
        .bus_id = 1,
        .address = 0x30,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // ESMT audio amplifier
    {
        .bus_id = 1,
        .address = 0x31,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // ESMT audio amplifier
    {
        .bus_id = 1,
        .address = 0x34,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // ESMT audio amplifier
    {
        .bus_id = 1,
        .address = 0x35,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
};

zx_status_t Av400::I2cInit() {
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

  // I2C_C
  gpio_impl_.SetAltFunction(A5_GPIOD(15), A5_GPIOD_15_I2C2_SCL_FN);
  gpio_impl_.SetAltFunction(A5_GPIOD(14), A5_GPIOD_14_I2C2_SDA_FN);

  // I2C_D
  gpio_impl_.SetAltFunction(A5_GPIOD(13), A5_GPIOD_13_I2C3_SCL_FN);
  gpio_impl_.SetAltFunction(A5_GPIOD(12), A5_GPIOD_12_I2C3_SDA_FN);

  zx_status_t status = pbus_.DeviceAdd(&i2c_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "I2cInit: DeviceAdd failed: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}
}  // namespace av400
