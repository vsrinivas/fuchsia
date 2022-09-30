// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
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
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> gpio_mmios{
    {{
        .base = S905D3_GPIO_BASE,
        .length = S905D3_GPIO_LENGTH,
    }},
    {{
        .base = S905D3_GPIO_AO_BASE,
        .length = S905D3_GPIO_AO_LENGTH,
    }},
    {{
        .base = S905D3_GPIO_INTERRUPT_BASE,
        .length = S905D3_GPIO_INTERRUPT_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> gpio_irqs{
    {{
        .irq = S905D3_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D3_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D3_GPIO_IRQ_2,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D3_GPIO_IRQ_3,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D3_GPIO_IRQ_4,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D3_GPIO_IRQ_5,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D3_GPIO_IRQ_6,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D3_GPIO_IRQ_7,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
};

// GPIOs to expose from generic GPIO driver. Do not expose H bank GPIOs here, as they are managed by
// the GPIO H device below. The two GPIO devices are not capable of synchronizing accesses to the
// interrupt registers, so H bank GPIOs that are used for interrupts must be exposed by the main
// device (only GPIO_SOC_SELINA_IRQ_OUT). All pins can be used in calls from the board driver,
// regardless of bank.
static const gpio_pin_t gpio_pins[] = {
    DECL_GPIO_PIN(GPIO_INRUSH_EN_SOC),
    DECL_GPIO_PIN(GPIO_SOC_I2S_SCLK),
    DECL_GPIO_PIN(GPIO_SOC_I2S_FS),
    DECL_GPIO_PIN(GPIO_SOC_I2S_DO0),
    DECL_GPIO_PIN(GPIO_SOC_I2S_DIN0),
    DECL_GPIO_PIN(GPIO_SOC_AUDIO_EN),
    DECL_GPIO_PIN(GPIO_SOC_MIC_DCLK),
    DECL_GPIO_PIN(GPIO_SOC_MICLR_DIN0),
    DECL_GPIO_PIN(GPIO_SOC_MICLR_DIN1),
    DECL_GPIO_PIN(GPIO_SOC_BKL_EN),
    DECL_GPIO_PIN(GPIO_AUDIO_SOC_FAULT_L),
    DECL_GPIO_PIN(GPIO_SOC_TH_RST_L),
    DECL_GPIO_PIN(GPIO_SOC_AV_I2C_SDA),
    DECL_GPIO_PIN(GPIO_SOC_AV_I2C_SCL),
    DECL_GPIO_PIN(GPIO_HW_ID_3),
    DECL_GPIO_PIN(GPIO_SOC_TH_BOOT_MODE_L),
    DECL_GPIO_PIN(GPIO_MUTE_SOC),
    DECL_GPIO_PIN(GPIO_HW_ID_2),
    DECL_GPIO_PIN(GPIO_TOUCH_SOC_INT_L),
    DECL_GPIO_PIN(GPIO_VOL_UP_L),
    DECL_GPIO_PIN(GPIO_VOL_DN_L),
    DECL_GPIO_PIN(GPIO_HW_ID_0),
    DECL_GPIO_PIN(GPIO_HW_ID_1),
    DECL_GPIO_PIN(GPIO_SOC_TOUCH_RST_L),
    DECL_GPIO_PIN(GPIO_ALERT_PWR_L),
    DECL_GPIO_PIN(GPIO_DISP_SOC_ID0),
    DECL_GPIO_PIN(GPIO_DISP_SOC_ID1),
    DECL_GPIO_PIN(GPIO_SOC_DISP_RST_L),
    DECL_GPIO_PIN(GPIO_SOC_TOUCH_I2C_SDA),
    DECL_GPIO_PIN(GPIO_SOC_TOUCH_I2C_SCL),
    DECL_GPIO_PIN(GPIO_SOC_SPI_A_MOSI),
    DECL_GPIO_PIN(GPIO_SOC_SPI_A_MISO),
    DECL_GPIO_PIN(GPIO_SOC_SPI_A_SS0),
    DECL_GPIO_PIN(GPIO_SOC_SPI_A_SCLK),
    DECL_GPIO_PIN(GPIO_TH_SOC_INT),
    DECL_GPIO_PIN(GPIO_SOC_TH_INT),
    DECL_GPIO_PIN(GPIO_SOC_WIFI_SDIO_D0),
    DECL_GPIO_PIN(GPIO_SOC_WIFI_SDIO_D1),
    DECL_GPIO_PIN(GPIO_SOC_WIFI_SDIO_D2),
    DECL_GPIO_PIN(GPIO_SOC_WIFI_SDIO_D3),
    DECL_GPIO_PIN(GPIO_SOC_WIFI_SDIO_CLK),
    DECL_GPIO_PIN(GPIO_SOC_WIFI_SDIO_CMD),
    DECL_GPIO_PIN(GPIO_SOC_WIFI_REG_ON),
    DECL_GPIO_PIN(GPIO_WIFI_SOC_WAKE),
    DECL_GPIO_PIN(GPIO_SOC_BT_PCM_IN),
    DECL_GPIO_PIN(GPIO_SOC_BT_PCM_OUT),
    DECL_GPIO_PIN(GPIO_SOC_BT_PCM_SYNC),
    DECL_GPIO_PIN(GPIO_SOC_BT_PCM_CLK),
    DECL_GPIO_PIN(GPIO_SOC_BT_UART_TX),
    DECL_GPIO_PIN(GPIO_SOC_BT_UART_RX),
    DECL_GPIO_PIN(GPIO_SOC_BT_UART_CTS),
    DECL_GPIO_PIN(GPIO_SOC_BT_UART_RTS),
    DECL_GPIO_PIN(GPIO_SOC_WIFI_LPO_32K768),
    DECL_GPIO_PIN(GPIO_SOC_BT_REG_ON),
    DECL_GPIO_PIN(GPIO_BT_SOC_WAKE),
    DECL_GPIO_PIN(GPIO_SOC_BT_WAKE),
    // Selina is responsible for not making concurrent calls to this GPIO and the GPIO H device (or
    // other clients of that device, namely SPI1). Calls may be made on the interrupt object (and
    // interrupts may be received) at any time, as there is no GPIO driver involvement in that case.
    DECL_GPIO_PIN(GPIO_SOC_SELINA_IRQ_OUT),
    DECL_GPIO_PIN(GPIO_SOC_DEBUG_UARTAO_TX),
    DECL_GPIO_PIN(GPIO_SOC_DEBUG_UARTAO_RX),
    DECL_GPIO_PIN(GPIO_SOC_SENSORS_I2C_SCL),
    DECL_GPIO_PIN(GPIO_SOC_SENSORS_I2C_SDA),
    DECL_GPIO_PIN(GPIO_HW_ID_4),
    DECL_GPIO_PIN(GPIO_RGB_SOC_INT_L),
    DECL_GPIO_PIN(GPIO_SOC_JTAG_TCK),
    DECL_GPIO_PIN(GPIO_SOC_JTAG_TMS),
    DECL_GPIO_PIN(GPIO_SOC_JTAG_TDI),
    DECL_GPIO_PIN(GPIO_SOC_JTAG_TDO),
    DECL_GPIO_PIN(GPIO_FDR_L),
    DECL_GPIO_PIN(GPIO_AMBER_LED_PWM),
    DECL_GPIO_PIN(GPIO_SOC_VDDEE_PWM),
    DECL_GPIO_PIN(GPIO_SOC_VDDCPU_PWM),
    DECL_GPIO_PIN(SOC_EMMC_D0),
    DECL_GPIO_PIN(SOC_EMMC_D1),
    DECL_GPIO_PIN(SOC_EMMC_D2),
    DECL_GPIO_PIN(SOC_EMMC_D3),
    DECL_GPIO_PIN(SOC_EMMC_D4),
    DECL_GPIO_PIN(SOC_EMMC_D5),
    DECL_GPIO_PIN(SOC_EMMC_D6),
    DECL_GPIO_PIN(SOC_EMMC_D7),
    DECL_GPIO_PIN(SOC_EMMC_CLK),
    DECL_GPIO_PIN(SOC_EMMC_CMD),
    DECL_GPIO_PIN(SOC_EMMC_RST_L),
    DECL_GPIO_PIN(SOC_EMMC_DS),
};

static const std::vector<fpbus::Metadata> gpio_metadata{
    {{
        .type = DEVICE_METADATA_GPIO_PINS,
        .data =
            std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&gpio_pins),
                                 reinterpret_cast<const uint8_t*>(&gpio_pins) + sizeof(gpio_pins)),
    }},
};

static const fpbus::Node gpio_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "gpio";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_S905D3;
  dev.did() = PDEV_DID_AMLOGIC_GPIO;
  dev.mmio() = gpio_mmios;
  dev.irq() = gpio_irqs;
  dev.metadata() = gpio_metadata;
  return dev;
}();

// The GPIO H device won't be able to provide interrupts for the pins it exposes, so
// GPIO_SOC_SELINA_IRQ_OUT must be be exposed by the main GPIO device (see the list of pins above)
// instead of this one.
static const gpio_pin_t gpio_h_pins[] = {
    DECL_GPIO_PIN(GPIO_SOC_SELINA_RESET), DECL_GPIO_PIN(GPIO_SOC_SPI_B_MOSI),
    DECL_GPIO_PIN(GPIO_SOC_SPI_B_MISO), DECL_GPIO_PIN(GPIO_SOC_SPI_B_SS0),
    DECL_GPIO_PIN(GPIO_SOC_SPI_B_SCLK)};

static const std::vector<fpbus::Metadata> gpio_h_metadata{
    {{
        .type = DEVICE_METADATA_GPIO_PINS,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&gpio_h_pins),
            reinterpret_cast<const uint8_t*>(&gpio_h_pins) + sizeof(gpio_h_pins)),
    }},
};

static const fpbus::Node gpio_h_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "gpio-h";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_S905D3;
  dev.did() = PDEV_DID_AMLOGIC_GPIO;
  dev.instance_id() = 1;
  dev.mmio() = gpio_mmios;
  dev.metadata() = gpio_h_metadata;
  return dev;
}();

zx_status_t Nelson::GpioInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('GPIO');
  auto result = pbus_.buffer(arena)->ProtocolNodeAdd(ZX_PROTOCOL_GPIO_IMPL,
                                                     fidl::ToWire(fidl_arena, gpio_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: ProtocolNodeAdd Gpio(gpio_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: ProtocolNodeAdd Gpio(gpio_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  gpio_impl_ = ddk::GpioImplProtocolClient(parent());
  if (!gpio_impl_.is_valid()) {
    zxlogf(ERROR, "%s: GpioImplProtocolClient failed", __func__);
    return ZX_ERR_INTERNAL;
  }

  // Enable mute LED so it will be controlled by mute switch.
  zx_status_t status = gpio_impl_.ConfigOut(GPIO_AMBER_LED_PWM, 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ConfigOut failed: %d", __func__, status);
  }

  {
    auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, gpio_h_dev));
    if (!result.ok()) {
      zxlogf(ERROR, "%s: NodeAdd Gpio(gpio_h_dev) request failed: %s", __func__,
             result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: NodeAdd Gpio(gpio_h_dev) failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
  }

#ifdef GPIO_TEST
  static const std::vector<fpbus::Gpio> gpio_test_gpios{
      {{
          // SYS_LED
          .gpio = GPIO_AMBER_LED_PWM,
      }},
      {{
          // JTAG Adapter Pin
          .gpio = GPIO_SOC_JTAG_TCK,
      }},
  };

  fpbus::Node gpio_test_dev;
  gpio_test_dev.name() = "nelson-gpio-test";
  gpio_test_dev.vid() = PDEV_VID_GENERIC;
  gpio_test_dev.pid() = PDEV_PID_GENERIC;
  gpio_test_dev.did() = PDEV_DID_GPIO_TEST;
  gpio_test_dev.gpio() = gpio_test_gpios;

  result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, gpio_test_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Gpio(gpio_test_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Gpio(gpio_test_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
#endif

  return ZX_OK;
}

}  // namespace nelson
