// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/gpioimpl/c/banjo.h>
#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/as370/as370-i2c.h>

#include "as370.h"
#include "src/devices/lib/fidl-metadata/i2c.h"

namespace board_as370 {
namespace fpbus = fuchsia_hardware_platform_bus;
using i2c_channel_t = fidl_metadata::i2c::Channel;

zx_status_t As370::I2cInit() {
  zx_status_t status;

  constexpr uint32_t i2c_gpios[] = {
      as370::kI2c0Sda,
      as370::kI2c0Scl,
      as370::kI2c1Sda,
      as370::kI2c1Scl,
  };

  ddk::GpioImplProtocolClient gpio(parent());

  if (!gpio.is_valid()) {
    zxlogf(ERROR, "%s: Failed to create GPIO protocol client", __func__);
    return ZX_ERR_INTERNAL;
  }

  for (uint32_t i = 0; i < std::size(i2c_gpios); i++) {
    status = gpio.SetAltFunction(i2c_gpios[i], 1);  // 1 == SDA/SCL pinmux setting.
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: GPIO SetAltFunction failed %d", __FUNCTION__, status);
      return status;
    }
  }

  static const std::vector<fpbus::Mmio> i2c_mmios{
      {{
          .base = as370::kI2c0Base,
          .length = as370::kI2c0Size,
      }},
      {{
          .base = as370::kI2c1Base,
          .length = as370::kI2c1Size,
      }},
  };

  static const std::vector<fpbus::Irq> i2c_irqs{
      {{
          .irq = as370::kI2c0Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      }},
      {{
          .irq = as370::kI2c1Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      }},
  };

  // I2C channels for as370 visalia
  constexpr i2c_channel_t visalia_i2c_channels[] = {
      // For audio out
      {
          .bus_id = 0,
          .address = 0x31,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
      // TI LP5018 LED driver
      {
          .bus_id = 0,
          .address = 0x29,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
      // For power regulator
      {
          .bus_id = 0,
          .address = 0x66,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
      // Cypress touch sensor
      {
          .bus_id = 0,
          .address = 0x37,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
  };

  auto visalia_i2c_channels_fidl = fidl_metadata::i2c::I2CChannelsToFidl(visalia_i2c_channels);
  if (visalia_i2c_channels_fidl.is_error()) {
    zxlogf(ERROR, "Failed to FIDL encode I2C channel metadata: %d",
           visalia_i2c_channels_fidl.error_value());
    return visalia_i2c_channels_fidl.error_value();
  }

  std::vector<fpbus::Metadata> visalia_i2c_metadata{
      {{
          .type = DEVICE_METADATA_I2C_CHANNELS,
      }},
  };

  fpbus::Node i2c_dev;
  i2c_dev.name() = "i2c";
  i2c_dev.vid() = PDEV_VID_GENERIC;
  i2c_dev.pid() = PDEV_PID_GENERIC;
  i2c_dev.did() = PDEV_DID_DW_I2C;
  i2c_dev.mmio() = i2c_mmios;
  i2c_dev.irq() = i2c_irqs;
  i2c_dev.metadata() = visalia_i2c_metadata;

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

}  // namespace board_as370
