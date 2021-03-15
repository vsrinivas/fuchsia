// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpio.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

namespace sherlock {

static const pbus_mmio_t gpio_mmios[] = {
    {
        .base = T931_GPIO_BASE,
        .length = T931_GPIO_LENGTH,
    },
    {
        .base = T931_GPIO_A0_BASE,
        .length = T931_GPIO_AO_LENGTH,
    },
    {
        .base = T931_GPIO_INTERRUPT_BASE,
        .length = T931_GPIO_INTERRUPT_LENGTH,
    },
};

static const pbus_irq_t gpio_irqs[] = {
    {
        .irq = T931_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_2,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_3,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_4,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_5,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_6,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_7,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
};

// GPIOs to expose from generic GPIO driver.
#ifdef FACTORY_BUILD
#define GPIO_PIN_COUNT 120
static const gpio_pin_t gpio_pins[] = {
    {T931_GPIOZ(0)},     {T931_GPIOZ(1)},     {T931_GPIOZ(2)},     {T931_GPIOZ(3)},
    {T931_GPIOZ(4)},     {T931_GPIOZ(5)},     {T931_GPIOZ(6)},     {T931_GPIOZ(7)},
    {T931_GPIOZ(8)},     {T931_GPIOZ(9)},     {T931_GPIOZ(10)},    {T931_GPIOZ(11)},
    {T931_GPIOZ(12)},    {T931_GPIOZ(13)},    {T931_GPIOZ(14)},    {T931_GPIOZ(15)},
    {T931_GPIOA(0)},     {T931_GPIOA(1)},     {T931_GPIOA(2)},     {T931_GPIOA(3)},
    {T931_GPIOA(4)},     {T931_GPIOA(5)},     {T931_GPIOA(6)},     {T931_GPIOA(7)},
    {T931_GPIOA(8)},     {T931_GPIOA(9)},     {T931_GPIOA(10)},    {T931_GPIOA(11)},
    {T931_GPIOA(12)},    {T931_GPIOA(13)},    {T931_GPIOA(14)},    {T931_GPIOA(15)},
    {T931_GPIOBOOT(0)},  {T931_GPIOBOOT(1)},  {T931_GPIOBOOT(2)},  {T931_GPIOBOOT(3)},
    {T931_GPIOBOOT(4)},  {T931_GPIOBOOT(5)},  {T931_GPIOBOOT(6)},  {T931_GPIOBOOT(7)},
    {T931_GPIOBOOT(8)},  {T931_GPIOBOOT(9)},  {T931_GPIOBOOT(10)}, {T931_GPIOBOOT(11)},
    {T931_GPIOBOOT(12)}, {T931_GPIOBOOT(13)}, {T931_GPIOBOOT(14)}, {T931_GPIOBOOT(15)},
    {T931_GPIOC(0)},     {T931_GPIOC(1)},     {T931_GPIOC(2)},     {T931_GPIOC(3)},
    {T931_GPIOC(4)},     {T931_GPIOC(5)},     {T931_GPIOC(6)},     {T931_GPIOC(7)},
    {T931_GPIOX(0)},     {T931_GPIOX(1)},     {T931_GPIOX(2)},     {T931_GPIOX(3)},
    {T931_GPIOX(4)},     {T931_GPIOX(5)},     {T931_GPIOX(6)},     {T931_GPIOX(7)},
    {T931_GPIOX(8)},     {T931_GPIOX(9)},     {T931_GPIOX(10)},    {T931_GPIOX(11)},
    {T931_GPIOX(12)},    {T931_GPIOX(13)},    {T931_GPIOX(14)},    {T931_GPIOX(15)},
    {T931_GPIOX(16)},    {T931_GPIOX(17)},    {T931_GPIOX(18)},    {T931_GPIOX(19)},
    {T931_GPIOX(20)},    {T931_GPIOX(21)},    {T931_GPIOX(22)},    {T931_GPIOX(23)},
    {T931_GPIOH(0)},     {T931_GPIOH(1)},     {T931_GPIOH(2)},     {T931_GPIOH(3)},
    {T931_GPIOH(4)},     {T931_GPIOH(5)},     {T931_GPIOH(6)},     {T931_GPIOH(7)},
    {T931_GPIOH(8)},     {T931_GPIOH(9)},     {T931_GPIOH(10)},    {T931_GPIOH(11)},
    {T931_GPIOH(12)},    {T931_GPIOH(13)},    {T931_GPIOH(14)},    {T931_GPIOH(15)},
    {T931_GPIOAO(0)},    {T931_GPIOAO(1)},    {T931_GPIOAO(2)},    {T931_GPIOAO(3)},
    {T931_GPIOAO(4)},    {T931_GPIOAO(5)},    {T931_GPIOAO(6)},    {T931_GPIOAO(7)},
    {T931_GPIOAO(8)},    {T931_GPIOAO(9)},    {T931_GPIOAO(10)},   {T931_GPIOAO(11)},
    {T931_GPIOAO(12)},   {T931_GPIOAO(13)},   {T931_GPIOAO(14)},   {T931_GPIOAO(15)},
    {T931_GPIOE(0)},     {T931_GPIOE(1)},     {T931_GPIOE(2)},     {T931_GPIOE(3)},
    {T931_GPIOE(4)},     {T931_GPIOE(5)},     {T931_GPIOE(6)},     {T931_GPIOE(7)},
};
#else
#define GPIO_PIN_COUNT 30
static const gpio_pin_t gpio_pins[] = {
    // For wifi.
    {T931_WIFI_HOST_WAKE},
    // For display.
    {GPIO_PANEL_DETECT},
    {GPIO_DDIC_DETECT},
    {GPIO_LCD_RESET},
    // For touch screen.
    {GPIO_TOUCH_INTERRUPT},
    {GPIO_TOUCH_RESET},
    // For audio out.
    {GPIO_AUDIO_SOC_FAULT_L},
    {GPIO_SOC_AUDIO_EN},
    // For Camera.
    {GPIO_VANA_ENABLE},
    {GPIO_VDIG_ENABLE},
    {GPIO_CAM_RESET},
    {GPIO_LIGHT_INTERRUPT},
    // For SPI interface.
    {GPIO_SPICC0_SS0},
    // For buttons.
    {GPIO_VOLUME_UP},
    {GPIO_VOLUME_DOWN},
    {GPIO_VOLUME_BOTH},
    {GPIO_MIC_PRIVACY},
    // For eMMC.
    {T931_EMMC_RST},
    // For SDIO.
    {T931_WIFI_REG_ON},
    // For OpenThread radio
    {GPIO_OT_RADIO_RESET},
    {GPIO_OT_RADIO_INTERRUPT},
    {GPIO_OT_RADIO_BOOTLOADER},
    // LED
    {GPIO_AMBER_LED},
    {GPIO_GREEN_LED},
    // For Bluetooth.
    {GPIO_SOC_WIFI_LPO_32k768},
    {GPIO_SOC_BT_REG_ON},

    // Luis Audio
    {GPIO_AMP_24V_EN},

    // Luis camera switch, unused on Sherlock
    {GPIO_CAM_MUTE},

    // Luis camera supplies, unused on Sherlock
    {GPIO_CAM_VIF_ENABLE},
    {GPIO_CAM_VANA_ENABLE},
};
#endif  // FACTORY_BUILD

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
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_GPIO;
  dev.mmio_list = gpio_mmios;
  dev.mmio_count = countof(gpio_mmios);
  dev.irq_list = gpio_irqs;
  dev.irq_count = countof(gpio_irqs);
  dev.metadata_list = gpio_metadata;
  dev.metadata_count = countof(gpio_metadata);
  return dev;
}();

zx_status_t Sherlock::GpioInit() {
  static_assert(countof(gpio_pins) == GPIO_PIN_COUNT, "Incorrect pin count.");

  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d", __func__, status);
    return status;
  }
// This test binds to system/dev/gpio/gpio-test to check that GPIOs work at all.
// gpio-test enables interrupts and write/read on the test GPIOs configured below.
//#define GPIO_TEST
#ifdef GPIO_TEST
  const pbus_gpio_t gpio_test_gpios[] = {
      {
          .gpio = T931_GPIOZ(5),  // Volume down, not used in this test.
      },
      {
          .gpio = T931_GPIOZ(4),  // Volume up, to test gpio_get_interrupt().
      },
  };

  pbus_dev_t gpio_test_dev = {};
  gpio_test_dev.name = "sherlock-gpio-test";
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

  gpio_impl_ = ddk::GpioImplProtocolClient(parent());
  if (!gpio_impl_.is_valid()) {
    zxlogf(ERROR, "%s: device_get_protocol failed %d", __func__, status);
    return ZX_ERR_INTERNAL;
  }

  // Luis audio
  if (pid_ == PDEV_PID_LUIS) {
    gpio_impl_.ConfigOut(GPIO_AMP_24V_EN, 1);
  }

  return ZX_OK;
}

}  // namespace sherlock
