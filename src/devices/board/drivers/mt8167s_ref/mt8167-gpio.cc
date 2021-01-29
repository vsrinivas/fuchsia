// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpio.h>
#include <ddk/platform-defs.h>
#include <soc/mt8167/mt8167-gpio.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::GpioInit() {
  const pbus_mmio_t gpio_mmios[] = {
      {
          .base = MT8167_GPIO_BASE,
          .length = MT8167_GPIO_SIZE,
      },
      {
          .base = MT8167_IOCFG_BASE,
          .length = MT8167_IOCFG_SIZE,
      },
      {
          .base = MT8167_EINT_BASE,
          .length = MT8167_EINT_SIZE,
      },
  };

  const pbus_irq_t gpio_irqs[] = {
      {
          .irq = MT8167_IRQ_ARM_EINT,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
  };

  const gpio_pin_t cleo_gpio_pins[] = {
      // For backlight driver
      {MT8167_CLEO_GPIO_LCM_EN},
      // For display driver
      {MT8167_GPIO_LCD_RST},
      // For touch screen driver
      {MT8167_GPIO_TOUCH_INT},
      {MT8167_GPIO_TOUCH_RST},
      // For mt8167s audio out
      {MT8167_GPIO107_MSDC1_DAT1},
      {MT8167_GPIO108_MSDC1_DAT2},
      // For audio in
      {MT8167_GPIO24_EINT24},
      // For mt8167 buttons.
      {MT8167_GPIO_KP_ROW0},
      {MT8167_GPIO_KP_ROW1},
      {MT8167_GPIO_KP_COL0},
      {MT8167_GPIO_KP_COL1},
      // For cleo buttons.
      {MT8167_GPIO_VOLUME_UP},
      {MT8167_GPIO_MIC_PRIVACY},
      // For I2C.
      {MT8167_GPIO58_SDA0},
      {MT8167_GPIO59_SCL0},
      {MT8167_GPIO52_SDA1},
      {MT8167_GPIO53_SCL1},
      {MT8167_GPIO60_SDA2},
      {MT8167_GPIO61_SCL2},
      // For eMMC.
      {MT8167_GPIO_MSDC0_RST},
      // For WiFi/Bluetooth.
      {MT8167_GPIO_MT7668_PMU_EN},
      // For WiFi/Bluetooth on Cleo.
      {MT8167_CLEO_GPIO_HUB_PWR_EN},
  };

  const pbus_metadata_t cleo_gpio_metadata[] = {{
      .type = DEVICE_METADATA_GPIO_PINS,
      .data_buffer = reinterpret_cast<const uint8_t*>(&cleo_gpio_pins),
      .data_size = sizeof(cleo_gpio_pins),
  }};

  pbus_dev_t gpio_dev = {};
  gpio_dev.name = "gpio";
  gpio_dev.vid = PDEV_VID_MEDIATEK;
  gpio_dev.did = PDEV_DID_MEDIATEK_GPIO;
  gpio_dev.mmio_list = gpio_mmios;
  gpio_dev.mmio_count = countof(gpio_mmios);
  gpio_dev.irq_list = gpio_irqs;
  gpio_dev.irq_count = countof(gpio_irqs);
  gpio_dev.metadata_list = cleo_gpio_metadata;
  gpio_dev.metadata_count = countof(cleo_gpio_metadata);

  if ((board_info_.vid == PDEV_VID_MEDIATEK && board_info_.pid == PDEV_PID_MEDIATEK_8167S_REF) ||
      (board_info_.vid == PDEV_VID_GOOGLE && board_info_.pid == PDEV_PID_CLEO)) {
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
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

//#define GPIO_TEST
#ifdef GPIO_TEST
  const pbus_gpio_t gpio_test_gpios[] = {
      {
          .gpio = 60,  // SDA2, to test gpio_write()
      },
      {
          .gpio = 40,  // EINT KPROW0 (key matrix) to test gpio_get_interrupt()
      },
  };

  pbus_dev_t gpio_test_dev = {};
  gpio_test_dev.name = "imx8mevk-gpio-test";
  gpio_test_dev.vid = PDEV_VID_GENERIC;
  gpio_test_dev.pid = PDEV_PID_GENERIC;
  gpio_test_dev.did = PDEV_DID_GPIO_TEST;
  gpio_test_dev.gpio_list = gpio_test_gpios;
  gpio_test_dev.gpio_count = countof(gpio_test_gpios);
  if ((status = pbus_.DeviceAdd(&gpio_test_dev)) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not add gpio_test_dev %d", __FUNCTION__, status);
    return status;
  }
#endif

  return ZX_OK;
}

}  // namespace board_mt8167
