// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gpioimpl/c/banjo.h>
#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/i2c.h>
#include <ddk/platform-defs.h>
#include <soc/as370/as370-i2c.h>

#include "as370.h"

namespace board_as370 {

zx_status_t As370::I2cInit() {
  zx_status_t status;

  constexpr uint32_t i2c_gpios[] = {
      as370::kI2c0Sda,
      as370::kI2c0Scl,
      as370::kI2c1Sda,
      as370::kI2c1Scl,
  };

  ddk::GpioImplProtocolClient gpio(parent());

  if (!gpio.is_valid()) {
    zxlogf(ERROR, "%s: Failed to create GPIO protocol client", __func__);
    return ZX_ERR_INTERNAL;
  }

  for (uint32_t i = 0; i < countof(i2c_gpios); i++) {
    status = gpio.SetAltFunction(i2c_gpios[i], 1);  // 1 == SDA/SCL pinmux setting.
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: GPIO SetAltFunction failed %d", __FUNCTION__, status);
      return status;
    }
  }

  constexpr pbus_mmio_t i2c_mmios[] = {
      {
          .base = as370::kI2c0Base,
          .length = as370::kI2c0Size,
      },
      {
          .base = as370::kI2c1Base,
          .length = as370::kI2c1Size,
      },
  };

  constexpr pbus_irq_t i2c_irqs[] = {
      {
          .irq = as370::kI2c0Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
      {
          .irq = as370::kI2c1Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
  };

  // I2C channels for as370 synaptics
  constexpr i2c_channel_t synaptics_i2c_channels[] = {
      // For audio out
      {
          .bus_id = 0,
          .address = 0x31,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
      // For power regulator
      {
          .bus_id = 0,
          .address = 0x66,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
  };

  // I2C channels for as370 visalia
  constexpr i2c_channel_t visalia_i2c_channels[] = {
      // For audio out
      {
          .bus_id = 0,
          .address = 0x31,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
      /* TI LP5018 LED driver */
      {
          .bus_id = 0,
          .address = 0x29,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
      // For power regulator
      {
          .bus_id = 0,
          .address = 0x66,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
      /* Cypress touch sensor */
      {
          .bus_id = 0,
          .address = 0x37,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
  };

  const pbus_metadata_t synaptics_i2c_metadata[] = {
      {
          .type = DEVICE_METADATA_I2C_CHANNELS,
          .data_buffer = &synaptics_i2c_channels,
          .data_size = sizeof(synaptics_i2c_channels),
      },
  };

  const pbus_metadata_t visalia_i2c_metadata[] = {
      {
          .type = DEVICE_METADATA_I2C_CHANNELS,
          .data_buffer = &visalia_i2c_channels,
          .data_size = sizeof(visalia_i2c_channels),
      },
  };

  pbus_dev_t i2c_dev = {};
  i2c_dev.name = "i2c";
  i2c_dev.vid = PDEV_VID_GENERIC;
  i2c_dev.pid = PDEV_PID_GENERIC;
  i2c_dev.did = PDEV_DID_DW_I2C;
  i2c_dev.mmio_list = i2c_mmios;
  i2c_dev.mmio_count = countof(i2c_mmios);
  i2c_dev.irq_list = i2c_irqs;
  i2c_dev.irq_count = countof(i2c_irqs);

  if (board_info_.vid == PDEV_VID_SYNAPTICS && board_info_.pid == PDEV_PID_SYNAPTICS_AS370) {
    i2c_dev.metadata_list = synaptics_i2c_metadata;
    i2c_dev.metadata_count = countof(synaptics_i2c_metadata);
  } else if (board_info_.vid == PDEV_VID_GOOGLE && board_info_.pid == PDEV_PID_VISALIA) {
    i2c_dev.metadata_list = visalia_i2c_metadata;
    i2c_dev.metadata_count = countof(visalia_i2c_metadata);
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = pbus_.DeviceAdd(&i2c_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_as370
