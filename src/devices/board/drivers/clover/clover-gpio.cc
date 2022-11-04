// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/gpio.h>
#include <soc/aml-a1/a1-gpio.h>
#include <soc/aml-a1/a1-hw.h>

#include "clover.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace clover {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> gpio_mmios{
    {{
        .base = A1_GPIO_BASE,
        .length = A1_GPIO_LENGTH,
    }},
    // A113L no AO mmio, as a placeholder
    {{
        .base = A1_GPIO_BASE,
        .length = A1_GPIO_LENGTH,
    }},
    {{
        .base = A1_GPIO_INTERRUPT_BASE,
        .length = A1_GPIO_INTERRUPT_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> gpio_irqs{
    {{
        .irq = A1_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A1_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A1_GPIO_IRQ_2,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A1_GPIO_IRQ_3,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A1_GPIO_IRQ_4,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A1_GPIO_IRQ_5,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A1_GPIO_IRQ_6,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A1_GPIO_IRQ_7,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
};

// GPIOs to expose from generic GPIO driver.
static const gpio_pin_t gpio_pins[] = {
    DECL_GPIO_PIN(A1_I2C_A_SCL),  DECL_GPIO_PIN(A1_I2C_A_SDA),  DECL_GPIO_PIN(A1_I2C_B_SCL),
    DECL_GPIO_PIN(A1_I2C_B_SDA),  DECL_GPIO_PIN(A1_I2C_C_SCL),  DECL_GPIO_PIN(A1_I2C_C_SDA),
    DECL_GPIO_PIN(A1_SPI_A_MOSI), DECL_GPIO_PIN(A1_SPI_A_MISO), DECL_GPIO_PIN(A1_SPI_A_CLK),
    DECL_GPIO_PIN(A1_SPI_A_SS0),
};

zx_status_t Clover::GpioInit() {
  fuchsia_hardware_gpio_init::wire::GpioInitMetadata metadata;
  metadata.steps = fidl::VectorView<fuchsia_hardware_gpio_init::wire::GpioInitStep>::FromExternal(
      gpio_init_steps_.data(), gpio_init_steps_.size());

  fidl::unstable::OwnedEncodedMessage<fuchsia_hardware_gpio_init::wire::GpioInitMetadata> encoded(
      fidl::internal::WireFormatVersion::kV2, &metadata);
  if (!encoded.ok()) {
    zxlogf(ERROR, "Failed to encode GPIO init metadata: %s", encoded.status_string());
    return encoded.status();
  }

  auto message = encoded.GetOutgoingMessage().CopyBytes();

  static const std::vector<fpbus::Metadata> gpio_metadata{
      {{
          .type = DEVICE_METADATA_GPIO_PINS,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&gpio_pins),
              reinterpret_cast<const uint8_t*>(&gpio_pins) + sizeof(gpio_pins)),
      }},
      {{
          .type = DEVICE_METADATA_GPIO_INIT_STEPS,
          .data = std::vector<uint8_t>(message.data(), message.data() + message.size()),
      }},
  };

  static const fpbus::Node gpio_dev = []() {
    fpbus::Node dev = {};
    dev.name() = "gpio";
    dev.vid() = PDEV_VID_AMLOGIC;
    dev.pid() = PDEV_PID_AMLOGIC_A1;
    dev.did() = PDEV_DID_AMLOGIC_GPIO;
    dev.mmio() = gpio_mmios;
    dev.irq() = gpio_irqs;
    dev.metadata() = gpio_metadata;
    return dev;
  }();

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('GPIO');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, gpio_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "NodeAdd Gpio(gpio_dev) request failed: %s", result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "NodeAdd Gpio(gpio_dev) failed: %s", zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace clover
