// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/focaltech/focaltech.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <fbl/algorithm.h>
#include <soc/mt8167/mt8167-hw.h>
#include <soc/mt8167/mt8167-power.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::TouchInit() {
  if (board_info_.vid != PDEV_VID_GOOGLE || board_info_.pid != PDEV_PID_CLEO) {
    return ZX_OK;
  }

  static constexpr uint32_t kDeviceId = FOCALTECH_DEVICE_FT6336;

  static const device_metadata_t touch_metadata[] = {
      {.type = DEVICE_METADATA_PRIVATE, .data = &kDeviceId, .length = sizeof(kDeviceId)},
  };

  const zx_device_prop_t ft_props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_FOCALTOUCH},
  };

  // Composite binding rules for focaltech touch driver.
  constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };
  constexpr zx_bind_inst_t ft_i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x38),
  };
  constexpr zx_bind_inst_t gpio_int_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_TOUCH_INT),
  };
  constexpr zx_bind_inst_t gpio_reset_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_TOUCH_RST),
  };
  const device_fragment_part_t ft_i2c_fragment[] = {
      {fbl::count_of(root_match), root_match},
      {fbl::count_of(ft_i2c_match), ft_i2c_match},
  };
  const device_fragment_part_t gpio_int_fragment[] = {
      {fbl::count_of(root_match), root_match},
      {fbl::count_of(gpio_int_match), gpio_int_match},
  };
  const device_fragment_part_t gpio_reset_fragment[] = {
      {fbl::count_of(root_match), root_match},
      {fbl::count_of(gpio_reset_match), gpio_reset_match},
  };
  const device_fragment_t ft_fragments[] = {
      {fbl::count_of(ft_i2c_fragment), ft_i2c_fragment},
      {fbl::count_of(gpio_int_fragment), gpio_int_fragment},
      {fbl::count_of(gpio_reset_fragment), gpio_reset_fragment},
  };

  static const composite_device_desc_t ft_comp_desc = {
      .props = ft_props,
      .props_count = countof(ft_props),
      .fragments = ft_fragments,
      .fragments_count = countof(ft_fragments),
      .coresident_device_index = UINT32_MAX,
      .metadata_list = touch_metadata,
      .metadata_count = countof(touch_metadata),
  };

  zx_status_t status = DdkAddComposite("touch", &ft_comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to add touch device: %d", __FUNCTION__, status);
  }

  return status;
}

}  // namespace board_mt8167
