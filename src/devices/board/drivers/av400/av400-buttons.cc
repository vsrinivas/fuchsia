// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/buttons.h>

#include "av400.h"
#include "src/devices/board/drivers/av400/av400-buttons-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace av400 {
namespace fpbus = fuchsia_hardware_platform_bus;

static constexpr buttons_button_config_t av400_buttons[] = {
    {BUTTONS_TYPE_DIRECT, BUTTONS_ID_MIC_MUTE, 0, 0, 0},
};

static constexpr buttons_gpio_config_t av400_gpios[] = {
    {BUTTONS_GPIO_TYPE_POLL, 0, {.poll = {GPIO_NO_PULL, zx::msec(20).get()}}},
};

static constexpr zx_device_prop_t props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_HID_BUTTONS},
};

static composite_device_desc_t comp_desc = []() {
  composite_device_desc_t desc = {};
  desc.props = props;
  desc.props_count = std::size(props);
  desc.fragments = av400_buttons_fragments;
  desc.fragments_count = std::size(av400_buttons_fragments);
  desc.primary_fragment = "mic-mute";
  desc.spawn_colocated = false;
  return desc;
}();

zx_status_t Av400::ButtonsInit() {
  std::vector<device_metadata_t> buttons_metadata;
  buttons_metadata.emplace_back(device_metadata_t{
      .type = DEVICE_METADATA_BUTTONS_BUTTONS,
      .data = reinterpret_cast<const void*>(&av400_buttons),
      .length = sizeof(av400_buttons),
  });
  buttons_metadata.emplace_back(device_metadata_t{
      .type = DEVICE_METADATA_BUTTONS_GPIOS,
      .data = reinterpret_cast<const void*>(&av400_gpios),
      .length = sizeof(av400_gpios),
  });

  comp_desc.metadata_list = buttons_metadata.data();
  comp_desc.metadata_count = buttons_metadata.size();

  return DdkAddComposite("av400-buttons", &comp_desc);
}

}  // namespace av400
