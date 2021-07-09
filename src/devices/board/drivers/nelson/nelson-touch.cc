// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/focaltech/focaltech.h>
#include <limits.h>
#include <unistd.h>

#include <fbl/algorithm.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "nelson-gpios.h"
#include "nelson.h"

namespace nelson {

static const FocaltechMetadata device_info = {
    .device_id = FOCALTECH_DEVICE_FT3X27,
    .needs_firmware = false,
};
static const device_metadata_t ft3x27_touch_metadata[] = {
    {.type = DEVICE_METADATA_PRIVATE, .data = &device_info, .length = sizeof(device_info)},
};

// Composite binding rules for focaltech touch driver.
const zx_bind_inst_t ft_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, NELSON_I2C_2),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, I2C_FOCALTECH_TOUCH_ADDR),
};
const zx_bind_inst_t goodix_i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, NELSON_I2C_2),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, I2C_GOODIX_TOUCH_ADDR),
};
static const zx_bind_inst_t gpio_int_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_TOUCH_INTERRUPT),
};
static const zx_bind_inst_t gpio_reset_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_TOUCH_RESET),
};
static const device_fragment_part_t ft_i2c_fragment[] = {
    {countof(ft_i2c_match), ft_i2c_match},
};
static const device_fragment_part_t goodix_i2c_fragment[] = {
    {countof(goodix_i2c_match), goodix_i2c_match},
};
static const device_fragment_part_t gpio_int_fragment[] = {
    {countof(gpio_int_match), gpio_int_match},
};
static const device_fragment_part_t gpio_reset_fragment[] = {
    {countof(gpio_reset_match), gpio_reset_match},
};
static const device_fragment_t ft_fragments[] = {
    {"i2c", countof(ft_i2c_fragment), ft_i2c_fragment},
    {"gpio-int", countof(gpio_int_fragment), gpio_int_fragment},
    {"gpio-reset", countof(gpio_reset_fragment), gpio_reset_fragment},
};
static const device_fragment_t goodix_fragments[] = {
    {"i2c", countof(goodix_i2c_fragment), goodix_i2c_fragment},
    {"gpio-int", countof(gpio_int_fragment), gpio_int_fragment},
    {"gpio-reset", countof(gpio_reset_fragment), gpio_reset_fragment},
};

zx_status_t Nelson::TouchInit() {
  const uint32_t display_id = GetDisplayId();
  zxlogf(INFO, "Board rev: %u", GetBoardRev());
  zxlogf(INFO, "Panel ID: 0b%d%d", display_id & 0b10 ? 1 : 0, display_id & 0b01 ? 1 : 0);

  if (GetBoardRev() < BOARD_REV_P2) {
    return TouchInitP1();
  }

  const zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GOODIX},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_GOODIX_GT6853},
  };

  const bool use_9365_config = Is9365Ddic();
  const device_metadata_t touch_metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data = &use_9365_config,
          .length = sizeof(use_9365_config),
      },
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .fragments = goodix_fragments,
      .fragments_count = countof(goodix_fragments),
      .primary_fragment = "i2c",
      .coresident_device_index = UINT32_MAX,
      .metadata_list = touch_metadata,
      .metadata_count = countof(touch_metadata),
  };
  zx_status_t status = DdkAddComposite("gt6853-touch", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "nelson_touch_init(gt6853): composite_device_add failed: %d", status);
    return status;
  }
  return ZX_OK;
}

zx_status_t Nelson::TouchInitP1() {
  /* Two variants of display are supported, one with BOE display panel and
        ft3x27 touch controller, the other with INX panel and Goodix touch
        controller.  This GPIO input is used to identify each.
        Logic 0 for BOE/ft3x27 combination
        Logic 1 for Innolux/Goodix combination
  */
  const uint8_t gpio_state = GetDisplayId() & 1;
  zxlogf(INFO, "%s - Touch type: %s", __func__, (gpio_state ? "GTx8x" : "FT3x27"));
  if (!gpio_state) {
    const zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GOOGLE},
        {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_NELSON},
        {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_GOODIX_GTX8X},
    };

    const composite_device_desc_t comp_desc = {
        .props = props,
        .props_count = countof(props),
        .fragments = goodix_fragments,
        .fragments_count = countof(goodix_fragments),
        .primary_fragment = "i2c",
        .coresident_device_index = UINT32_MAX,
        .metadata_list = nullptr,
        .metadata_count = 0,
    };
    zx_status_t status = DdkAddComposite("gtx8x-touch", &comp_desc);
    if (status != ZX_OK) {
      zxlogf(INFO, "nelson_touch_init(gt92xx): composite_device_add failed: %d", status);
      return status;
    }
  } else {
    const zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
        {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_NELSON},
        {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_FOCALTOUCH},
    };

    const composite_device_desc_t comp_desc = {
        .props = props,
        .props_count = countof(props),
        .fragments = ft_fragments,
        .fragments_count = countof(ft_fragments),
        .primary_fragment = "i2c",
        .coresident_device_index = UINT32_MAX,
        .metadata_list = ft3x27_touch_metadata,
        .metadata_count = std::size(ft3x27_touch_metadata),
    };

    zx_status_t status = DdkAddComposite("ft3x27-touch", &comp_desc);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s(ft3x27): CompositeDeviceAdd failed: %d", __func__, status);
      return status;
    }
  }

  return ZX_OK;
}

}  // namespace nelson
