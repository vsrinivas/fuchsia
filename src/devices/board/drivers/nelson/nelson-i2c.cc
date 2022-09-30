// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-common/aml-i2c.h>
#include <soc/aml-s905d3/s905d3-gpio.h>
#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson-gpios.h"
#include "nelson.h"
#include "src/devices/lib/fidl-metadata/i2c.h"

namespace nelson {
namespace fpbus = fuchsia_hardware_platform_bus;
using i2c_channel_t = fidl_metadata::i2c::Channel;

static const std::vector<fpbus::Mmio> i2c_mmios{
    {{
        .base = S905D3_I2C_AO_0_BASE,
        .length = 0x20,
    }},
    {{
        .base = S905D3_I2C2_BASE,
        .length = 0x20,
    }},
    {{
        .base = S905D3_I2C3_BASE,
        .length = 0x20,
    }},
};

static const aml_i2c_delay_values i2c_delays[] = {
    // These are based on a core clock rate of 166 Mhz (fclk_div4 / 3).
    {819, 417},  // I2C_AO 100 kHz
    {152, 125},  // I2C_2 400 kHz
    {152, 125},  // I2C_3 400 kHz
};

static const std::vector<fpbus::Irq> i2c_irqs{
    {{
        .irq = S905D3_I2C_AO_0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = S905D3_I2C2_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
    {{
        .irq = S905D3_I2C3_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const i2c_channel_t i2c_channels[] = {
    // Backlight I2C
    {
        .bus_id = NELSON_I2C_3,
        .address = I2C_BACKLIGHT_ADDR,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Focaltech touch screen
    {
        // binds as composite device
        .bus_id = NELSON_I2C_2,
        .address = I2C_FOCALTECH_TOUCH_ADDR,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Goodix touch screen
    {
        // binds as composite device
        .bus_id = NELSON_I2C_2,
        .address = I2C_GOODIX_TOUCH_ADDR,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Light sensor
    {
        // binds as composite device
        .bus_id = NELSON_I2C_A0_0,
        .address = I2C_AMBIENTLIGHT_ADDR,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Audio output
    {
        // binds as composite device
        .bus_id = NELSON_I2C_3,
        .address = I2C_AUDIO_CODEC_ADDR,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Audio output
    {
        // binds as composite device
        .bus_id = NELSON_I2C_3,
        .address = I2C_AUDIO_CODEC_ADDR_P2,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    // Power sensors
    {
        .bus_id = NELSON_I2C_3,
        .address = I2C_TI_INA231_MLB_ADDR,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    {
        .bus_id = NELSON_I2C_3,
        .address = I2C_TI_INA231_SPEAKERS_ADDR,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
    {
        .bus_id = NELSON_I2C_A0_0,
        .address = I2C_SHTV3_ADDR,
        .vid = PDEV_VID_SENSIRION,
        .pid = 0,
        .did = PDEV_DID_SENSIRION_SHTV3,
    },
    {
        .bus_id = NELSON_I2C_3,
        .address = I2C_TI_INA231_MLB_ADDR_PROTO,
        .vid = 0,
        .pid = 0,
        .did = 0,
    },
};

static fpbus::Node i2c_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "i2c";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_AMLOGIC_I2C;
  dev.mmio() = i2c_mmios;
  dev.irq() = i2c_irqs;
  return dev;
}();

zx_status_t Nelson::I2cInit() {
  // setup pinmux for our I2C busses

  // i2c_ao_0
  gpio_impl_.SetAltFunction(GPIO_SOC_SENSORS_I2C_SCL, 1);
  gpio_impl_.SetDriveStrength(GPIO_SOC_SENSORS_I2C_SCL, 2500, nullptr);
  gpio_impl_.SetAltFunction(GPIO_SOC_SENSORS_I2C_SDA, 1);
  gpio_impl_.SetDriveStrength(GPIO_SOC_SENSORS_I2C_SDA, 2500, nullptr);
  // i2c2
  gpio_impl_.SetAltFunction(GPIO_SOC_TOUCH_I2C_SDA, 3);
  gpio_impl_.SetDriveStrength(GPIO_SOC_TOUCH_I2C_SDA, 3000, nullptr);
  gpio_impl_.SetAltFunction(GPIO_SOC_TOUCH_I2C_SCL, 3);
  gpio_impl_.SetDriveStrength(GPIO_SOC_TOUCH_I2C_SCL, 3000, nullptr);
  // i2c3
  gpio_impl_.SetAltFunction(GPIO_SOC_AV_I2C_SDA, 2);
  gpio_impl_.SetDriveStrength(GPIO_SOC_AV_I2C_SDA, 3000, nullptr);
  gpio_impl_.SetAltFunction(GPIO_SOC_AV_I2C_SCL, 2);
  gpio_impl_.SetDriveStrength(GPIO_SOC_AV_I2C_SCL, 3000, nullptr);

  auto i2c_status = fidl_metadata::i2c::I2CChannelsToFidl(i2c_channels);
  if (i2c_status.is_error()) {
    zxlogf(ERROR, "%s: failed to fidl encode i2c channels: %d", __func__, i2c_status.error_value());
    return i2c_status.error_value();
  }

  auto& data = i2c_status.value();
  std::vector<fpbus::Metadata> i2c_metadata{
      {{
          .type = DEVICE_METADATA_I2C_CHANNELS,
          .data = std::move(data),
      }},
      {{
          .type = DEVICE_METADATA_PRIVATE,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&i2c_delays),
              reinterpret_cast<const uint8_t*>(&i2c_delays) + sizeof(i2c_delays)),
      }},
  };
  i2c_dev.metadata() = std::move(i2c_metadata);

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('I2C_');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, i2c_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd I2c(i2c_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd I2c(i2c_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace nelson
