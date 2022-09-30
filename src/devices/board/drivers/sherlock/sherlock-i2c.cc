// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"
#include "src/devices/lib/fidl-metadata/i2c.h"

namespace sherlock {
namespace fpbus = fuchsia_hardware_platform_bus;

using i2c_channel_t = fidl_metadata::i2c::Channel;

static const std::vector<fpbus::Mmio> i2c_mmios{
    {{
        .base = T931_I2C_AOBUS_BASE,
        .length = 0x20,
    }},
    {{
        .base = T931_I2C2_BASE,
        .length = 0x20,
    }},
    {{
        .base = T931_I2C3_BASE,
        .length = 0x20,
    }},
};

static const std::vector<fpbus::Irq> i2c_irqs{
    {{
        .irq = T931_I2C_AO_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = T931_I2C2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = T931_I2C3_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const i2c_channel_t luis_ernie_i2c_channels[] = {
    // Backlight I2C
    {
        .bus_id = SHERLOCK_I2C_3,
        .address = 0x2C,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Touch screen I2C
    {
        .bus_id = SHERLOCK_I2C_2,
        .address = 0x40,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Codec 0
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x4c,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // IMX355 Camera Sensor
    {
        .bus_id = SHERLOCK_I2C_3,
        .address = 0x1a,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Light Sensor
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x39,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // IMX355 Camera EEPROM
    {
        .bus_id = SHERLOCK_I2C_3,
        .address = 0x51,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // 0P8_EE_BUCK - platform
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x60,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // CPU_A_BUCK - platform
    {
        .bus_id = SHERLOCK_I2C_3,
        .address = 0x60,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // 0P8_EE_BUCK - form factor
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x6a,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // CPU_A_BUCK - form factor
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x69,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Codec 1
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x2d,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
};

static const i2c_channel_t sherlock_i2c_channels[] = {
    // Backlight I2C
    {
        .bus_id = SHERLOCK_I2C_3,
        .address = 0x2C,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Touch screen I2C
    {
        .bus_id = SHERLOCK_I2C_2,
        .address = 0x38,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Tweeter left
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x6c,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Tweeter right
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x6d,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Woofer
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x6f,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // IMX227 Camera Sensor
    {
        .bus_id = SHERLOCK_I2C_3,
        .address = 0x36,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Light Sensor
    {
        .bus_id = SHERLOCK_I2C_A0_0,
        .address = 0x39,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },

    // LCD Bias
    {
        .bus_id = SHERLOCK_I2C_3,
        .address = 0X3E,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
};

zx_status_t Sherlock::I2cInit() {
  // setup pinmux for our I2C busses
  // i2c_ao_0
  gpio_impl_.SetAltFunction(T931_GPIOAO(2), 1);
  gpio_impl_.SetAltFunction(T931_GPIOAO(3), 1);
  if (pid_ == PDEV_PID_LUIS) {
    gpio_impl_.SetDriveStrength(T931_GPIOAO(2), 3000, nullptr);
    gpio_impl_.SetDriveStrength(T931_GPIOAO(3), 3000, nullptr);
  }
  // i2c2
  gpio_impl_.SetAltFunction(T931_GPIOZ(14), 3);
  gpio_impl_.SetAltFunction(T931_GPIOZ(15), 3);
  // i2c3
  gpio_impl_.SetAltFunction(T931_GPIOA(14), 2);
  gpio_impl_.SetAltFunction(T931_GPIOA(15), 2);

  fpbus::Node dev;
  dev.name() = "gpio";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_AMLOGIC_I2C;
  dev.mmio() = i2c_mmios;
  dev.irq() = i2c_irqs;

  const i2c_channel_t* channels;
  size_t channel_count;
  std::vector<fpbus::Metadata> metadata;
  if (pid_ == PDEV_PID_SHERLOCK) {
    channels = sherlock_i2c_channels;
    channel_count = std::size(sherlock_i2c_channels);
  } else {
    channels = luis_ernie_i2c_channels;
    channel_count = std::size(luis_ernie_i2c_channels);
  };

  auto i2c_status = fidl_metadata::i2c::I2CChannelsToFidl(
      cpp20::span<const i2c_channel_t>(channels, channel_count));
  if (i2c_status.is_error()) {
    zxlogf(ERROR, "%s: failed to fidl encode i2c channels: %d", __func__, i2c_status.error_value());
    return i2c_status.error_value();
  }

  auto& data = i2c_status.value();

  metadata.emplace_back([&]() {
    fpbus::Metadata ret;
    ret.type() = DEVICE_METADATA_I2C_CHANNELS;
    ret.data() = std::move(data);
    return ret;
  }());

  dev.metadata() = std::move(metadata);

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('I2C_');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd I2c(dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd I2c(dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace sherlock
