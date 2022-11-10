// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/buttons.h>

#include "as370.h"
#include "src/devices/board/drivers/as370/as370-buttons-bind.h"

namespace board_as370 {

constexpr buttons_button_config_t mute_button{BUTTONS_TYPE_DIRECT, BUTTONS_ID_MIC_MUTE, 0, 0, 0};

constexpr buttons_gpio_config_t mute_gpio{
    BUTTONS_GPIO_TYPE_INTERRUPT,
    0,
    {.interrupt = {GPIO_NO_PULL}},
};

constexpr device_metadata_t available_buttons_metadata[] = {
    {
        .type = DEVICE_METADATA_BUTTONS_BUTTONS,
        .data = &mute_button,
        .length = sizeof(mute_button),
    },
    {
        .type = DEVICE_METADATA_BUTTONS_GPIOS,
        .data = &mute_gpio,
        .length = sizeof(mute_gpio),
    }};

zx_status_t As370::ButtonsInit() {
  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_HID_BUTTONS},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = std::size(props),
      .fragments = as370_buttons_fragments,
      .fragments_count = std::size(as370_buttons_fragments),
      .primary_fragment = "mic-mute",
      .spawn_colocated = false,
      .metadata_list = available_buttons_metadata,
      .metadata_count = std::size(available_buttons_metadata),
  };

  zx_status_t status = DdkAddComposite("as370-buttons", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "CompositeDeviceAdd failed: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace board_as370
