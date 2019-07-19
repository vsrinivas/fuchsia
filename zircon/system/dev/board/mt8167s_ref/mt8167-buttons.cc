// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/buttons.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::ButtonsInit() {
  // clang-format off
    static constexpr buttons_button_config_t mt8167s_ref_buttons[] = {
        {BUTTONS_TYPE_MATRIX, BUTTONS_ID_VOLUME_UP,  0, 2, 0},
        {BUTTONS_TYPE_MATRIX, BUTTONS_ID_KEY_A,      1, 2, 0},
        {BUTTONS_TYPE_MATRIX, BUTTONS_ID_KEY_M,      0, 3, 0},
        {BUTTONS_TYPE_MATRIX, BUTTONS_ID_PLAY_PAUSE, 1, 3, 0},
    };
    static constexpr buttons_gpio_config_t mt8167s_ref_gpios[] = {
        {BUTTONS_GPIO_TYPE_INTERRUPT,     BUTTONS_GPIO_FLAG_INVERTED, {GPIO_PULL_UP}},
        {BUTTONS_GPIO_TYPE_INTERRUPT,     BUTTONS_GPIO_FLAG_INVERTED, {GPIO_PULL_UP}},
        {BUTTONS_GPIO_TYPE_MATRIX_OUTPUT, BUTTONS_GPIO_FLAG_INVERTED, {0}           },
        {BUTTONS_GPIO_TYPE_MATRIX_OUTPUT, BUTTONS_GPIO_FLAG_INVERTED, {0}           },
    };
  // clang-format on
  static constexpr pbus_metadata_t mt8167s_ref_metadata[] = {
      {
          .type = DEVICE_METADATA_BUTTONS_BUTTONS,
          .data_buffer = &mt8167s_ref_buttons,
          .data_size = sizeof(mt8167s_ref_buttons),
      },
      {
          .type = DEVICE_METADATA_BUTTONS_GPIOS,
          .data_buffer = &mt8167s_ref_gpios,
          .data_size = sizeof(mt8167s_ref_gpios),
      }};

  static constexpr buttons_button_config_t cleo_buttons[] = {
      {BUTTONS_TYPE_DIRECT, BUTTONS_ID_VOLUME_UP, 0, 0, 0},
      {BUTTONS_TYPE_DIRECT, BUTTONS_ID_MIC_MUTE, 1, 0, 0},
  };
  static constexpr buttons_gpio_config_t cleo_gpios[] = {
      {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_PULL_UP}},
      {BUTTONS_GPIO_TYPE_INTERRUPT, BUTTONS_GPIO_FLAG_INVERTED, {GPIO_NO_PULL}},
  };
  static constexpr pbus_metadata_t cleo_metadata[] = {
      {
          .type = DEVICE_METADATA_BUTTONS_BUTTONS,
          .data_buffer = &cleo_buttons,
          .data_size = sizeof(cleo_buttons),
      },
      {
          .type = DEVICE_METADATA_BUTTONS_GPIOS,
          .data_buffer = &cleo_gpios,
          .data_size = sizeof(cleo_gpios),
      },
  };

  pbus_dev_t dev = {};
  dev.name = "mt8167-buttons";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_HID_BUTTONS;
  static const zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };
  if (board_info_.vid == PDEV_VID_MEDIATEK && board_info_.pid == PDEV_PID_MEDIATEK_8167S_REF) {
    dev.metadata_list = mt8167s_ref_metadata;
    dev.metadata_count = countof(mt8167s_ref_metadata);
    static const zx_bind_inst_t row0_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
        BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_KP_ROW0),
    };
    static const zx_bind_inst_t row1_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
        BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_KP_ROW1),
    };
    static const zx_bind_inst_t col0_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
        BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_KP_COL0),
    };
    static const zx_bind_inst_t col1_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
        BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_KP_COL1),
    };
    static const device_component_part_t row0_component[] = {
        {countof(root_match), root_match},
        {countof(row0_match), row0_match},
    };
    static const device_component_part_t row1_component[] = {
        {countof(root_match), root_match},
        {countof(row1_match), row1_match},
    };
    static const device_component_part_t col0_component[] = {
        {countof(root_match), root_match},
        {countof(col0_match), col0_match},
    };
    static const device_component_part_t col1_component[] = {
        {countof(root_match), root_match},
        {countof(col1_match), col1_match},
    };
    static const device_component_t components[] = {
        {countof(row0_component), row0_component},
        {countof(row1_component), row1_component},
        {countof(col0_component), col0_component},
        {countof(col0_component), col1_component},
    };
    auto status = pbus_.CompositeDeviceAdd(&dev, components, countof(components), UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d\n", __func__, status);
      return status;
    }
  } else if (board_info_.vid == PDEV_VID_GOOGLE && board_info_.pid == PDEV_PID_CLEO) {
    dev.metadata_list = cleo_metadata;
    dev.metadata_count = countof(cleo_metadata);
    static const zx_bind_inst_t volume_up_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
        BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_VOLUME_UP),
    };
    static const zx_bind_inst_t mic_privacy_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
        BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_MIC_PRIVACY),
    };
    static const device_component_part_t volume_up_component[] = {
        {countof(root_match), root_match},
        {countof(volume_up_match), volume_up_match},
    };
    static const device_component_part_t mic_privacy_component[] = {
        {countof(root_match), root_match},
        {countof(mic_privacy_match), mic_privacy_match},
    };
    static const device_component_t components[] = {
        {countof(volume_up_component), volume_up_component},
        {countof(mic_privacy_component), mic_privacy_component},
    };
    auto status = pbus_.CompositeDeviceAdd(&dev, components, countof(components), UINT32_MAX);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d\n", __func__, status);
      return status;
    }
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

}  // namespace board_mt8167
