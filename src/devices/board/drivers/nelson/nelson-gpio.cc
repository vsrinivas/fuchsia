// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpio.h>
#include <ddk/platform-defs.h>
#include <soc/aml-s905d3/s905d3-gpio.h>
#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson-gpios.h"
#include "nelson.h"

// uncomment to disable LED blinky test
// #define GPIO_TEST

namespace nelson {

static const pbus_mmio_t gpio_mmios[] = {
    {
        .base = S905D3_GPIO_BASE,
        .length = S905D3_GPIO_LENGTH,
    },
    {
        .base = S905D3_GPIO_A0_BASE,
        .length = S905D3_GPIO_AO_LENGTH,
    },
    {
        .base = S905D3_GPIO_INTERRUPT_BASE,
        .length = S905D3_GPIO_INTERRUPT_LENGTH,
    },
};

static const pbus_irq_t gpio_irqs[] = {
    {
        .irq = S905D3_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D3_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D3_GPIO_IRQ_2,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D3_GPIO_IRQ_3,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D3_GPIO_IRQ_4,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D3_GPIO_IRQ_5,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D3_GPIO_IRQ_6,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = S905D3_GPIO_IRQ_7,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
};

// GPIOs to expose from generic GPIO driver.
static const gpio_pin_t gpio_pins[] = {
    // For wifi.
    {S905D3_WIFI_SDIO_WAKE_HOST},
    // For display.
    {GPIO_DISPLAY_ID0},
    {GPIO_DISPLAY_ID1},
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
    // For LED.
    {GPIO_AMBER_LED},
    // For SDIO.
    {GPIO_WIFI_REG_ON},
    // For SPI.
    {GPIO_SPICC1_SS0},
    // For eMMC.
    {GPIO_EMMC_RESET},
    // For Bluetooth.
    {GPIO_SOC_WIFI_LPO_32k768},
    {GPIO_SOC_BT_REG_ON},
    // For radar sensor.
    {GPIO_SELINA_RESET},
    {GPIO_SELINA_IRQ},
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

zx_status_t Nelson::GpioInit() {
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

  // Enable mute LED so it will be controlled by mute switch.
  status = gpio_impl_.ConfigOut(S905D3_GPIOAO(11), 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ConfigOut failed: %d", __func__, status);
  }

#ifdef GPIO_TEST
  static const pbus_gpio_t gpio_test_gpios[] = {{
                                                    // SYS_LED
                                                    .gpio = S905D3_GPIOAO(11),
                                                },
                                                {
                                                    // JTAG Adapter Pin
                                                    .gpio = S905D3_GPIOAO(6),
                                                }};

  const pbus_dev_t gpio_test_dev = []() {
    pbus_dev_t dev = {};
    dev.name = "nelson-gpio-test";
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

}  // namespace nelson
