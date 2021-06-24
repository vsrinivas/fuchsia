// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <limits.h>

#include <ddk/metadata/i2c.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>

#include "src/devices/lib/fidl-metadata/i2c.h"
#include "vim.h"

namespace vim {
using i2c_channel_t = fidl_metadata::i2c::Channel;

static const pbus_mmio_t i2c_mmios[] = {
    {
        .base = S912_I2C_A_BASE,
        .length = S912_I2C_A_LENGTH,
    },
    {
        .base = S912_I2C_B_BASE,
        .length = S912_I2C_B_LENGTH,
    },
    {
        .base = S912_I2C_C_BASE,
        .length = S912_I2C_C_LENGTH,
    },
    /*
    {
        .base = S912_I2C_D_BASE,
        .length = S912_I2C_D_LENGTH,
    },
*/
};

static const pbus_irq_t i2c_irqs[] = {
    {
        .irq = S912_M_I2C_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S912_M_I2C_1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    {
        .irq = S912_M_I2C_2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
    /*
    {
        .irq = S912_M_I2C_3_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
*/
};

static const i2c_channel_t i2c_channels[] = {
    // RTC
    {
        .bus_id = 1,
        .address = 0x51,
        .vid = PDEV_VID_NXP,
        .pid = PDEV_PID_GENERIC,
        .did = PDEV_DID_PCF8563_RTC,
    },
    // Ethernet
    {
        .bus_id = 1,
        .address = 0x18,
        // Binds to a composite device.
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
};

zx_status_t Vim::I2cInit() {
  pbus_dev_t i2c_dev = {};
  i2c_dev.name = "i2c";
  i2c_dev.vid = PDEV_VID_AMLOGIC;
  i2c_dev.pid = PDEV_PID_GENERIC;
  i2c_dev.did = PDEV_DID_AMLOGIC_I2C;
  i2c_dev.mmio_list = i2c_mmios;
  i2c_dev.mmio_count = countof(i2c_mmios);
  i2c_dev.irq_list = i2c_irqs;
  i2c_dev.irq_count = countof(i2c_irqs);

  auto i2c_status = fidl_metadata::i2c::I2CChannelsToFidl(i2c_channels);
  if (i2c_status.is_error()) {
    zxlogf(ERROR, "%s: failed to fidl encode i2c channels: %d", __func__, i2c_status.error_value());
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

  // setup pinmux for our I2C busses
  // I2C_A and I2C_B are exposed on the 40 pin header and I2C_C on the FPC connector
  gpio_impl_.SetAltFunction(S912_I2C_SDA_A, S912_I2C_SDA_A_FN);
  gpio_impl_.SetAltFunction(S912_I2C_SCK_A, S912_I2C_SCK_A_FN);
  gpio_impl_.SetAltFunction(S912_I2C_SDA_B, S912_I2C_SDA_B_FN);
  gpio_impl_.SetAltFunction(S912_I2C_SCK_B, S912_I2C_SCK_B_FN);
  gpio_impl_.SetAltFunction(S912_I2C_SDA_C, S912_I2C_SDA_C_FN);
  gpio_impl_.SetAltFunction(S912_I2C_SCK_C, S912_I2C_SCK_C_FN);

  zx_status_t status = pbus_.DeviceAdd(&i2c_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "I2cInit: DeviceAdd failed: %d", status);
    return status;
  }

  return ZX_OK;
}
}  // namespace vim
