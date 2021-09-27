// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/buttons.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/sherlock-buttons-bind.h"

namespace sherlock {

zx_status_t Sherlock::ButtonsInit() {
  static constexpr buttons_button_config_t sherlock_buttons[] = {
      {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP, 0, 0, 0},
      {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_DOWN, 1, 0, 0},
      {BUTTONS_TYPE_DIRECT, BUTTONS_ID_FDR, 2, 0, 0},
      {BUTTONS_TYPE_DIRECT, BUTTONS_ID_MIC_AND_CAM_MUTE, 3, 0, 0},
  };
  constexpr size_t kSherlockButtonCount = 4;

  static constexpr buttons_button_config_t luis_buttons[] = {
      {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP, 0, 0, 0},
      {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_DOWN, 1, 0, 0},
      {BUTTONS_TYPE_DIRECT, BUTTONS_ID_FDR, 2, 0, 0},
      {BUTTONS_TYPE_DIRECT, BUTTONS_ID_MIC_MUTE, 3, 0, 0},
      {BUTTONS_TYPE_DIRECT, BUTTONS_ID_CAM_MUTE, 4, 0, 0},
  };
  // TODO(fxbug.dev/58662): Re-enable camera mute switch.
  // constexpr size_t kLuisButtonCount = 5;
  constexpr size_t kLuisButtonCount = 4;

  // No need for internal pull, external pull-ups used.
  static constexpr buttons_gpio_config_t gpios[] = {
      {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {.interrupt = {GPIO_PULL_UP}}},
      {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {.interrupt = {GPIO_PULL_UP}}},
      {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {.interrupt = {GPIO_NO_PULL}}},
      {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {.interrupt = {GPIO_NO_PULL}}},
      // CAM_MUTE high means the camera is enabled, low means the camera is disabled.
      {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {.interrupt = {GPIO_NO_PULL}}},
  };

  const void* buttons = &sherlock_buttons;
  size_t button_count = kSherlockButtonCount;
  if (pid_ == PDEV_PID_LUIS) {
    buttons = &luis_buttons;
    button_count = kLuisButtonCount;
  }

  const device_metadata_t available_buttons_metadata[] = {
      {
          .type = DEVICE_METADATA_BUTTONS_BUTTONS,
          .data = buttons,
          .length = button_count * sizeof(buttons_button_config_t),
      },
      {
          .type = DEVICE_METADATA_BUTTONS_GPIOS,
          .data = &gpios,
          .length = button_count * sizeof(gpios[0]),
      },
  };

  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_HID_BUTTONS},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .fragments = sherlock_buttons_fragments,
      .fragments_count = kSherlockButtonCount,
      .primary_fragment = "volume-up",  // ???
      .spawn_colocated = false,
      .metadata_list = available_buttons_metadata,
      .metadata_count = countof(available_buttons_metadata),
  };

  zx_status_t status = DdkAddComposite("sherlock-buttons", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
