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
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

namespace sherlock {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> gpio_mmios{
    {{
        .base = T931_GPIO_BASE,
        .length = T931_GPIO_LENGTH,
    }},
    {{
        .base = T931_GPIO_AO_BASE,
        .length = T931_GPIO_AO_LENGTH,
    }},
    {{
        .base = T931_GPIO_INTERRUPT_BASE,
        .length = T931_GPIO_INTERRUPT_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> gpio_irqs{
    {{
        .irq = T931_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = T931_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = T931_GPIO_IRQ_2,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = T931_GPIO_IRQ_3,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = T931_GPIO_IRQ_4,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = T931_GPIO_IRQ_5,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = T931_GPIO_IRQ_6,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = T931_GPIO_IRQ_7,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
};

// GPIOs to expose from generic GPIO driver.
#ifdef FACTORY_BUILD
#define GPIO_PIN_COUNT 120
static const gpio_pin_t gpio_pins[] = {
    DECL_GPIO_PIN(T931_GPIOZ(0)),     DECL_GPIO_PIN(T931_GPIOZ(1)),
    DECL_GPIO_PIN(T931_GPIOZ(2)),     DECL_GPIO_PIN(T931_GPIOZ(3)),
    DECL_GPIO_PIN(T931_GPIOZ(4)),     DECL_GPIO_PIN(T931_GPIOZ(5)),
    DECL_GPIO_PIN(T931_GPIOZ(6)),     DECL_GPIO_PIN(T931_GPIOZ(7)),
    DECL_GPIO_PIN(T931_GPIOZ(8)),     DECL_GPIO_PIN(T931_GPIOZ(9)),
    DECL_GPIO_PIN(T931_GPIOZ(10)),    DECL_GPIO_PIN(T931_GPIOZ(11)),
    DECL_GPIO_PIN(T931_GPIOZ(12)),    DECL_GPIO_PIN(T931_GPIOZ(13)),
    DECL_GPIO_PIN(T931_GPIOZ(14)),    DECL_GPIO_PIN(T931_GPIOZ(15)),
    DECL_GPIO_PIN(T931_GPIOA(0)),     DECL_GPIO_PIN(T931_GPIOA(1)),
    DECL_GPIO_PIN(T931_GPIOA(2)),     DECL_GPIO_PIN(T931_GPIOA(3)),
    DECL_GPIO_PIN(T931_GPIOA(4)),     DECL_GPIO_PIN(T931_GPIOA(5)),
    DECL_GPIO_PIN(T931_GPIOA(6)),     DECL_GPIO_PIN(T931_GPIOA(7)),
    DECL_GPIO_PIN(T931_GPIOA(8)),     DECL_GPIO_PIN(T931_GPIOA(9)),
    DECL_GPIO_PIN(T931_GPIOA(10)),    DECL_GPIO_PIN(T931_GPIOA(11)),
    DECL_GPIO_PIN(T931_GPIOA(12)),    DECL_GPIO_PIN(T931_GPIOA(13)),
    DECL_GPIO_PIN(T931_GPIOA(14)),    DECL_GPIO_PIN(T931_GPIOA(15)),
    DECL_GPIO_PIN(T931_GPIOBOOT(0)),  DECL_GPIO_PIN(T931_GPIOBOOT(1)),
    DECL_GPIO_PIN(T931_GPIOBOOT(2)),  DECL_GPIO_PIN(T931_GPIOBOOT(3)),
    DECL_GPIO_PIN(T931_GPIOBOOT(4)),  DECL_GPIO_PIN(T931_GPIOBOOT(5)),
    DECL_GPIO_PIN(T931_GPIOBOOT(6)),  DECL_GPIO_PIN(T931_GPIOBOOT(7)),
    DECL_GPIO_PIN(T931_GPIOBOOT(8)),  DECL_GPIO_PIN(T931_GPIOBOOT(9)),
    DECL_GPIO_PIN(T931_GPIOBOOT(10)), DECL_GPIO_PIN(T931_GPIOBOOT(11)),
    DECL_GPIO_PIN(T931_GPIOBOOT(12)), DECL_GPIO_PIN(T931_GPIOBOOT(13)),
    DECL_GPIO_PIN(T931_GPIOBOOT(14)), DECL_GPIO_PIN(T931_GPIOBOOT(15)),
    DECL_GPIO_PIN(T931_GPIOC(0)),     DECL_GPIO_PIN(T931_GPIOC(1)),
    DECL_GPIO_PIN(T931_GPIOC(2)),     DECL_GPIO_PIN(T931_GPIOC(3)),
    DECL_GPIO_PIN(T931_GPIOC(4)),     DECL_GPIO_PIN(T931_GPIOC(5)),
    DECL_GPIO_PIN(T931_GPIOC(6)),     DECL_GPIO_PIN(T931_GPIOC(7)),
    DECL_GPIO_PIN(T931_GPIOX(0)),     DECL_GPIO_PIN(T931_GPIOX(1)),
    DECL_GPIO_PIN(T931_GPIOX(2)),     DECL_GPIO_PIN(T931_GPIOX(3)),
    DECL_GPIO_PIN(T931_GPIOX(4)),     DECL_GPIO_PIN(T931_GPIOX(5)),
    DECL_GPIO_PIN(T931_GPIOX(6)),     DECL_GPIO_PIN(T931_GPIOX(7)),
    DECL_GPIO_PIN(T931_GPIOX(8)),     DECL_GPIO_PIN(T931_GPIOX(9)),
    DECL_GPIO_PIN(T931_GPIOX(10)),    DECL_GPIO_PIN(T931_GPIOX(11)),
    DECL_GPIO_PIN(T931_GPIOX(12)),    DECL_GPIO_PIN(T931_GPIOX(13)),
    DECL_GPIO_PIN(T931_GPIOX(14)),    DECL_GPIO_PIN(T931_GPIOX(15)),
    DECL_GPIO_PIN(T931_GPIOX(16)),    DECL_GPIO_PIN(T931_GPIOX(17)),
    DECL_GPIO_PIN(T931_GPIOX(18)),    DECL_GPIO_PIN(T931_GPIOX(19)),
    DECL_GPIO_PIN(T931_GPIOX(20)),    DECL_GPIO_PIN(T931_GPIOX(21)),
    DECL_GPIO_PIN(T931_GPIOX(22)),    DECL_GPIO_PIN(T931_GPIOX(23)),
    DECL_GPIO_PIN(T931_GPIOH(0)),     DECL_GPIO_PIN(T931_GPIOH(1)),
    DECL_GPIO_PIN(T931_GPIOH(2)),     DECL_GPIO_PIN(T931_GPIOH(3)),
    DECL_GPIO_PIN(T931_GPIOH(4)),     DECL_GPIO_PIN(T931_GPIOH(5)),
    DECL_GPIO_PIN(T931_GPIOH(6)),     DECL_GPIO_PIN(T931_GPIOH(7)),
    DECL_GPIO_PIN(T931_GPIOH(8)),     DECL_GPIO_PIN(T931_GPIOH(9)),
    DECL_GPIO_PIN(T931_GPIOH(10)),    DECL_GPIO_PIN(T931_GPIOH(11)),
    DECL_GPIO_PIN(T931_GPIOH(12)),    DECL_GPIO_PIN(T931_GPIOH(13)),
    DECL_GPIO_PIN(T931_GPIOH(14)),    DECL_GPIO_PIN(T931_GPIOH(15)),
    DECL_GPIO_PIN(T931_GPIOAO(0)),    DECL_GPIO_PIN(T931_GPIOAO(1)),
    DECL_GPIO_PIN(T931_GPIOAO(2)),    DECL_GPIO_PIN(T931_GPIOAO(3)),
    DECL_GPIO_PIN(T931_GPIOAO(4)),    DECL_GPIO_PIN(T931_GPIOAO(5)),
    DECL_GPIO_PIN(T931_GPIOAO(6)),    DECL_GPIO_PIN(T931_GPIOAO(7)),
    DECL_GPIO_PIN(T931_GPIOAO(8)),    DECL_GPIO_PIN(T931_GPIOAO(9)),
    DECL_GPIO_PIN(T931_GPIOAO(10)),   DECL_GPIO_PIN(T931_GPIOAO(11)),
    DECL_GPIO_PIN(T931_GPIOAO(12)),   DECL_GPIO_PIN(T931_GPIOAO(13)),
    DECL_GPIO_PIN(T931_GPIOAO(14)),   DECL_GPIO_PIN(T931_GPIOAO(15)),
    DECL_GPIO_PIN(T931_GPIOE(0)),     DECL_GPIO_PIN(T931_GPIOE(1)),
    DECL_GPIO_PIN(T931_GPIOE(2)),     DECL_GPIO_PIN(T931_GPIOE(3)),
    DECL_GPIO_PIN(T931_GPIOE(4)),     DECL_GPIO_PIN(T931_GPIOE(5)),
    DECL_GPIO_PIN(T931_GPIOE(6)),     DECL_GPIO_PIN(T931_GPIOE(7)),
};
#else
#define GPIO_PIN_COUNT 28
static const gpio_pin_t gpio_pins[] = {
    // For wifi.
    DECL_GPIO_PIN(T931_WIFI_HOST_WAKE),
    // For display.
    DECL_GPIO_PIN(GPIO_PANEL_DETECT),
    DECL_GPIO_PIN(GPIO_DDIC_DETECT),
    DECL_GPIO_PIN(GPIO_LCD_RESET),
    // For touch screen.
    DECL_GPIO_PIN(GPIO_TOUCH_INTERRUPT),
    DECL_GPIO_PIN(GPIO_TOUCH_RESET),
    // For audio out.
    DECL_GPIO_PIN(GPIO_AUDIO_SOC_FAULT_L),
    DECL_GPIO_PIN(GPIO_SOC_AUDIO_EN),
    // For Camera.
    DECL_GPIO_PIN(GPIO_VANA_ENABLE),
    DECL_GPIO_PIN(GPIO_VDIG_ENABLE),
    DECL_GPIO_PIN(GPIO_CAM_RESET),
    DECL_GPIO_PIN(GPIO_LIGHT_INTERRUPT),
    // For SPI interface.
    DECL_GPIO_PIN(GPIO_SPICC0_SS0),
    // For buttons.
    DECL_GPIO_PIN(GPIO_VOLUME_UP),
    DECL_GPIO_PIN(GPIO_VOLUME_DOWN),
    DECL_GPIO_PIN(GPIO_VOLUME_BOTH),
    DECL_GPIO_PIN(GPIO_MIC_PRIVACY),
    // For eMMC.
    DECL_GPIO_PIN(T931_EMMC_RST),
    // For SDIO.
    DECL_GPIO_PIN(T931_WIFI_REG_ON),
    // For OpenThread radio
    DECL_GPIO_PIN(GPIO_OT_RADIO_RESET),
    DECL_GPIO_PIN(GPIO_OT_RADIO_INTERRUPT),
    DECL_GPIO_PIN(GPIO_OT_RADIO_BOOTLOADER),
    // LED
    DECL_GPIO_PIN(GPIO_AMBER_LED),
    DECL_GPIO_PIN(GPIO_GREEN_LED),
    // For Bluetooth.
    DECL_GPIO_PIN(GPIO_SOC_WIFI_LPO_32k768),
    DECL_GPIO_PIN(GPIO_SOC_BT_REG_ON),

    // Luis Audio
    DECL_GPIO_PIN(GPIO_AMP_24V_EN),

    // Luis camera supplies, unused on Sherlock
    DECL_GPIO_PIN(GPIO_CAM_VANA_ENABLE),
};
#endif  // FACTORY_BUILD

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
  dev.pid() = PDEV_PID_AMLOGIC_T931;
  dev.did() = PDEV_DID_AMLOGIC_GPIO;
  dev.mmio() = gpio_mmios;
  dev.irq() = gpio_irqs;
  dev.metadata() = gpio_metadata;
  return dev;
}();

zx_status_t Sherlock::GpioInit() {
  static_assert(std::size(gpio_pins) == GPIO_PIN_COUNT, "Incorrect pin count.");

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
// This test binds to system/dev/gpio/gpio-test to check that GPIOs work at all.
// gpio-test enables interrupts and write/read on the test GPIOs configured below.
// #define GPIO_TEST
#ifdef GPIO_TEST
  const pbus_gpio_t gpio_test_gpios[] = {
      {
          .gpio = T931_GPIOZ(5),  // Volume down, not used in this test.
      },
      {
          .gpio = T931_GPIOZ(4),  // Volume up, to test gpio_get_interrupt().
      },
  };

  fpbus::Node gpio_test_dev;
  gpio_test_dev.name() = "sherlock-gpio-test";
  gpio_test_dev.vid() = PDEV_VID_GENERIC;
  gpio_test_dev.pid() = PDEV_PID_GENERIC;
  gpio_test_dev.did() = PDEV_DID_GPIO_TEST;
  gpio_test_dev.gpio() = gpio_test_gpios;
  if ((status = pbus_.DeviceAdd(&gpio_test_dev)) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not add gpio_test_dev %d", __FUNCTION__, status);
    return status;
  }
#endif

  gpio_impl_ = ddk::GpioImplProtocolClient(parent());
  if (!gpio_impl_.is_valid()) {
    zxlogf(ERROR, "%s: device_get_protocol failed", __func__);
    return ZX_ERR_INTERNAL;
  }

  // Luis audio
  if (pid_ == PDEV_PID_LUIS) {
    gpio_impl_.ConfigOut(GPIO_AMP_24V_EN, 1);
  }

  return ZX_OK;
}

}  // namespace sherlock
