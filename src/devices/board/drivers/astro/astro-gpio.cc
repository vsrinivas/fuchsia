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
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro-gpios.h"
#include "astro.h"

// uncomment to disable LED blinky test
// #define GPIO_TEST

namespace astro {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> gpio_mmios{
    {{
        .base = S905D2_GPIO_BASE,
        .length = S905D2_GPIO_LENGTH,
    }},
    {{
        .base = S905D2_GPIO_AO_BASE,
        .length = S905D2_GPIO_AO_LENGTH,
    }},
    {{
        .base = S905D2_GPIO_INTERRUPT_BASE,
        .length = S905D2_GPIO_INTERRUPT_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> gpio_irqs{
    {{
        .irq = S905D2_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D2_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D2_GPIO_IRQ_2,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D2_GPIO_IRQ_3,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D2_GPIO_IRQ_4,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D2_GPIO_IRQ_5,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D2_GPIO_IRQ_6,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = S905D2_GPIO_IRQ_7,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
};

// GPIOs to expose from generic GPIO driver.
static const gpio_pin_t gpio_pins[] = {
    // For wifi.
    DECL_GPIO_PIN(S905D2_WIFI_SDIO_WAKE_HOST),
    // For display.
    DECL_GPIO_PIN(GPIO_PANEL_DETECT),
    DECL_GPIO_PIN(GPIO_LCD_RESET),
    // For touch screen.
    DECL_GPIO_PIN(GPIO_TOUCH_INTERRUPT),
    DECL_GPIO_PIN(GPIO_TOUCH_RESET),
    // For light sensor.
    DECL_GPIO_PIN(GPIO_LIGHT_INTERRUPT),
    // For audio.
    DECL_GPIO_PIN(GPIO_AUDIO_SOC_FAULT_L),
    DECL_GPIO_PIN(GPIO_SOC_AUDIO_EN),
    // For buttons.
    DECL_GPIO_PIN(GPIO_VOLUME_UP),
    DECL_GPIO_PIN(GPIO_VOLUME_DOWN),
    DECL_GPIO_PIN(GPIO_VOLUME_BOTH),
    DECL_GPIO_PIN(GPIO_MIC_PRIVACY),
    // For SDIO.
    DECL_GPIO_PIN(GPIO_SDIO_RESET),
    // For Bluetooth.
    DECL_GPIO_PIN(GPIO_SOC_WIFI_LPO_32k768),
    DECL_GPIO_PIN(GPIO_SOC_BT_REG_ON),
    // For lights.
    DECL_GPIO_PIN(GPIO_AMBER_LED),
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
  dev.pid() = PDEV_PID_AMLOGIC_S905D2;
  dev.did() = PDEV_DID_AMLOGIC_GPIO;
  dev.mmio() = gpio_mmios;
  dev.irq() = gpio_irqs;
  dev.metadata() = gpio_metadata;
  return dev;
}();

zx_status_t Astro::GpioInit() {
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

#ifdef GPIO_TEST
  static const pbus_gpio_t gpio_test_gpios[] = {{
                                                    // SYS_LED
                                                    .gpio = S905D2_GPIOAO(11),
                                                },
                                                {
                                                    // JTAG Adapter Pin
                                                    .gpio = S905D2_GPIOAO(6),
                                                }};

  fpbus::Node gpio_test_dev;
  fpbus::Node dev = {};
  dev.name() = "astro-gpio-test";
  dev.vid() = PDEV_VID_GENERIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_GPIO_TEST;
  dev.gpio() = gpio_test_gpios;
  return dev;
}
();

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

}  // namespace astro
