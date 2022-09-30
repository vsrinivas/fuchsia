// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/status.h>

#include "nelson-gpios.h"
#include "nelson.h"
#include "src/devices/board/drivers/nelson/nelson_selina_bind.h"

namespace nelson {
namespace fpbus = fuchsia_hardware_platform_bus;

static constexpr zx_device_prop_t props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_NELSON},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_RADAR_SENSOR},
};

static composite_device_desc_t composite_dev = []() {
  composite_device_desc_t desc = {};
  desc.props = props;
  desc.props_count = std::size(props);
  desc.fragments = selina_fragments;
  desc.fragments_count = std::size(selina_fragments);
  desc.primary_fragment = "spi";
  desc.spawn_colocated = true;
  return desc;
}();

zx_status_t Nelson::SelinaInit() {
  // Enable the clock to the Selina sensor on proto boards. GPIOH_8 is open-drain: set it to input
  // so that it gets pulled up by the sensor board. This pin is not connected to anything on DVT2.
  if (GetBoardRev() == BOARD_REV_P1) {
    gpio_impl_.SetAltFunction(GPIO_SOC_SELINA_OSC_EN, 0);
    gpio_impl_.ConfigIn(GPIO_SOC_SELINA_OSC_EN, GPIO_NO_PULL);
  }

  return DdkAddComposite("selina", &composite_dev);
}

}  // namespace nelson
