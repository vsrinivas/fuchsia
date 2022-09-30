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

#include "test.h"

namespace board_test {
namespace fpbus = fuchsia_hardware_platform_bus;

namespace {

static const gpio_pin_t gpio_pins[] = {
    DECL_GPIO_PIN(1),
    DECL_GPIO_PIN(3),
    DECL_GPIO_PIN(5),
};

static const std::vector<fpbus::Metadata> gpio_metadata{
    []() {
      fpbus::Metadata ret;
      ret.type() = DEVICE_METADATA_GPIO_PINS;
      ret.data() =
          std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&gpio_pins),
                               reinterpret_cast<const uint8_t*>(&gpio_pins) + sizeof(gpio_pins));
      return ret;
    }(),
};

}  // namespace

zx_status_t TestBoard::GpioInit() {
  fpbus::Node gpio_dev;
  gpio_dev.name() = "gpio";
  gpio_dev.vid() = PDEV_VID_TEST;
  gpio_dev.pid() = PDEV_PID_PBUS_TEST;
  gpio_dev.did() = PDEV_DID_TEST_GPIO;
  gpio_dev.metadata() = gpio_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('TGPI');
  auto result = pbus_.buffer(arena)->ProtocolNodeAdd(ZX_PROTOCOL_GPIO_IMPL,
                                                     fidl::ToWire(fidl_arena, gpio_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: DeviceAdd Gpio request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: DeviceAdd Gpio failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace board_test
