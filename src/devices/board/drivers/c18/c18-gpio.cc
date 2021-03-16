// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>

#include <lib/ddk/metadata.h>
#include <ddk/metadata/gpio.h>
#include <fbl/macros.h>
#include <soc/mt8183/mt8183-hw.h>

#include "c18.h"

namespace board_c18 {

zx_status_t C18::GpioInit() {
  const pbus_mmio_t gpio_mmios[] = {
      {
          .base = MT8183_GPIO_BASE,
          .length = MT8183_GPIO_SIZE,
      },
      {
          .base = MT8183_EINT_BASE,
          .length = MT8183_EINT_SIZE,
      },
  };

  const pbus_irq_t gpio_irqs[] = {
      {
          .irq = MT8183_IRQ_EINT,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
  };

  const gpio_pin_t c18_gpio_pins[] = {
      // For eMMC.
      {MT8183_GPIO_MSDC0_RST},
  };

  const pbus_metadata_t c18_gpio_metadata[] = {{
      .type = DEVICE_METADATA_GPIO_PINS,
      .data_buffer = &c18_gpio_pins,
      .data_size = sizeof(c18_gpio_pins),
  }};

  pbus_dev_t gpio_dev = {};
  gpio_dev.name = "gpio";
  gpio_dev.vid = PDEV_VID_MEDIATEK;
  gpio_dev.did = PDEV_DID_MEDIATEK_GPIO;
  gpio_dev.mmio_list = gpio_mmios;
  gpio_dev.mmio_count = countof(gpio_mmios);
  gpio_dev.irq_list = gpio_irqs;
  gpio_dev.irq_count = countof(gpio_irqs);
  gpio_dev.metadata_list = c18_gpio_metadata;
  gpio_dev.metadata_count = countof(c18_gpio_metadata);

  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  status = device_get_protocol(parent(), ZX_PROTOCOL_GPIO_IMPL, &gpio_impl_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_protocol failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_c18
