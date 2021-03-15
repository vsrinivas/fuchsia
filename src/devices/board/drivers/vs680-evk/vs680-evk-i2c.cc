// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/i2c.h>
#include <fbl/algorithm.h>
#include <soc/vs680/vs680-i2c.h>

#include "vs680-evk.h"

namespace board_vs680_evk {

zx_status_t Vs680Evk::I2cInit() {
  ddk::GpioImplProtocolClient gpio(parent());
  if (!gpio.is_valid()) {
    zxlogf(ERROR, "%s: Failed to create GPIO protocol client", __func__);
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status;
  if ((status = gpio.SetAltFunction(vs680::kI2c0Sda, vs680::kI2c0AltFunction)) != ZX_OK ||
      (status = gpio.SetAltFunction(vs680::kI2c0Scl, vs680::kI2c0AltFunction)) != ZX_OK ||
      (status = gpio.SetAltFunction(vs680::kI2c1Sda, vs680::kI2c1AltFunction)) != ZX_OK ||
      (status = gpio.SetAltFunction(vs680::kI2c1Scl, vs680::kI2c1AltFunction)) != ZX_OK) {
    zxlogf(ERROR, "%s: GPIO SetAltFunction failed %d", __func__, status);
    return status;
  }

  constexpr pbus_mmio_t i2c_mmios[] = {
      {
          .base = vs680::kI2c0Base,
          .length = vs680::kI2cSize,
      },
      {
          .base = vs680::kI2c1Base,
          .length = vs680::kI2cSize,
      },
  };

  constexpr pbus_irq_t i2c_irqs[] = {
      {
          .irq = vs680::kI2c0Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
      {
          .irq = vs680::kI2c1Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
  };

  constexpr i2c_channel_t i2c_channels[] = {
      {
          // GPIO expander 2
          .bus_id = 0,
          .address = 0x43,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
      {
          // GPIO expander 3
          .bus_id = 0,
          .address = 0x44,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
      {
          // VCPU PMIC
          .bus_id = 1,
          .address = 0x62,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
  };

  const pbus_metadata_t i2c_metadata[] = {
      {
          .type = DEVICE_METADATA_I2C_CHANNELS,
          .data_buffer = &i2c_channels,
          .data_size = sizeof(i2c_channels),
      },
  };

  pbus_dev_t i2c_dev = {};
  i2c_dev.name = "i2c";
  i2c_dev.vid = PDEV_VID_GENERIC;
  i2c_dev.pid = PDEV_PID_GENERIC;
  i2c_dev.did = PDEV_DID_DW_I2C;
  i2c_dev.mmio_list = i2c_mmios;
  i2c_dev.mmio_count = std::size(i2c_mmios);
  i2c_dev.irq_list = i2c_irqs;
  i2c_dev.irq_count = std::size(i2c_irqs);
  i2c_dev.metadata_list = i2c_metadata;
  i2c_dev.metadata_count = std::size(i2c_metadata);

  if ((status = pbus_.DeviceAdd(&i2c_dev)) != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_vs680_evk
