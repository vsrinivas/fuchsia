// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <limits.h>

#include <soc/aml-a1/a1-gpio.h>
#include <soc/aml-a1/a1-hw.h>

#include "clover.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"
#include "src/devices/lib/fidl-metadata/i2c.h"

namespace clover {
namespace fpbus = fuchsia_hardware_platform_bus;
using i2c_channel_t = fidl_metadata::i2c::Channel;

static const std::vector<fpbus::Mmio> i2c_mmios{
    {{
        .base = A1_I2C_A_BASE,
        .length = A1_I2C_LENGTH,
    }},
    {{
        .base = A1_I2C_B_BASE,
        .length = A1_I2C_LENGTH,
    }},
    {{
        .base = A1_I2C_C_BASE,
        .length = A1_I2C_LENGTH,
    }},
    {{
        .base = A1_I2C_D_BASE,
        .length = A1_I2C_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> i2c_irqs{
    {{
        .irq = A1_I2C_A_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = A1_I2C_B_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = A1_I2C_C_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = A1_I2C_D_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const i2c_channel_t i2c_channels[] = {
    {
        // place holder
        .bus_id = 0,
        .address = 0x41,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
};

zx_status_t Clover::I2cInit() {
  fpbus::Node i2c_dev;
  i2c_dev.name() = "i2c";
  i2c_dev.vid() = PDEV_VID_AMLOGIC;
  i2c_dev.pid() = PDEV_PID_GENERIC;
  i2c_dev.did() = PDEV_DID_AMLOGIC_I2C;
  i2c_dev.mmio() = i2c_mmios;
  i2c_dev.irq() = i2c_irqs;

  auto i2c_gpio = [&arena = gpio_init_arena_](
                      uint64_t alt_function) -> fuchsia_hardware_gpio_init::wire::GpioInitOptions {
    return fuchsia_hardware_gpio_init::wire::GpioInitOptions::Builder(arena)
        .alt_function(alt_function)
        .Build();
  };

  gpio_init_steps_.push_back({A1_I2C_A_SCL, i2c_gpio(A1_I2C_A_SCL_FN)});
  gpio_init_steps_.push_back({A1_I2C_A_SDA, i2c_gpio(A1_I2C_A_SDA_FN)});
  gpio_init_steps_.push_back({A1_I2C_B_SCL, i2c_gpio(A1_I2C_B_SCL_FN)});
  gpio_init_steps_.push_back({A1_I2C_B_SDA, i2c_gpio(A1_I2C_B_SDA_FN)});
  gpio_init_steps_.push_back({A1_I2C_C_SCL, i2c_gpio(A1_I2C_C_SCL_FN)});
  gpio_init_steps_.push_back({A1_I2C_C_SDA, i2c_gpio(A1_I2C_C_SDA_FN)});

  auto i2c_status = fidl_metadata::i2c::I2CChannelsToFidl(i2c_channels);
  if (i2c_status.is_error()) {
    zxlogf(ERROR, "I2cInit: Failed to fidl encode i2c channels: %s", i2c_status.status_string());
    return i2c_status.error_value();
  }

  auto& data = i2c_status.value();

  std::vector<fpbus::Metadata> i2c_metadata{
      {{
          .type = DEVICE_METADATA_I2C_CHANNELS,
          .data = std::move(data),
      }},
  };
  i2c_dev.metadata() = std::move(i2c_metadata);

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('I2C_');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, i2c_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "NodeAdd I2c(i2c_dev) request failed: %s", result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "NodeAdd I2c(i2c_dev) failed: %s", zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}
}  // namespace clover
