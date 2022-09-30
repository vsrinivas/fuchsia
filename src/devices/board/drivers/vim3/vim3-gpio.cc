// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/gpio.h>
#include <soc/aml-a311d/a311d-gpio.h>
#include <soc/aml-a311d/a311d-hw.h>

#include "vim3-gpios.h"
#include "vim3.h"

namespace vim3 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> gpio_mmios{
    {{
        .base = A311D_GPIO_BASE,
        .length = A311D_GPIO_LENGTH,
    }},
    {{
        .base = A311D_GPIO_AO_BASE,
        .length = A311D_GPIO_AO_LENGTH,
    }},
    {{
        .base = A311D_GPIO_INTERRUPT_BASE,
        .length = A311D_GPIO_INTERRUPT_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> gpio_irqs{
    {{
        .irq = A311D_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A311D_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A311D_GPIO_IRQ_2,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A311D_GPIO_IRQ_3,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A311D_GPIO_IRQ_4,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A311D_GPIO_IRQ_5,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A311D_GPIO_IRQ_6,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
    {{
        .irq = A311D_GPIO_IRQ_7,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    }},
};

// GPIOs to expose from generic GPIO driver.
static const gpio_pin_t gpio_pins[] = {
    DECL_GPIO_PIN(VIM3_J4_PIN_39),     DECL_GPIO_PIN(VIM3_ETH_MAC_INTR),
    DECL_GPIO_PIN(A311D_GPIOBOOT(12)), DECL_GPIO_PIN(A311D_GPIOX(6)),
    DECL_GPIO_PIN(VIM3_HPD_IN),        DECL_GPIO_PIN(VIM3_FUSB302_INT),
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
  dev.pid() = PDEV_PID_AMLOGIC_A311D;
  dev.did() = PDEV_DID_AMLOGIC_GPIO;
  dev.mmio() = gpio_mmios;
  dev.irq() = gpio_irqs;
  dev.metadata() = gpio_metadata;
  return dev;
}();

const zx_device_prop_t gpio_expander_props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TI},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TI_TCA6408A},
};

constexpr zx_bind_inst_t gpio_expander_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_FIDL_PROTOCOL, ZX_FIDL_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x20),
};

constexpr device_fragment_part_t gpio_expander_i2c_fragment[] = {
    {std::size(gpio_expander_i2c_match), gpio_expander_i2c_match},
};

constexpr device_fragment_t gpio_expander_fragments[] = {
    {"i2c", std::size(gpio_expander_i2c_fragment), gpio_expander_i2c_fragment},
};

static const gpio_pin_t gpio_expander_pins[] = {
    DECL_GPIO_PIN(VIM3_SD_MODE),
};

static const uint32_t gpio_expander_pin_offset = VIM3_EXPANDER_GPIO_START;

static const device_metadata_t gpio_expander_metadata[] = {
    {
        .type = DEVICE_METADATA_GPIO_PINS,
        .data = &gpio_expander_pins,
        .length = sizeof(gpio_expander_pins),
    },
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data = &gpio_expander_pin_offset,
        .length = sizeof(gpio_expander_pin_offset),
    },
};

static composite_device_desc_t gpio_expander_dev = []() {
  composite_device_desc_t dev = {};
  dev.props = gpio_expander_props;
  dev.props_count = std::size(gpio_expander_props);
  dev.fragments = gpio_expander_fragments;
  dev.fragments_count = std::size(gpio_expander_fragments);
  dev.primary_fragment = gpio_expander_fragments[0].name;
  dev.spawn_colocated = false;
  dev.metadata_list = gpio_expander_metadata;
  dev.metadata_count = std::size(gpio_expander_metadata);
  return dev;
}();

zx_status_t Vim3::GpioInit() {
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
    zxlogf(ERROR, "%s: device_get_protocol failed", __func__);
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status = DdkAddComposite("gpio-expander", &gpio_expander_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkAddComposite for gpio-expander failed %d", status);
    return status;
  }

  return ZX_OK;
}

}  // namespace vim3
