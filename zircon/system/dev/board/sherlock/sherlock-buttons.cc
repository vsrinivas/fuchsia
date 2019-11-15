// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/buttons.h>
#include <ddk/platform-defs.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

namespace sherlock {

zx_status_t Sherlock::ButtonsInit() {
  static const zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };
  static const zx_bind_inst_t volume_up_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_VOLUME_UP),
  };
  static const zx_bind_inst_t volume_down_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_VOLUME_DOWN),
  };
  static const zx_bind_inst_t volume_both_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_VOLUME_BOTH),
  };
  static const zx_bind_inst_t mic_privacy_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_MIC_PRIVACY),
  };
  static const device_component_part_t volume_up_component[] = {
      {countof(root_match), root_match},
      {countof(volume_up_match), volume_up_match},
  };
  static const device_component_part_t volume_down_component[] = {
      {countof(root_match), root_match},
      {countof(volume_down_match), volume_down_match},
  };
  static const device_component_part_t volume_both_component[] = {
      {countof(root_match), root_match},
      {countof(volume_both_match), volume_both_match},
  };
  static const device_component_part_t mic_privacy_component[] = {
      {countof(root_match), root_match},
      {countof(mic_privacy_match), mic_privacy_match},
  };
  static const device_component_t components[] = {
      {countof(volume_up_component), volume_up_component},
      {countof(volume_down_component), volume_down_component},
      {countof(volume_both_component), volume_both_component},
      {countof(mic_privacy_component), mic_privacy_component},
  };
  // clang-format off
    static constexpr buttons_button_config_t buttons[] = {
        {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP,   0, 0, 0},
        {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_DOWN, 1, 0, 0},
        {BUTTONS_TYPE_DIRECT, BUTTONS_ID_FDR,         2, 0, 0},
        {BUTTONS_TYPE_DIRECT, BUTTONS_ID_MIC_MUTE,    3, 0, 0},
    };
    // No need for internal pull, external pull-ups used.
    static constexpr buttons_gpio_config_t gpios[] = {
        {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_NO_PULL}},
        {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_NO_PULL}},
        {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_NO_PULL}},
        {BUTTONS_GPIO_TYPE_INTERRUPT, 0, {GPIO_NO_PULL}},
    };
  // clang-format on
  static constexpr device_metadata_t available_buttons_metadata[] = {
      {
          .type = DEVICE_METADATA_BUTTONS_BUTTONS,
          .data = &buttons,
          .length = sizeof(buttons),
      },
      {
          .type = DEVICE_METADATA_BUTTONS_GPIOS,
          .data = &gpios,
          .length = sizeof(gpios),
      }};

  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_HID_BUTTONS},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .components = components,
      .components_count = countof(components),
      .coresident_device_index = UINT32_MAX,
      .metadata_list = available_buttons_metadata,
      .metadata_count = countof(available_buttons_metadata),
  };

  zx_status_t status = DdkAddComposite("sherlock-buttons", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
