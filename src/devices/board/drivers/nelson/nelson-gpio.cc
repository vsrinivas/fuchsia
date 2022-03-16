// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/gpio.h>
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
        .base = S905D3_GPIO_AO_BASE,
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
    {GPIO_INRUSH_EN_SOC},
    {GPIO_SOC_I2S_SCLK},
    {GPIO_SOC_I2S_FS},
    {GPIO_SOC_I2S_DO0},
    {GPIO_SOC_I2S_DIN0},
    {GPIO_SOC_AUDIO_EN},
    {GPIO_SOC_MIC_DCLK},
    {GPIO_SOC_MICLR_DIN0},
    {GPIO_SOC_MICLR_DIN1},
    {GPIO_SOC_BKL_EN},
    {GPIO_AUDIO_SOC_FAULT_L},
    {GPIO_SOC_TH_RST_L},
    {GPIO_SOC_AV_I2C_SDA},
    {GPIO_SOC_AV_I2C_SCL},
    {GPIO_HW_ID_3},
    {GPIO_SOC_TH_BOOT_MODE_L},
    {GPIO_MUTE_SOC},
    {GPIO_HW_ID_2},
    {GPIO_TOUCH_SOC_INT_L},
    {GPIO_VOL_UP_L},
    {GPIO_VOL_DN_L},
    {GPIO_HW_ID_0},
    {GPIO_HW_ID_1},
    {GPIO_SOC_TOUCH_RST_L},
    {GPIO_ALERT_PWR_L},
    {GPIO_DISP_SOC_ID0},
    {GPIO_DISP_SOC_ID1},
    {GPIO_SOC_DISP_RST_L},
    {GPIO_SOC_TOUCH_I2C_SDA},
    {GPIO_SOC_TOUCH_I2C_SCL},
    {GPIO_SOC_SPI_A_MOSI},
    {GPIO_SOC_SPI_A_MISO},
    {GPIO_SOC_SPI_A_SS0},
    {GPIO_SOC_SPI_A_SCLK},
    {GPIO_TH_SOC_INT},
    {GPIO_SOC_TH_INT},
    {GPIO_SOC_WIFI_SDIO_D0},
    {GPIO_SOC_WIFI_SDIO_D1},
    {GPIO_SOC_WIFI_SDIO_D2},
    {GPIO_SOC_WIFI_SDIO_D3},
    {GPIO_SOC_WIFI_SDIO_CLK},
    {GPIO_SOC_WIFI_SDIO_CMD},
    {GPIO_SOC_WIFI_REG_ON},
    {GPIO_WIFI_SOC_WAKE},
    {GPIO_SOC_BT_PCM_IN},
    {GPIO_SOC_BT_PCM_OUT},
    {GPIO_SOC_BT_PCM_SYNC},
    {GPIO_SOC_BT_PCM_CLK},
    {GPIO_SOC_BT_UART_TX},
    {GPIO_SOC_BT_UART_RX},
    {GPIO_SOC_BT_UART_CTS},
    {GPIO_SOC_BT_UART_RTS},
    {GPIO_SOC_WIFI_LPO_32K768},
    {GPIO_SOC_BT_REG_ON},
    {GPIO_BT_SOC_WAKE},
    {GPIO_SOC_BT_WAKE},
    {GPIO_SOC_SELINA_RESET},
    {GPIO_SOC_SELINA_IRQ_OUT},
    {GPIO_SOC_SPI_B_MOSI},
    {GPIO_SOC_SPI_B_MISO},
    {GPIO_SOC_SPI_B_SS0},
    {GPIO_SOC_SPI_B_SCLK},
    {GPIO_SOC_DEBUG_UARTAO_TX},
    {GPIO_SOC_DEBUG_UARTAO_RX},
    {GPIO_SOC_SENSORS_I2C_SCL},
    {GPIO_SOC_SENSORS_I2C_SDA},
    {GPIO_HW_ID_4},
    {GPIO_RGB_SOC_INT_L},
    {GPIO_SOC_JTAG_TCK},
    {GPIO_SOC_JTAG_TMS},
    {GPIO_SOC_JTAG_TDI},
    {GPIO_SOC_JTAG_TDO},
    {GPIO_FDR_L},
    {GPIO_AMBER_LED_PWM},
    {GPIO_SOC_VDDEE_PWM},
    {GPIO_SOC_VDDCPU_PWM},

    {GPIO_EMMC_RESET},
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
  dev.mmio_count = std::size(gpio_mmios);
  dev.irq_list = gpio_irqs;
  dev.irq_count = std::size(gpio_irqs);
  dev.metadata_list = gpio_metadata;
  dev.metadata_count = std::size(gpio_metadata);
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
    dev.gpio_count = std::size(gpio_test_gpios);
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
