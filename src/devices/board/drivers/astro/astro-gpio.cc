// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <lib/ddk/metadata.h>
#include <ddk/metadata/gpio.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro-gpios.h"
#include "astro.h"

// uncomment to disable LED blinky test
// #define GPIO_TEST

namespace astro {

static const pbus_mmio_t gpio_mmios[] = {
    {
        .base = S905D2_GPIO_BASE,
        .length = S905D2_GPIO_LENGTH,
    },
    {
        .base = S905D2_GPIO_A0_BASE,
        .length = S905D2_GPIO_AO_LENGTH,
    },
    {
        .base = S905D2_GPIO_INTERRUPT_BASE,
        .length = S905D2_GPIO_INTERRUPT_LENGTH,
    },
};

static const pbus_irq_t gpio_irqs[] = {
    {
        .irq = S905D2_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D2_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D2_GPIO_IRQ_2,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D2_GPIO_IRQ_3,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D2_GPIO_IRQ_4,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D2_GPIO_IRQ_5,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D2_GPIO_IRQ_6,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D2_GPIO_IRQ_7,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
};

// GPIOs to expose from generic GPIO driver.
static const gpio_pin_t gpio_pins[] = {
    // For wifi.
    {S905D2_WIFI_SDIO_WAKE_HOST},
    // For display.
    {GPIO_PANEL_DETECT},
    {GPIO_LCD_RESET},
    // For touch screen.
    {GPIO_TOUCH_INTERRUPT},
    {GPIO_TOUCH_RESET},
    // For light sensor.
    {GPIO_LIGHT_INTERRUPT},
    // For audio.
    {GPIO_AUDIO_SOC_FAULT_L},
    {GPIO_SOC_AUDIO_EN},
    // For buttons.
    {GPIO_VOLUME_UP},
    {GPIO_VOLUME_DOWN},
    {GPIO_VOLUME_BOTH},
    {GPIO_MIC_PRIVACY},
    // For SDIO.
    {GPIO_SDIO_RESET},
    // For Bluetooth.
    {GPIO_SOC_WIFI_LPO_32k768},
    {GPIO_SOC_BT_REG_ON},
    // For lights.
    {GPIO_AMBER_LED},
};

static const pbus_metadata_t gpio_metadata[] = {
    {
        .type = DEVICE_METADATA_GPIO_PINS,
        .data_buffer = reinterpret_cast<const uint8_t*>(&gpio_pins),
        .data_size = sizeof(gpio_pins),
    },
};

static pbus_dev_t gpio_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "gpio";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D2;
  dev.did = PDEV_DID_AMLOGIC_GPIO;
  dev.mmio_list = gpio_mmios;
  dev.mmio_count = countof(gpio_mmios);
  dev.irq_list = gpio_irqs;
  dev.irq_count = countof(gpio_irqs);
  dev.metadata_list = gpio_metadata;
  dev.metadata_count = countof(gpio_metadata);
  return dev;
}();

zx_status_t Astro::GpioInit() {
  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed: %d", __func__, status);
    return status;
  }

  gpio_impl_ = ddk::GpioImplProtocolClient(parent());
  if (!gpio_impl_.is_valid()) {
    zxlogf(ERROR, "%s: GpioImplProtocolClient failed %d", __func__, status);
    return ZX_ERR_INTERNAL;
  }

#ifdef GPIO_TEST
  static const pbus_gpio_t gpio_test_gpios[] = {{
                                                    // SYS_LED
                                                    .gpio = S905D2_GPIOAO(11),
                                                },
                                                {
                                                    // JTAG Adapter Pin
                                                    .gpio = S905D2_GPIOAO(6),
                                                }};

  const pbus_dev_t gpio_test_dev = []() {
    pbus_dev_t dev = {};
    dev.name = "astro-gpio-test";
    dev.vid = PDEV_VID_GENERIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_GPIO_TEST;
    dev.gpio_list = gpio_test_gpios;
    dev.gpio_count = countof(gpio_test_gpios);
    return dev;
  }();

  if ((status = pbus_.DeviceAdd(&gpio_test_dev)) != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd gpio_test failed: %d", __func__, status);
    return status;
  }
#endif

  return ZX_OK;
}

}  // namespace astro
